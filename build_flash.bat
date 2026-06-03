@echo off
taskkill /F /IM platformio.exe 2>nul
timeout /t 2 /nobreak >nul
cd /d c:\AgOpenGPS\forwarderke
platformio run -e motor_driver_s3_ota -t upload --upload-port COM18
echo.
echo === BUILD COMPLETE ===
