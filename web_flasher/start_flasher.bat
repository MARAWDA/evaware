@echo off
REM Manual/visible launcher for the HaleHound web flasher.
REM Double-click this if the page ever goes down and you want to see errors.
cd /d "c:\Users\wisht\Downloads\HaleHound-CYD-main\HaleHound-CYD-main"
"C:\Users\wisht\AppData\Local\Programs\Python\Python312\python.exe" "web_flasher\app.py" --host 127.0.0.1 --port 8000
pause
