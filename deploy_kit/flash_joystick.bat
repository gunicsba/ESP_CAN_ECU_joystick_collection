@echo off
REM Flash unified joystick ECU via USB. Usage: flash_joystick.bat [COM port] (default COM7)
set PORT=%1
if "%PORT%"=="" set PORT=COM7
cd /d %~dp0
esptool.exe --chip esp32s3 --port %PORT% --baud 460800 write_flash -z 0x0000 joystick\bootloader.bin 0x8000 joystick\partitions.bin 0x10000 joystick\firmware.bin
pause
