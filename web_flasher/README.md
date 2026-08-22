# Local ESP32 Web Flasher

This folder contains a small local web app that lets you:

- select a board profile
- pick a COM port
- choose a firmware `.bin` file from the project or upload your own
- run a legitimate ESP32 flash command through `esptool`
- erase a board's flash before reflashing

It is designed for normal firmware flashing only and does not include radio attack automation or offensive tooling.

## Setup on a new PC (step by step)

These steps work on any Windows PC, not just the one this was built on.

### 1. Get the project files

Copy or download this whole project folder (the one containing `HaleHound-CYD.ino`,
`platformio.ini`, and this `web_flasher` folder) onto the new PC. Any folder location works —
nothing in the flasher depends on a specific path.

### 2. Install Python

You need Python 3.10 or newer.

- Download it from https://www.python.org/downloads/
- During install, check **"Add python.exe to PATH"**.
- If Python is already installed, skip this step.

Verify it worked by opening PowerShell and running:

```powershell
python --version
```

(If that fails, try `py --version` instead — both are used interchangeably below.)

### 3. Install the USB driver for the board

The board's USB-to-serial chip needs a driver before Windows will show it as a COM port.

- CH340: https://www.wch-ic.com/downloads/CH341SER_EXE.html
- CP210x: https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers

Install the one that matches the board, then plug it in with a data-capable USB cable
(not a charge-only cable).

### 4. Install the flasher's Python dependencies

Open PowerShell in the project folder (the one with `platformio.ini` in it) and run:

```powershell
cd path\to\HaleHound-CYD-main
python -m pip install -r web_flasher\requirements.txt
```

### 5. Start the flasher

Any of these work — pick whichever is easiest:

- Double-click `web_flasher\start_flasher.bat` (shows a console window with logs).
- Double-click `web_flasher\start_flasher_hidden.vbs` (starts silently in the background).
- Or run it manually from PowerShell:

```powershell
python web_flasher\app.py --host 127.0.0.1 --port 8000
```

### 6. Open the flasher

In a browser, go to:

```text
http://127.0.0.1:8000
```

### 7. Flash the board

1. Pick the board profile that matches the hardware.
2. Pick the flash mode (single file is recommended).
3. Pick the serial port for the board (click Refresh if it's not listed).
4. Pick a firmware file, or upload your own `.bin`.
5. Click **Flash**.
6. If you ever need a clean slate first, click **Erase Flash**, then flash normally afterward.

Wait for the log to say the flash finished, then unplug and replug the board to reboot it.

## If Windows does not recognize the board

- Reinstall/confirm the USB driver from step 3.
- Try a different USB cable — many cables are charge-only and carry no data lines.
- Unplug other USB serial devices so the correct COM port is easy to pick.

## Troubleshooting

- **"python is not recognized"** — Python isn't installed or wasn't added to PATH. Reinstall
  Python and check the PATH option, or use the full path to `python.exe`.
- **"No module named flask" / esptool errors** — dependencies weren't installed; rerun step 4.
- **Port list is empty** — the board isn't plugged in, the driver isn't installed, or another
  program (like a serial monitor) is already holding the port open.
- **Flashing fails partway through** — try a slower/different USB cable or port, and make sure
  nothing else is reading the same COM port at the same time.

## Common notes

- Use a data-capable USB cable and not a charge-only cable.
- Disconnect other USB serial devices first for easier port selection.
- Leave the board connected until the log shows the flash is complete.
- After flashing, unplug and replug the board to reboot.

## Files used

- `web_flasher/app.py` - Flask web server and flash backend
- `web_flasher/templates/index.html` - UI page
- `web_flasher/static/app.js` - browser logic
- `web_flasher/static/styles.css` - matching styling
- `web_flasher/start_flasher.bat` - visible launcher (Windows)
- `web_flasher/start_flasher_hidden.vbs` - silent/background launcher (Windows)
- `web_flasher/start.ps1` - PowerShell launcher that auto-detects Python
