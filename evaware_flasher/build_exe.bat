@echo off
REM Build EvaWareFlasher.exe with PyInstaller. Run from repo root or this folder.
cd /d "%~dp0"
"%~dp0..\.venv\Scripts\pyinstaller.exe" --noconfirm --onefile --windowed --name EvaWareFlasher --add-data "style.qss;." --collect-data esptool main.py
echo.
echo Built exe: %~dp0dist\EvaWareFlasher.exe
echo Copy flash_package\ next to the exe before flashing.
