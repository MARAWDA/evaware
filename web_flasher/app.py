from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from flask import Flask, jsonify, render_template, request
from werkzeug.utils import secure_filename
import serial.tools.list_ports as list_ports

BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
FLASH_PACKAGE = PROJECT_ROOT / "flash_package"
UPLOAD_DIR = BASE_DIR / "uploads"
UPLOAD_DIR.mkdir(exist_ok=True)

app = Flask(__name__, template_folder="templates", static_folder="static")

BOARD_FIRMWARE = {
    "esp32-cyd": {"single": "HaleHound-CYD-FULL.bin", "split": "HaleHound-CYD.bin"},
    "esp32-e32r35t": {"single": "HaleHound-E32R35T-FULL.bin", "split": "HaleHound-E32R35T.bin"},
    "esp32-e32r28t": {"single": "HaleHound-E32R28T-FULL.bin", "split": "HaleHound-E32R28T.bin"},
}


def get_flash_package_files():
    if not FLASH_PACKAGE.exists():
        return []

    files = []
    for item in sorted(FLASH_PACKAGE.iterdir()):
        if item.is_file() and item.suffix.lower() in {".bin", ".img"}:
            files.append({
                "name": item.name,
                "path": str(item.relative_to(PROJECT_ROOT)),
                "full_path": str(item),
            })
    return files


def resolve_firmware_path(selected_name: str | None) -> Path | None:
    if not selected_name:
        return None

    candidate = (PROJECT_ROOT / selected_name).resolve()
    if candidate.exists() and candidate.is_file():
        return candidate

    for package_file in FLASH_PACKAGE.iterdir():
        if package_file.name == selected_name and package_file.is_file():
            return package_file

    return None


@app.get("/")
def index():
    return render_template("index.html")


@app.get("/api/ports")
def api_ports():
    ports = []
    for port in list_ports.comports():
        ports.append({
            "name": port.device,
            "description": port.description,
            "hwid": port.hwid,
        })
    return jsonify({"ports": ports})


@app.get("/api/files")
def api_files():
    return jsonify({"files": get_flash_package_files()})


@app.post("/api/flash")
def api_flash():
    port = (request.form.get("port") or "").strip()
    board = (request.form.get("board") or "esp32-cyd").strip()
    flash_mode = (request.form.get("flash_mode") or "single").strip()
    selected_name = (request.form.get("firmware_name") or "").strip()
    custom_file = request.files.get("custom_firmware")

    if not port:
        return jsonify({"ok": False, "output": "No serial port was selected."}), 400

    if flash_mode not in {"single", "split"}:
        flash_mode = "single"

    if custom_file and custom_file.filename:
        if flash_mode == "split":
            return jsonify({
                "ok": False,
                "output": "For the four-file mode, use the board's split firmware file and the included boot files from flash_package.",
            }), 400

        safe_name = secure_filename(custom_file.filename)
        if not safe_name:
            return jsonify({"ok": False, "output": "Invalid custom firmware filename."}), 400
        target_path = UPLOAD_DIR / safe_name
        custom_file.save(target_path)
        firmware_path = target_path
        command = [
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            "esp32",
            "--port",
            port,
            "--baud",
            "460800",
            "--before",
            "default_reset",
            "--after",
            "hard_reset",
            "write_flash",
            "0x0",
            str(firmware_path),
        ]
    else:
        firmware_name = selected_name or BOARD_FIRMWARE.get(board, {}).get(flash_mode, "HaleHound-CYD-FULL.bin")
        firmware_path = resolve_firmware_path(firmware_name)

        if firmware_path is None or not firmware_path.exists():
            return jsonify({
                "ok": False,
                "output": "The selected firmware file could not be found. Pick a valid .bin or upload a custom one.",
            }), 400

        if flash_mode == "single":
            command = [
                sys.executable,
                "-m",
                "esptool",
                "--chip",
                "esp32",
                "--port",
                port,
                "--baud",
                "460800",
                "--before",
                "default_reset",
                "--after",
                "hard_reset",
                "write_flash",
                "0x0",
                str(firmware_path),
            ]
        else:
            shared_boot = FLASH_PACKAGE / "bootloader.bin"
            partitions = FLASH_PACKAGE / "partitions.bin"
            boot_app0 = FLASH_PACKAGE / "boot_app0.bin"

            missing = [
                str(path)
                for path in [shared_boot, partitions, boot_app0]
                if not path.exists()
            ]
            if missing:
                return jsonify({
                    "ok": False,
                    "output": "Missing required shared boot files: " + ", ".join(missing),
                }), 400

            command = [
                sys.executable,
                "-m",
                "esptool",
                "--chip",
                "esp32",
                "--port",
                port,
                "--baud",
                "460800",
                "--before",
                "default_reset",
                "--after",
                "hard_reset",
                "write_flash",
                "0x1000",
                str(shared_boot),
                "0x8000",
                str(partitions),
                "0xe000",
                str(boot_app0),
                "0x10000",
                str(firmware_path),
            ]

    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except FileNotFoundError:
        return jsonify({
            "ok": False,
            "output": "esptool is not installed. Run: py -m pip install -r web_flasher/requirements.txt",
        }), 500

    output = (result.stdout or "") + (result.stderr or "")
    return jsonify({
        "ok": result.returncode == 0,
        "output": output.strip() or "Flashing finished.",
    })


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Local ESP32 web flasher")
    parser.add_argument("--host", default="127.0.0.1", help="Host to bind the webpage to")
    parser.add_argument("--port", default=8000, type=int, help="Port to run the web app on")
    args = parser.parse_args()
    app.run(host=args.host, port=args.port, debug=False)
