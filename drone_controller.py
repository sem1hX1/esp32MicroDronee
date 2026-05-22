import pygame
import serial
import serial.tools.list_ports
import time
import sys

def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    # Önce genel USB-Serial çiplerini ara (ESP32 genellikle bunlardan biridir)
    for port in ports:
        desc = port.description.upper()
        if "USB" in desc or "CH340" in desc or "CP210" in desc or "SERIAL" in desc or "JTAG" in desc:
            return port.device
    # Bulamazsa ilk bulduğu portu ver
    if ports:
        return ports[0].device
    return None

def main():
    print("="*50)
    print("   NEXT-GEN DRONE - GAMEPAD KONTROLCÜSÜ   ")
    print("="*50)
    print("   Web Panel de aktif: http://192.168.4.1")
    print("   WiFi: DRONE-NEXTGEN / drone123")
    print("="*50)
    
    # 1. Seri Port Bağlantısı
    port_name = find_esp32_port()
    if not port_name:
        print("HATA: ESP32 bulunamadı! USB kablosunu kontrol edin.")
        input("Çıkmak için Enter'a basın...")
        return
        
    print(f"[*] ESP32 {port_name} portunda bulundu. Bağlanılıyor...")
    try:
        ser = serial.Serial(port_name, 115200, timeout=0.1)
        print("[*] Seri port bağlantısı BAŞARILI!")
    except Exception as e:
        print(f"HATA: Port açılamadı! Arduino IDE Serial Monitor açıksa KAPATIN.\nDetay: {e}")
        input("Çıkmak için Enter'a basın...")
        return

    # 2. Gamepad Bağlantısı
    pygame.init()
    pygame.joystick.init()
    
    if pygame.joystick.get_count() == 0:
        print("HATA: Gamepad (Oyun Kolu) bulunamadı! Lütfen bağlayıp tekrar açın.")
        input("Çıkmak için Enter'a basın...")
        return
        
    joystick = pygame.joystick.Joystick(0)
    joystick.init()
    print(f"[*] Gamepad Bulundu: {joystick.get_name()}")
    
    print("\n--- KONTROLLER ---")
    print("Sol Çubuk (Yukarı/Aşağı)   : Gaz (Throttle)")
    print("Sol Çubuk (Sağ/Sol)        : Yaw (Kendi etrafında dönme)")
    print("Sağ Çubuk (Yukarı/Aşağı)   : Pitch (İleri/Geri)")
    print("Sağ Çubuk (Sağ/Sol)        : Roll (Sağa/Sola yatma)")
    print("A Butonu (veya Çapraz)     : Motorları KİLİTLE / AÇ (Arm/Disarm)")
    print("\nÇıkmak için pencereyi kapatın veya CTRL+C yapın.\n")

    armed = 0
    button_pressed_last = False
    
    clock = pygame.time.Clock()

    try:
        while True:
            pygame.event.pump()
            
            # --- ARM / DISARM Butonu Kontrolü (Genelde Buton 0 = A tuşu) ---
            button_pressed = joystick.get_button(0)
            if button_pressed and not button_pressed_last:
                armed = 1 if armed == 0 else 0
                state = "AKTİF (Motorlar Dönebilir!)" if armed else "KİLİTLİ"
                print(f">>> DURUM DEĞİŞTİ: {state}")
            button_pressed_last = button_pressed

            # --- Eksenleri (Joystick) Oku ---
            # Pygame'de eksenler -1.0 ile 1.0 arası değer verir.
            # Yukarı itmek genelde negatif (-1.0), aşağı çekmek pozitif (1.0) değer verir.
            
            # Sol Çubuk Y (Gaz) -> Ekseni ters çevirip 0-255 arasına map ediyoruz
            axis_throttle = -joystick.get_axis(1) 
            throttle = int((axis_throttle + 1.0) / 2.0 * 255.0)
            if throttle < 5: throttle = 0
            if throttle > 255: throttle = 255

            # Sol Çubuk X (Yaw) -> -40 ile +40 arası
            axis_yaw = joystick.get_axis(0)
            yaw = int(axis_yaw * 40.0)

            # Sağ Çubuk Y (Pitch) -> -40 ile +40 arası
            axis_pitch = -joystick.get_axis(3)
            pitch = int(axis_pitch * 40.0)

            # Sağ Çubuk X (Roll) -> -40 ile +40 arası
            axis_roll = joystick.get_axis(2)
            roll = int(axis_roll * 40.0)

            # Deadzone (Ufak titremeleri yok say)
            if abs(yaw) < 5: yaw = 0
            if abs(pitch) < 5: pitch = 0
            if abs(roll) < 5: roll = 0

            # Kilitliyse değerleri sıfırla
            if not armed:
                throttle = 0
                yaw = 0
                pitch = 0
                roll = 0

            # --- Veriyi ESP32'ye Gönder ---
            # Format: T:255 P:0 R:0 Y:0 A:1
            command = f"T:{throttle} P:{pitch} R:{roll} Y:{yaw} A:{armed}\n"
            ser.write(command.encode('utf-8'))

            # Ekrana durumu yaz (Aynı satırda güncellenir)
            sys.stdout.write(f"\rGaz: {throttle:3} | Pitch: {pitch:3} | Roll: {roll:3} | Yaw: {yaw:3} | Durum: {'AKTIF' if armed else 'KILIT'}    ")
            sys.stdout.flush()

            # Saniyede 50 kere gönder (20ms gecikme)
            clock.tick(50)

    except KeyboardInterrupt:
        print("\nÇıkış yapılıyor...")
    finally:
        # Çıkarken motorları durdurmak için son bir sıfırlama komutu gönder
        try:
            ser.write(b"T:0 P:0 R:0 Y:0 A:0\n")
            ser.close()
        except:
            pass
        pygame.quit()

if __name__ == "__main__":
    main()
