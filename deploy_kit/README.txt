FORWARDER CAN CONTROLLER - FIELD DEPLOYMENT KIT (2026-09-03)
=============================================================

CONTENTS
  motor_driver\firmware.bin     Motor driver ECU app (for OTA or USB)
  motor_driver\bootloader.bin   Bootloader (USB flash only)
  motor_driver\partitions.bin   Partition table (USB flash only)
  joystick\...                  Same three files for joystick ECU (USB only - it has no WiFi)
  esptool.exe                   USB flashing tool
  flash_motor.bat               One-click USB flash for motor driver
  flash_joystick.bat            One-click USB flash for joystick

---------------------------------------------------------------------
1) OTA UPDATE - MOTOR DRIVER (preferred, keeps all config in NVS)
---------------------------------------------------------------------
  a. Connect phone/laptop to WiFi:  forwarder-motor-20   password: 12345678
  b. Open browser: http://192.168.4.1
  c. Go to the Update / OTA page, upload ONLY:
        motor_driver\firmware.bin
     (no bootloader.bin, no partitions.bin)
  d. Wait for "OK" - board reboots itself after ~1 s.
  e. Close other browser tabs to the board during upload (limited buffers).
  NOTE: OTA writes only the app partition - button mappings, axis config
        and labels in NVS are NOT touched.

---------------------------------------------------------------------
2) USB FALLBACK - IF OTA FAILS OR BOARD WON'T BOOT
---------------------------------------------------------------------
  Find the COM port first (PowerShell):
      Get-CimInstance Win32_SerialPort | Select DeviceID, Description
  (At home: motor driver = COM6, joystick = COM7. At the customer site
   the ports may be different!)

  Then either run the one-click script:
      flash_motor.bat COM6
      flash_joystick.bat COM7

  ...or the raw commands (from this folder):
      esptool.exe --chip esp32s3 --port COM6 --baud 460800 write_flash -z ^
        0x0000 motor_driver\bootloader.bin ^
        0x8000 motor_driver\partitions.bin ^
        0x10000 motor_driver\firmware.bin

      esptool.exe --chip esp32s3 --port COM7 --baud 460800 write_flash -z ^
        0x0000 joystick\bootloader.bin ^
        0x8000 joystick\partitions.bin ^
        0x10000 joystick\firmware.bin

  If esptool can't open the port: close any serial monitor holding it.
  If the board doesn't enter download mode: hold BOOT, tap RESET, release BOOT.
  NEVER run erase_flash - it wipes the NVS config partition!

---------------------------------------------------------------------
3) COMPILE AT THE CUSTOMER SITE
---------------------------------------------------------------------
  Requires: the full project folder AND PlatformIO installed on the laptop.
  PlatformIO needs internet access the first time per machine (toolchain
  download) - if unsure, build once at home on that laptop before leaving,
  then compilation works offline at the customer.

  From the project root (d:\AgOpenGPS\ESP_CAN_ECU_joystick_collection):

      Build motor driver:
        C:\Users\Balazs\.platformio\penv\Scripts\pio.exe run -e motor_driver_s3_ota

      Build joystick:
        C:\Users\Balazs\.platformio\penv\Scripts\pio.exe run -e joystick_full

      Build + flash in one step (auto-detects port from platformio.ini):
        pio.exe run -e motor_driver_s3_ota --target upload

      Fresh build output lands in:
        .pio\build\motor_driver_s3_ota\   (firmware.bin, bootloader.bin, partitions.bin)
        .pio\build\joystick_full\

---------------------------------------------------------------------
4) ON-SITE ACCESS REFERENCE
---------------------------------------------------------------------
  Motor driver WiFi AP:  forwarder-motor-20 / 12345678 -> http://192.168.4.1
  Motor driver Ethernet: static IP 192.168.5.40 (UDP 8888/9999)
  CAN addresses:         motor driver = 0x20, unified joystick = 0x80
  Watchdogs:             8 s task watchdog (auto-reboot on hang),
                         500 ms CAN safety timeout (outputs zero on bus loss)
  Joystick ECU:          CAN-only, no WiFi, no OTA - USB cable needed
