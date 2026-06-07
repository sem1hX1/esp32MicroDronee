import pygame
import time

print("=" * 50)
print("JOYSTICK TEST SCRIPTI")
print("=" * 50)
print(f"pygame versiyonu: {pygame.ver}")
print()

pygame.init()
pygame.joystick.init()

count = pygame.joystick.get_count()
print(f"Algilanan joystick sayisi: {count}")

if count == 0:
    print("\nHicbir joystick bulunamadi!")
    print("Lütfen sunlari kontrol edin:")
    print("  1. PS4 kolu USB ile baglanmis mi?")
    print("  2. Windows Ayarlari > Bluetooth > bagli cihazlar'da görünüyor mu?")
    print("  3. DS4Windows gibi bir program kullaniyorsaniz, onu kapatin ve tekrar deneyin.")
    print("\n5 saniye bekleniyor, kolu simdi takin...")
    time.sleep(5)
    pygame.joystick.quit()
    pygame.joystick.init()
    count = pygame.joystick.get_count()
    print(f"\nTekrar kontrol: Algilanan joystick sayisi: {count}")

for i in range(count):
    js = pygame.joystick.Joystick(i)
    js.init()
    print(f"\n--- Joystick {i} ---")
    print(f"  Adi: {js.get_name()}")
    try:
        print(f"  GUID: {js.get_guid()}")
    except:
        pass
    print(f"  Eksen sayisi: {js.get_numaxes()}")
    print(f"  Buton sayisi: {js.get_numbuttons()}")
    print(f"  Hat sayisi: {js.get_numhats()}")

if count > 0:
    print("\n\nEksen ve buton degerlerini okuyorum (10 saniye)...")
    print("Kolu hareket ettirin ve butonlara basin!\n")
    js = pygame.joystick.Joystick(0)
    js.init()
    
    start = time.time()
    while time.time() - start < 10:
        pygame.event.pump()
        axes = [round(js.get_axis(a), 2) for a in range(js.get_numaxes())]
        buttons = [js.get_button(b) for b in range(js.get_numbuttons())]
        print(f"  Eksenler: {axes}  |  Butonlar: {buttons}", end="\r")
        time.sleep(0.1)
    print("\n\nTest bitti!")

input("\nKapatmak icin Enter'a basin...")
