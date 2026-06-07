import serial
import time
import tkinter as tk
import threading
import os
import math

# Pygame import
try:
    import pygame
except ImportError:
    print("HATA: pygame veya pygame-ce kutuphane eksik.")
    print("Cozum: 'pip install pygame-ce' yazarak kurun.")
    input("Kapatmak icin Enter'a basin...")
    exit()

# ==========================================
# AYARLAR
# ==========================================
PORT = "COM7"       # <-- Arduino/ESP32 portunuzu buraya yazin
BAUD = 115200
JOYSTICK_INDEX = 0  # PS4 Controller (0. joystick)

# GAZ HIZI AYARI - Değeri büyütürseniz gaz daha hızlı artar/azalır
THROTTLE_SPEED = 12  # Eski: 4, Şimdi: 12 (3x daha hızlı)

# ==========================================
# ARDUINO BAGLANTISI
# ==========================================
arduino = None
try:
    print(f"Arduino'ya baglaniliyor ({PORT})...")
    arduino = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(2)
    print("Arduino Baglantisi Basarili!")
except Exception as e:
    print(f"UYARI: {PORT} portu acilamadi: {e}")
    print("Arayuz yine de acilacak, Arduino'suz test edebilirsiniz.")
    arduino = None

# ==========================================
# PYGAME JOYSTICK BAGLANTISI
# ==========================================
os.environ["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"
pygame.init()
pygame.joystick.init()

joystick = None
joystick_name = "Bulunamadi"
if pygame.joystick.get_count() > 0:
    joystick = pygame.joystick.Joystick(JOYSTICK_INDEX)
    joystick_name = joystick.get_name()
    print(f"Kol Bulundu: {joystick_name}")
    print(f"  Eksen sayisi: {joystick.get_numaxes()}")
    print(f"  Buton sayisi: {joystick.get_numbuttons()}")
else:
    print("DIKKAT: Hicbir oyun kolu algilanamadi!")

# ==========================================
# DRONE DURUMU
# ==========================================
state = {"T": 0, "P": 0, "R": 0, "Y": 0, "C": 0}
armed = False          # Drone arm/disarm durumu
button_states = {}     # Basılan butonları takip etmek için
last_button_log = ""   # Arayüzde son basılan butonu göstermek
cal_status = ""        # Kalibrasyon durumu (seri port mesajlarından)
is_calibrating = False # Kalibrasyon devam ediyor mu?

# ==========================================
# PS4 KOLU BUTON HARITASI (pygame-ce / SDL2 - Windows)
# ==========================================
# Button 0  : X (Çarpı)      -> ACİL DURDURMA - Gaz ve tüm eksenleri sıfırlar
# Button 1  : O (Daire)       -> Kalibrasyon komutu gönder (C:1)
# Button 2  : □ (Kare)        -> Gaz %50 seviyesine ayarla
# Button 3  : △ (Üçgen)       -> Gaz yavaşça sıfırla (yumuşak iniş)
# Button 4  : Share           -> Tüm değerleri sıfırla (reset)
# Button 5  : PS Butonu       -> (Sistem butonu)
# Button 6  : Options         -> ARM / DISARM değiştir
# Button 7  : L3 (Sol çubuk)  -> Yaw sıfırla (düzleştir)
# Button 8  : R3 (Sağ çubuk)  -> Pitch ve Roll sıfırla (düzleştir)
# Button 9  : L1              -> Gaz -25 (hızlı düşür)
# Button 10 : R1              -> Gaz +25 (hızlı yükselt)
# Button 11 : D-pad Yukarı    -> Gaz +50 (boost)
# Button 12 : D-pad Aşağı     -> Gaz -50 (hızlı iniş)
# Button 13 : D-pad Sol       -> Yaw -20 (sola adım dön)
# Button 14 : D-pad Sağ       -> Yaw +20 (sağa adım dön)
# Button 15 : Touchpad        -> Gaz %75 seviyesine ayarla
#
# Axis 0: Sol Analog X  -> Yaw (Kendi etrafında dönme)
# Axis 1: Sol Analog Y  -> Throttle (Gaz) [Yukarı = -1, Aşağı = +1]
# Axis 2: Sağ Analog X  -> Roll (Sağa sola yatma)
# Axis 3: Sağ Analog Y  -> Pitch (İleri geri) [Yukarı = -1, Aşağı = +1]
# Axis 4: L2 Tetik      -> Ekstra yavaşlatma (basılıyken gaz hassasiyeti düşer)
# Axis 5: R2 Tetik      -> Ekstra hızlandırma (basılıyken gaz hassasiyeti artar)
# ==========================================

DEADZONE = 0.12

def apply_deadzone(value, dz=DEADZONE):
    if abs(value) < dz:
        return 0.0
    return value

def send_state():
    cmd = f"T:{state['T']},P:{state['P']},R:{state['R']},Y:{state['Y']},C:{state['C']}\n"
    if arduino is not None and arduino.is_open:
        try:
            arduino.write(cmd.encode('utf-8'))
        except Exception:
            pass

# ==========================================
# BUTON İŞLEYİCİLER
# ==========================================
def is_just_pressed(btn_id):
    """Butonun yeni mi basıldığını kontrol et (basılı tutmada tekrarlamaz)."""
    try:
        current = joystick.get_button(btn_id)
    except Exception:
        return False
    was_pressed = button_states.get(btn_id, False)
    button_states[btn_id] = bool(current)
    return current and not was_pressed

def handle_buttons():
    """Tüm butonları işle."""
    global armed, last_button_log

    if joystick is None:
        return

    num_buttons = joystick.get_numbuttons()

    # Button 0 - X (Çarpı): ACİL DURDURMA (basılı tutulduğu sürece aktif)
    try:
        if joystick.get_button(0):
            state['T'] = 0
            state['P'] = 0
            state['R'] = 0
            state['Y'] = 0
            state['C'] = 0
            last_button_log = "✕ ACİL DURDURMA!"
    except Exception:
        pass

    # Button 1 - O (Daire): Kalibrasyon
    global is_calibrating
    if num_buttons > 1 and is_just_pressed(1):
        state['C'] = 1
        last_button_log = "○ Kalibrasyon komutu gönderildi..."
        is_calibrating = True

    # Button 2 - □ (Kare): Gaz %50
    if num_buttons > 2 and is_just_pressed(2):
        state['T'] = 128
        last_button_log = "□ Gaz %50 (128)"

    # Button 3 - △ (Üçgen): Yumuşak iniş (gaz yavaşça düşer)
    try:
        if num_buttons > 3 and joystick.get_button(3):
            state['T'] = max(0, state['T'] - 3)
            last_button_log = "△ Yumuşak İniş..."
    except Exception:
        pass

    # Button 4 - Share: Tüm değerleri sıfırla
    if num_buttons > 4 and is_just_pressed(4):
        state['T'] = 0
        state['P'] = 0
        state['R'] = 0
        state['Y'] = 0
        state['C'] = 0
        last_button_log = "Share: Tam Sıfırlama"

    # Button 6 - Options: ARM/DISARM
    if num_buttons > 6 and is_just_pressed(6):
        armed = not armed
        last_button_log = f"Options: {'ARMED ✓' if armed else 'DISARMED ✗'}"

    # Button 7 - L3: Yaw sıfırla
    if num_buttons > 7 and is_just_pressed(7):
        state['Y'] = 0
        last_button_log = "L3: Yaw sıfırlandı"

    # Button 8 - R3: Pitch/Roll sıfırla
    if num_buttons > 8 and is_just_pressed(8):
        state['P'] = 0
        state['R'] = 0
        last_button_log = "R3: Pitch/Roll sıfırlandı"

    # Button 9 - L1: Gaz -25
    if num_buttons > 9 and is_just_pressed(9):
        state['T'] = max(0, state['T'] - 25)
        last_button_log = f"L1: Gaz -25 → {state['T']}"

    # Button 10 - R1: Gaz +25
    if num_buttons > 10 and is_just_pressed(10):
        state['T'] = min(255, state['T'] + 25)
        last_button_log = f"R1: Gaz +25 → {state['T']}"

    # Button 11 - D-pad Yukarı: Gaz +50 (boost)
    if num_buttons > 11 and is_just_pressed(11):
        state['T'] = min(255, state['T'] + 50)
        last_button_log = f"D↑: Gaz +50 → {state['T']}"

    # Button 12 - D-pad Aşağı: Gaz -50
    if num_buttons > 12 and is_just_pressed(12):
        state['T'] = max(0, state['T'] - 50)
        last_button_log = f"D↓: Gaz -50 → {state['T']}"

    # Button 13 - D-pad Sol: Yaw -20 adım
    if num_buttons > 13 and is_just_pressed(13):
        state['Y'] = max(-40, state['Y'] - 20)
        last_button_log = f"D←: Yaw -20 → {state['Y']}"

    # Button 14 - D-pad Sağ: Yaw +20 adım
    if num_buttons > 14 and is_just_pressed(14):
        state['Y'] = min(40, state['Y'] + 20)
        last_button_log = f"D→: Yaw +20 → {state['Y']}"

    # Button 15 - Touchpad: Gaz %75
    if num_buttons > 15 and is_just_pressed(15):
        state['T'] = 192
        last_button_log = "Touchpad: Gaz %75 (192)"

    # Kalibrasyon komutu bir kere gönderildikten sonra sıfırla
    if state['C'] == 1 and not (num_buttons > 1 and joystick.get_button(1)):
        state['C'] = 0

# ==========================================
# JOYSTICK OKUMA THREAD'İ
# ==========================================
joystick_running = True

def joystick_reader_thread():
    global joystick_running
    while joystick_running:
        try:
            pygame.event.pump()

            if joystick is not None:
                axis_lx = apply_deadzone(joystick.get_axis(0))
                axis_ly = apply_deadzone(joystick.get_axis(1))
                axis_rx = apply_deadzone(joystick.get_axis(2))
                axis_ry = apply_deadzone(joystick.get_axis(3))

                # L2/R2 tetik eksenleri (dinlenme = -1, tam basılı = +1)
                l2_value = 0.0
                r2_value = 0.0
                if joystick.get_numaxes() > 4:
                    l2_raw = joystick.get_axis(4)
                    l2_value = max(0, (l2_raw + 1) / 2)  # 0.0 - 1.0 arası normalize
                if joystick.get_numaxes() > 5:
                    r2_raw = joystick.get_axis(5)
                    r2_value = max(0, (r2_raw + 1) / 2)  # 0.0 - 1.0 arası normalize

                # --- GAZ (Throttle) ---
                # Hız çarpanı: R2 basılıyken 2x hızlı, L2 basılıyken 0.3x yavaş
                speed_mult = 1.0
                if r2_value > 0.1:
                    speed_mult = 1.0 + r2_value * 1.5  # Max 2.5x hız
                if l2_value > 0.1:
                    speed_mult = max(0.2, 1.0 - l2_value * 0.8)  # Min 0.2x yavaş

                effective_speed = THROTTLE_SPEED * speed_mult

                if axis_ly < 0:
                    state['T'] = min(255, state['T'] + int(abs(axis_ly) * effective_speed))
                elif axis_ly > 0:
                    state['T'] = max(0, state['T'] - int(abs(axis_ly) * effective_speed))

                # --- YAW (Kendi etrafında dönme) ---
                state['Y'] = int(axis_lx * 40)

                # --- ROLL (Sağa sola yatma) ---
                state['R'] = int(axis_rx * 40)

                # --- PITCH (İleri geri) ---
                state['P'] = int(-axis_ry * 40)

                # --- BUTONLAR ---
                handle_buttons()

        except Exception:
            pass

        time.sleep(0.025)  # ~40Hz okuma

reader_thread = threading.Thread(target=joystick_reader_thread, daemon=True)
reader_thread.start()

# ==========================================
# TELEMETRİ OKUMA THREAD'İ
# ==========================================
telemetry = {"P": 0.0, "R": 0.0, "Y": 0.0}
prev_cal_state = 0
serial_line_count = 0
tel_line_count = 0
last_serial_line = "(henüz veri yok)"

def serial_reader_thread():
    global telemetry, cal_status, is_calibrating, last_button_log, prev_cal_state
    global serial_line_count, tel_line_count, last_serial_line
    while joystick_running:
        if arduino and arduino.is_open:
            try:
                if arduino.in_waiting > 0:
                    raw = arduino.readline()
                    line = raw.decode('utf-8', errors='ignore').strip()
                    
                    if line:  # Boş satırları sayma
                        serial_line_count += 1
                        last_serial_line = line[:80]  # UI'da göstermek için
                    
                    # Telemetri verisi
                    # Eski format: TEL:P:12.3,R:-4.5,Y:0.0
                    # Yeni format: TEL:P:12.3,R:-4.5,Y:0.0,S:2
                    if line.startswith("TEL:"):
                        tel_line_count += 1
                        parts = line[4:].split(',')
                        cal_state = 0
                        for part in parts:
                            try:
                                if part.startswith("P:"):
                                    telemetry["P"] = float(part[2:])
                                elif part.startswith("R:"):
                                    telemetry["R"] = float(part[2:])
                                elif part.startswith("Y:"):
                                    telemetry["Y"] = float(part[2:])
                                elif part.startswith("S:"):
                                    cal_state = int(float(part[2:]))
                            except ValueError:
                                pass  # Tek alan bozuksa atla, diğerlerini oku
                        
                        # Kalibrasyon durumunu NRF üzerinden takip et
                        # 0=normal, 1=kalibre ediliyor, 2=kalibre edildi
                        if cal_state == 1:
                            if not is_calibrating:
                                print("[CAL] Kalibrasyon başladı!")
                            is_calibrating = True
                            cal_status = "⚙ KALİBRASYON DEVAM EDİYOR..."
                        elif cal_state == 2 and is_calibrating:
                            # Komut gönderdik ve drone kalibre edildi
                            is_calibrating = False
                            cal_status = "✅ KALİBRASYON TAMAM!"
                            last_button_log = "○ Kalibrasyon başarıyla tamamlandı!"
                            telemetry["P"] = 0.0
                            telemetry["R"] = 0.0
                            telemetry["Y"] = 0.0
                            print("[CAL] Kalibrasyon başarılı!")
                        elif cal_state == 2:
                            cal_status = "✅ Kalibre Edildi"
                        elif cal_state == 0 and is_calibrating:
                            # Butona bastık ama state 0 geldi (belki gaz açık olduğu için reddedildi)
                            # Ya da uzun süre geçti ama kalibrasyon bitmedi
                            # Geçici olarak hatayı gösterme, çünkü C:1 komutu birkaç frame geç gidebilir.
                            pass
                        elif cal_state == 0 and prev_cal_state == 1:
                            is_calibrating = False
                            cal_status = "❌ KALİBRASYON BAŞARISIZ!"
                            last_button_log = "⚠ Kalibrasyon başarısız!"
                            print("[CAL] Kalibrasyon başarısız!")
                        
                        prev_cal_state = cal_state
            except Exception as e:
                print(f"[SERIAL HATA] {e}")
        time.sleep(0.01)

serial_thread = threading.Thread(target=serial_reader_thread, daemon=True)
serial_thread.start()

# ==========================================
# TKINTER ARAYÜZ (GUI)
# ==========================================
root = tk.Tk()
root.title("Micro Drone - PS4 Kontrol İstasyonu")
root.geometry("600x900")
root.configure(bg="#0a0a12")
try:
    root.state('zoomed')  # Pencereyi tam ekran (maximized) yap
except Exception:
    root.attributes('-fullscreen', True) # Linux/Mac alternatifi

root.resizable(False, False)

# Başlık
tk.Label(root, text="🎮 DRONE PS4 KONTROLÜ",
         fg="#e94560", bg="#0f0f1a",
         font=("Arial", 18, "bold")).pack(pady=(12, 4))

# --- Durum Çubuğu ---
frame_status = tk.Frame(root, bg="#0f0f1a")
frame_status.pack(fill="x", padx=15, pady=4)

joystick_status_text = f"🎮 {joystick_name}" if joystick else "🎮 KOL BAĞLI DEĞİL!"
joystick_color = "#16213e" if joystick else "#8b0000"
lbl_joystick = tk.Label(frame_status, text=joystick_status_text,
                         fg="white", bg=joystick_color,
                         font=("Arial", 9, "bold"), padx=8, pady=2)
lbl_joystick.pack(side="left", expand=True, fill="x", padx=(0, 3))

arduino_status_text = f"📡 {PORT} BAĞLI" if arduino else f"📡 {PORT} YOK"
arduino_color = "#16213e" if arduino else "#8b0000"
lbl_arduino = tk.Label(frame_status, text=arduino_status_text,
                        fg="white", bg=arduino_color,
                        font=("Arial", 9, "bold"), padx=8, pady=2)
lbl_arduino.pack(side="left", expand=True, fill="x", padx=(3, 0))

# ARM durumu
lbl_arm = tk.Label(root, text="DISARMED", fg="#e74c3c", bg="#0f0f1a",
                   font=("Arial", 14, "bold"))
lbl_arm.pack(pady=3)

# Acil durdurma
tk.Label(root, text="⚠ ACİL DURDURMA: ✕ (X) TUŞU ⚠",
         fg="#e74c3c", bg="#0f0f1a",
         font=("Arial", 10, "bold")).pack(pady=2)

# Separator
tk.Frame(root, bg="#1a1a3e", height=2).pack(fill="x", padx=15, pady=5)

# --- Değerler ---
frame_vals = tk.Frame(root, bg="#0f0f1a")
frame_vals.pack(pady=3, fill="x", padx=25)

def make_value_row(parent, label_text, color, row):
    lbl_name = tk.Label(parent, text=label_text, fg="#888", bg="#0f0f1a",
                        font=("Consolas", 11), anchor="w")
    lbl_name.grid(row=row, column=0, sticky="w", pady=3)
    lbl_val = tk.Label(parent, text="0", fg=color, bg="#0f0f1a",
                       font=("Consolas", 18, "bold"), anchor="e", width=5)
    lbl_val.grid(row=row, column=1, sticky="e", pady=3)

    # Basit progress bar
    bar_frame = tk.Frame(parent, bg="#1a1a2e", height=6, width=150)
    bar_frame.grid(row=row, column=2, sticky="w", padx=(10, 0), pady=3)
    bar_frame.grid_propagate(False)
    bar_fill = tk.Frame(bar_frame, bg=color, height=6, width=0)
    bar_fill.place(x=0, y=0, height=6)

    return lbl_val, bar_fill

lbl_throttle, bar_throttle = make_value_row(frame_vals, "GAZ", "#2ecc71", 0)
lbl_pitch, bar_pitch       = make_value_row(frame_vals, "PITCH", "#3498db", 1)
lbl_roll, bar_roll         = make_value_row(frame_vals, "ROLL", "#3498db", 2)
lbl_yaw, bar_yaw           = make_value_row(frame_vals, "YAW", "#f1c40f", 3)

frame_vals.columnconfigure(0, weight=1)
frame_vals.columnconfigure(1, weight=0)
frame_vals.columnconfigure(2, weight=0)

# Separator
tk.Frame(root, bg="#1a1a3e", height=2).pack(fill="x", padx=15, pady=5)

# --- Buton Haritası ---
tk.Label(root, text="BUTON HARİTASI", fg="#666", bg="#0f0f1a",
         font=("Arial", 10, "bold")).pack(pady=(3, 2))

frame_btns = tk.Frame(root, bg="#0f0f1a")
frame_btns.pack(pady=2, padx=15)

btn_labels_left = [
    ("✕  Acil Durdurma", "#e74c3c"),
    ("○  Kalibrasyon", "#f39c12"),
    ("□  Gaz %50", "#3498db"),
    ("△  Yumuşak İniş", "#2ecc71"),
    ("L1  Gaz -25", "#e67e22"),
    ("R1  Gaz +25", "#27ae60"),
]
btn_labels_right = [
    ("D↑  Gaz +50", "#27ae60"),
    ("D↓  Gaz -50", "#e67e22"),
    ("D←  Yaw Sol", "#9b59b6"),
    ("D→  Yaw Sağ", "#9b59b6"),
    ("Share  Sıfırla", "#95a5a6"),
    ("Options  ARM", "#1abc9c"),
]

for i, (txt, clr) in enumerate(btn_labels_left):
    tk.Label(frame_btns, text=txt, fg=clr, bg="#0f0f1a",
             font=("Consolas", 8), anchor="w").grid(row=i, column=0, sticky="w", padx=(0, 15), pady=1)

for i, (txt, clr) in enumerate(btn_labels_right):
    tk.Label(frame_btns, text=txt, fg=clr, bg="#0f0f1a",
             font=("Consolas", 8), anchor="w").grid(row=i, column=1, sticky="w", padx=(15, 0), pady=1)

# Separator
tk.Frame(root, bg="#1a1a3e", height=2).pack(fill="x", padx=15, pady=5)

# Son basılan buton / log
lbl_log = tk.Label(root, text="Bekleniyor...", fg="#555", bg="#0f0f1a",
                   font=("Consolas", 10))
lbl_log.pack(pady=2)

# Kalibrasyon durum etiketi
lbl_cal_status = tk.Label(root, text="", fg="#f39c12", bg="#0f0f1a",
                          font=("Arial", 10, "bold"))
lbl_cal_status.pack(pady=1)

# Tetik göstergesi
lbl_triggers = tk.Label(root, text="L2: 0%  |  R2: 0%", fg="#444", bg="#0f0f1a",
                        font=("Consolas", 9))
lbl_triggers.pack(pady=2)

# Ham eksen değerleri
lbl_raw = tk.Label(root, text="Ham: ---", fg="#333", bg="#0f0f1a",
                   font=("Consolas", 8))
lbl_raw.pack(pady=2)

# --- SERI PORT DEBUG ---
tk.Frame(root, bg="#e74c3c", height=1).pack(fill="x", padx=15, pady=3)
lbl_serial_debug = tk.Label(root, text="SERI PORT: Bekleniyor...", fg="#555", bg="#0f0f1a",
                            font=("Consolas", 8))
lbl_serial_debug.pack(pady=1)
lbl_serial_last = tk.Label(root, text="", fg="#444", bg="#0f0f1a",
                           font=("Consolas", 7), wraplength=500)
lbl_serial_last.pack(pady=1)

# --- Yapay Ufuk (Neon Drone View) ---
tk.Frame(root, bg="#1a1a3e", height=2).pack(fill="x", padx=15, pady=5)
lbl_telemetry = tk.Label(root, text="DRONE CANLI TELEMETRİ", fg="#00f2fe", bg="#0f0f1a",
                         font=("Arial", 12, "bold"))
lbl_telemetry.pack(pady=(0, 2))

attitude_canvas = tk.Canvas(root, width=600, height=300, bg="#0a0a12", highlightthickness=2, highlightbackground="#1f1f33")
attitude_canvas.pack(pady=5)

def draw_attitude(p, r):
    attitude_canvas.delete("all")
    cx, cy = 300, 110
    
    # 1. Şık Arka Plan Grid
    for i in range(0, 600, 40):
        attitude_canvas.create_line(i, 0, i, 300, fill="#141424", dash=(2,2))
    for i in range(0, 300, 40):
        attitude_canvas.create_line(0, i, 600, i, fill="#141424", dash=(2,2))
        
    # 2. Pitch Skalası (Arka Planda)
    for i in range(-60, 70, 20):
        y_pos = cy + i * 1.5
        if 0 < y_pos < 300:
            attitude_canvas.create_line(cx - 30, y_pos, cx + 30, y_pos, fill="#33334d")
            attitude_canvas.create_text(cx + 45, y_pos, text=str(-i), fill="#555577", font=("Arial", 8))
            
    # 3. Sabit Hedef Artı (Merkez)
    attitude_canvas.create_line(cx - 30, cy, cx + 30, cy, fill="#e74c3c", width=1, dash=(4,2))
    attitude_canvas.create_line(cx, cy - 30, cx, cy + 30, fill="#e74c3c", width=1, dash=(4,2))
    
    # 4. Dönüş Mantığı
    angle = math.radians(r)
    pitch_offset = p * 1.5 
    
    def rotate_point(x, y):
        rx = x * math.cos(angle) - y * math.sin(angle)
        ry = x * math.sin(angle) + y * math.cos(angle)
        return rx + cx, ry + cy + pitch_offset
        
    # 5. Tam Drone Çizimi (X Frame Quadcopter)
    arm_l = 100  # Kol uzunluğu
    arm_w = 50   # Kolların genişlik/açı faktörü
    
    # Motor pozisyonları (x, y) - Drone'a arkadan bakıyoruz
    motors = [
        (-arm_l, -arm_w), # Sol Ön
        (arm_l, -arm_w),  # Sağ Ön
        (-arm_l, arm_w),  # Sol Arka
        (arm_l, arm_w)    # Sağ Arka
    ]
    
    # Kolların çizimi
    for mx, my in motors:
        p1 = rotate_point(0, 0)
        p2 = rotate_point(mx, my)
        # Ön kollar daha koyu, arka kollar daha parlak (derinlik hissi)
        color = "#0088aa" if my < 0 else "#00f2fe"
        attitude_canvas.create_line(p1[0], p1[1], p2[0], p2[1], fill=color, width=6, capstyle=tk.ROUND)
        
    # Pervanelerin çizimi
    for i, (mx, my) in enumerate(motors):
        center = rotate_point(mx, my)
        # Pitch'e göre pervanenin yüksekliğini basıklaştıralım (perspektif)
        prop_w = 45 
        prop_h = 12 + abs(p) * 0.3 
        
        # Pervane dönüş dairesi
        color = "#f39c12" if my < 0 else "#2ecc71" # Önler turuncu, arkalar yeşil
        attitude_canvas.create_oval(center[0]-prop_w, center[1]-prop_h, 
                                    center[0]+prop_w, center[1]+prop_h, 
                                    outline=color, width=3)
        
        # Motor göbeği
        attitude_canvas.create_oval(center[0]-5, center[1]-5, 
                                    center[0]+5, center[1]+5, 
                                    fill="#fff", outline="")
    
    # Merkez Gövde
    center = rotate_point(0, 0)
    attitude_canvas.create_oval(center[0]-20, center[1]-20, 
                                center[0]+20, center[1]+20, 
                                fill="#2c3e50", outline="#ecf0f1", width=2)
    # Yön göstergesi (Kafa)
    head = rotate_point(0, -25)
    attitude_canvas.create_polygon(center[0], center[1]-10, 
                                   head[0]-8, head[1], 
                                   head[0]+8, head[1], 
                                   fill="#e74c3c")



# ==========================================
# UI GÜNCELLEME DÖNGÜSÜ
# ==========================================
def update_bar(bar_widget, value, max_val, is_centered=False):
    """Progress bar güncelle."""
    if is_centered:
        # -40 ile +40 arası: ortadan iki yöne
        ratio = abs(value) / max_val
        width = int(ratio * 75)
        bar_widget.place(x=75, y=0, height=6, width=width)
    else:
        ratio = value / max_val
        width = int(ratio * 150)
        bar_widget.place(x=0, y=0, height=6, width=max(0, width))

def update_ui():
    lbl_throttle.config(text=str(state['T']))
    lbl_pitch.config(text=str(state['P']))
    lbl_roll.config(text=str(state['R']))
    lbl_yaw.config(text=str(state['Y']))

    # Bar'ları güncelle
    update_bar(bar_throttle, state['T'], 255)
    update_bar(bar_pitch, state['P'], 40, True)
    update_bar(bar_roll, state['R'], 40, True)
    update_bar(bar_yaw, state['Y'], 40, True)

    # ARM durumu
    if armed:
        lbl_arm.config(text="🟢 ARMED", fg="#2ecc71")
    else:
        lbl_arm.config(text="🔴 DISARMED", fg="#e74c3c")

    # Son buton logu
    if last_button_log:
        lbl_log.config(text=last_button_log, fg="#aaa")

    # Kalibrasyon durum göstergesi
    if is_calibrating:
        lbl_cal_status.config(text="⚙ KALİBRASYON DEVAM EDİYOR...", fg="#f39c12")
    elif cal_status:
        color = "#2ecc71" if "TAMAM" in cal_status else ("#e74c3c" if "BAŞARISIZ" in cal_status else "#f39c12")
        lbl_cal_status.config(text=cal_status, fg=color)
    else:
        lbl_cal_status.config(text="")

    # Tetik değerleri
    if joystick is not None and joystick.get_numaxes() > 5:
        try:
            l2_raw = joystick.get_axis(4)
            r2_raw = joystick.get_axis(5)
            l2_pct = int(max(0, (l2_raw + 1) / 2) * 100)
            r2_pct = int(max(0, (r2_raw + 1) / 2) * 100)
            lbl_triggers.config(text=f"L2: {l2_pct}%  |  R2: {r2_pct}%")
        except Exception:
            pass

    # Ham eksen
    if joystick is not None:
        try:
            axes = [round(joystick.get_axis(i), 2) for i in range(min(6, joystick.get_numaxes()))]
            lbl_raw.config(text=f"Ham: {axes}")
        except Exception:
            pass

    # Ufuk Çizgisini Güncelle
    p = telemetry["P"]
    r = telemetry["R"]
    y_angle = telemetry["Y"]
    lbl_telemetry.config(text=f"PITCH: {p:.1f}°  |  ROLL: {r:.1f}°  |  YAW: {y_angle:.1f}°")
    
    draw_attitude(p, r)

    # Seri port debug göstergesi
    if arduino is None:
        lbl_serial_debug.config(text="SERI PORT: BAĞLI DEĞİL!", fg="#e74c3c")
    else:
        color = "#2ecc71" if tel_line_count > 0 else ("#f39c12" if serial_line_count > 0 else "#e74c3c")
        lbl_serial_debug.config(
            text=f"SERI: {serial_line_count} satır | TEL: {tel_line_count} telemetri",
            fg=color
        )
        lbl_serial_last.config(text=f"Son: {last_serial_line}", fg="#555")

    root.after(50, update_ui)

def loop_send():
    send_state()
    root.after(50, loop_send)

# Klavye yedek kontrol
def key_press(event):
    key = event.keysym.lower()
    if key == 'w': state['T'] = min(255, state['T'] + 10)
    elif key == 's': state['T'] = max(0, state['T'] - 10)
    elif key == 'space':
        state['T'] = 0; state['P'] = 0; state['R'] = 0; state['Y'] = 0
    elif key == 'up': state['P'] = 40
    elif key == 'down': state['P'] = -40
    elif key == 'left': state['R'] = -40
    elif key == 'right': state['R'] = 40
    elif key == 'a': state['Y'] = -40
    elif key == 'd': state['Y'] = 40

def key_release(event):
    key = event.keysym.lower()
    if key in ['up', 'down']: state['P'] = 0
    if key in ['left', 'right']: state['R'] = 0
    if key in ['a', 'd']: state['Y'] = 0

root.bind('<KeyPress>', key_press)
root.bind('<KeyRelease>', key_release)

# Döngüleri başlat
update_ui()
loop_send()

def on_close():
    global joystick_running
    joystick_running = False
    if arduino and arduino.is_open:
        arduino.close()
    pygame.quit()
    root.destroy()

root.protocol("WM_DELETE_WINDOW", on_close)
root.mainloop()
