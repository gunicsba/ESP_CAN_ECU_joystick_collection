@echo off
REM Flash motor driver ECU via USB. Usage: flash_motor.bat [COM port] (default COM6)
set PORT=%1
if "%PORT%"=="" set PORT=COM6
cd /d %~dp0
esptool.exe --chip esp32s3 --port %PORT% --baud 460800 write_flash -z 0x0000 motor_driver\bootloader.bin 0x8000 motor_driver\partitions.bin 0x10000 motor_driver\firmware.bin
pause
