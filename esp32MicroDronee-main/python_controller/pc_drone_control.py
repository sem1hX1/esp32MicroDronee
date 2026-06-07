import serial
import time
import tkinter as tk

# ==========================================
# AYARLAR
# ==========================================
PORT = "COM7" # <-- BURAYI ARDUINO'NUN PORTU ILE DEGISTIRIN
BAUD = 115200

try:
    print(f"Arduino'ya baglaniliyor ({PORT})...")
    arduino = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(2) # Arduino'nun resetlenmesini bekle
    print("Baglanti Basarili!")
except Exception as e:
    print(f"HATA: {PORT} portu acilamadi! Lutfen portu guncelleyin. {e}")
    arduino = None # Hata durumunda arduino'yu None yap ve cikma, arayuz acilsin.

# Drone Durumu
state = {"T": 0, "P": 0, "R": 0, "Y": 0, "C": 0}

def send_state():
    # Durumu Arduino'ya gönder
    cmd = f"T:{state['T']},P:{state['P']},R:{state['R']},Y:{state['Y']},C:{state['C']}\n"
    if arduino is not None and arduino.is_open:
        arduino.write(cmd.encode('utf-8'))

def key_press(event):
    key = event.keysym.lower()
    # Gaz Kontrolü (W / S)
    if key == 'w': state['T'] = min(255, state['T'] + 5)
    elif key == 's': state['T'] = max(0, state['T'] - 5)
    elif key == 'space': state['T'] = 0 # ACIL DURDURMA!
    
    # İleri/Geri (Yön Tuşları)
    elif key == 'up': state['P'] = 40
    elif key == 'down': state['P'] = -40
    
    # Sağa/Sola Yatırma (Yön Tuşları)
    elif key == 'left': state['R'] = -40
    elif key == 'right': state['R'] = 40
    
    # Kendi Ekseninde Dönme (A / D)
    elif key == 'a': state['Y'] = -40
    elif key == 'd': state['Y'] = 40
    
    update_ui()

def key_release(event):
    key = event.keysym.lower()
    # Tuşu bırakınca kollar merkeze (sıfıra) döner (Gaz hariç)
    if key in ['up', 'down']: state['P'] = 0
    if key in ['left', 'right']: state['R'] = 0
    if key in ['a', 'd']: state['Y'] = 0
    update_ui()

def loop_send():
    send_state()
    # Saniyede 20 kere (50ms) aralıksız gönder (Failsafe için)
    root.after(50, loop_send)

# Arayüz (UI) Güncelleme
def update_ui():
    lbl_throttle.config(text=f"Gaz (W/S): {state['T']}")
    lbl_pitch.config(text=f"İleri/Geri (Yukarı/Aşağı): {state['P']}")
    lbl_roll.config(text=f"Sağ/Sol (Sol/Sağ): {state['R']}")
    lbl_yaw.config(text=f"Kendi Etrafında Dönme (A/D): {state['Y']}")

# Tkinter Pencere Kurulumu
root = tk.Tk()
root.title("Micro Drone Kontrol İstasyonu")
root.geometry("400x300")
root.configure(bg="#2c3e50")

tk.Label(root, text="DRONE KLAVYE KONTROLÜ", fg="white", bg="#2c3e50", font=("Arial", 16, "bold")).pack(pady=10)
tk.Label(root, text="Acil Durdurma: BOSLUK (Space) Tusu", fg="#e74c3c", bg="#2c3e50", font=("Arial", 10, "bold")).pack(pady=5)

lbl_throttle = tk.Label(root, fg="#2ecc71", bg="#2c3e50", font=("Arial", 14))
lbl_throttle.pack(pady=5)
lbl_pitch = tk.Label(root, fg="#3498db", bg="#2c3e50", font=("Arial", 14))
lbl_pitch.pack(pady=5)
lbl_roll = tk.Label(root, fg="#3498db", bg="#2c3e50", font=("Arial", 14))
lbl_roll.pack(pady=5)
lbl_yaw = tk.Label(root, fg="#f1c40f", bg="#2c3e50", font=("Arial", 14))
lbl_yaw.pack(pady=5)

update_ui()

root.bind('<KeyPress>', key_press)
root.bind('<KeyRelease>', key_release)

# Arka plan döngüsünü başlat
root.after(50, loop_send)
root.mainloop()
