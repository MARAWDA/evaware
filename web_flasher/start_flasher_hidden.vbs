' Launches the HaleHound web flasher with no visible console window.
' Safe to run multiple times: app.py binds to 127.0.0.1:8000, a second
' instance will simply fail to bind if one is already running.
Set shell = CreateObject("WScript.Shell")
projectDir = "c:\Users\wisht\Downloads\HaleHound-CYD-main\HaleHound-CYD-main"
pythonw = "C:\Users\wisht\AppData\Local\Programs\Python\Python312\pythonw.exe"
shell.CurrentDirectory = projectDir
shell.Run """" & pythonw & """ ""web_flasher\app.py"" --host 127.0.0.1 --port 8000", 0, False
