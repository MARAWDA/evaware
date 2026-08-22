@echo off
REM Manual/visible launcher for the HaleHound web flasher.
REM Double-click this if the page ever goes down and you want to see errors.
cd /d "%~dp0.."

where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    py "web_flasher\app.py" --host 127.0.0.1 --port 8000
) else (
    where python >nul 2>nul
    if %ERRORLEVEL% EQU 0 (
        python "web_flasher\app.py" --host 127.0.0.1 --port 8000
    ) else (
        echo Python was not found on PATH. Install it from https://python.org/downloads and try again.
    )
)
pause
