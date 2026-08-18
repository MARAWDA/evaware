# Local ESP32 Web Flasher

This folder contains a small local web app that lets you:

- select a board profile
- pick a COM port
- choose a firmware `.bin` file from the project or upload your own
- run a legitimate ESP32 flash command through `esptool`

It is designed for normal firmware flashing only and does not include radio attack automation or offensive tooling.

## Quick start on Windows

1. Open PowerShell in this project folder.
2. Run the following, using whichever Python command works on your machine:

```powershell
cd "c:\Users\wisht\Downloads\HaleHound-CYD-main\HaleHound-CYD-main"

# Try this first
py -m pip install -r web_flasher\requirements.txt
py web_flasher\app.py --host 127.0.0.1 --port 8000
```

If `py` is not recognized, use:

```powershell
python -m pip install -r web_flasher\requirements.txt
python web_flasher\app.py --host 127.0.0.1 --port 8000
```

3. Open this URL in your browser:

```text
http://127.0.0.1:8000
```

4. Pick your board, choose the serial port, and click Flash firmware.

## If Windows does not recognize the board

Install the USB driver before flashing:

- CH340: https://www.wch-ic.com/downloads/CH341SER_EXE.html
- CP210x: https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers

Then unplug and replug the board.

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
