@echo off
setlocal

:: Default port COM10 (configured in platformio.ini), or allow passing COM port as argument: upload.bat COM5
set PORT=%1
if "%PORT%"=="" set PORT=COM10

echo ========================================================
echo Flashing precompiled firmware to %PORT% (No compile)
echo ========================================================

"C:\Users\admin\.platformio\penv\Scripts\python.exe" "C:\Users\admin\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port %PORT% --baud 1500000 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB 0x0 "%~dp0firmware_merged.bin"

if %ERRORLEVEL% equ 0 (
    echo.
    echo ========================================================
    echo Flash SUCCESS! Robot will reboot now.
    echo ========================================================
) else (
    echo.
    echo [ERROR] Flash failed! Check that COM port is correct and not in use.
)

pause
