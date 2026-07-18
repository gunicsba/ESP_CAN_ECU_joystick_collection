import serial
import serial.tools.list_ports
import time
import sys
import os

sys.path.insert(0, r"C:\Users\Balazs\.platformio\packages\tool-esptoolpy")
import esptool

BUILD_DIR = r"d:\AgOpenGPS\ESP_CAN_ECU_joystick_collection\.pio\build\motor_driver_s3_ota"
PORT = "COM7"
BAUD = "115200"

BOOT_APP0 = r"C:\Users\Balazs\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"

print("CDC flash: erase first, then write - retry loop")

attempt = 0
while True:
    attempt += 1
    ports = serial.tools.list_ports.comports()
    port_names = [p.device for p in ports]
    
    if PORT in port_names:
        print(f"\n*** COM7 detected on attempt {attempt}! Erasing flash first... ***")
        try:
            # Step 1: Erase entire flash to stop any boot loop
            esptool.main([
                '--chip', 'esp32s3',
                '--port', PORT,
                '--baud', BAUD,
                '--before', 'default_reset',
                '--after', 'no_reset',
                'erase_flash',
            ])
            print("\n*** Flash erased! Waiting 2s before write... ***")
            time.sleep(2)
            
            # Step 2: Write firmware (device should be in download mode, no boot loop)
            esptool.main([
                '--chip', 'esp32s3',
                '--port', PORT,
                '--baud', BAUD,
                '--before', 'default_reset',
                '--after', 'hard_reset',
                'write_flash', '-z',
                '--flash_mode', 'dio',
                '--flash_freq', '80m',
                '--flash_size', '8MB',
                '0x0000', os.path.join(BUILD_DIR, "bootloader.bin"),
                '0x8000', os.path.join(BUILD_DIR, "partitions.bin"),
                '0xe000', BOOT_APP0,
                '0x10000', os.path.join(BUILD_DIR, "firmware.bin"),
            ])
            print("\n*** FLASH SUCCESSFUL! ***")
            break
        except SystemExit:
            print("\n*** Flash completed (with exit) ***")
            break
        except Exception as e:
            print(f"\n*** Flash error: {e}, retrying... ***")
            time.sleep(1)
    else:
        if attempt % 20 == 0:
            print(f"  Waiting... ({attempt} attempts)")
        time.sleep(0.05)
