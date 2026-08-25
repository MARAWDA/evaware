"""Firmware flashing/erasing logic for EVAWARE / HaleHound-CYD boards.

Calls esptool's Python API in-process (rather than shelling out to
``python -m esptool``) since these boards are plain ESP32 parts flashed
over the UART bootloader -- there is no DFU mode like on the Flipper Zero.
Calling esptool in-process also means this keeps working once the app is
frozen into a standalone .exe with PyInstaller, where there is no separate
Python interpreter to spawn as a subprocess.

Runs in a QThread so the GUI stays responsive, and parses esptool's stdout
to drive a progress bar.
"""
from __future__ import annotations

import contextlib
import io
import re
import sys
from pathlib import Path

from PySide6.QtCore import QThread, Signal

# When frozen by PyInstaller there is no source tree next to the script, so
# flash_package/ is expected to live next to the built .exe instead.
if getattr(sys, "frozen", False):
    PROJECT_ROOT = Path(sys.executable).resolve().parent
else:
    PROJECT_ROOT = Path(__file__).resolve().parent.parent
FLASH_PACKAGE = PROJECT_ROOT / "flash_package"

# Board id -> (single-file firmware, split-mode firmware)
BOARD_FIRMWARE = {
    "esp32-cyd": {"single": "HaleHound-CYD-FULL.bin", "split": "HaleHound-CYD.bin"},
    "esp32-e32r35t": {"single": "HaleHound-E32R35T-FULL.bin", "split": "HaleHound-E32R35T.bin"},
    "esp32-e32r28t": {"single": "HaleHound-E32R28T-FULL.bin", "split": "HaleHound-E32R28T.bin"},
}

# Matches esptool progress output like "Writing at 0x00010000... (42 %)"
PROGRESS_RE = re.compile(r"\((\d{1,3})\s?%\)")


def resolve_firmware_path(name: str | None) -> Path | None:
    if not name:
        return None
    candidate = (PROJECT_ROOT / name).resolve()
    if candidate.exists() and candidate.is_file():
        return candidate
    if FLASH_PACKAGE.exists():
        for item in FLASH_PACKAGE.iterdir():
            if item.name == name and item.is_file():
                return item
    return None


def build_flash_args(port: str, flash_mode: str, firmware_path: Path) -> list[str] | None:
    """Build the esptool argv (without the "esptool" program name itself)."""
    base = [
        "--chip", "esp32",
        "--port", port,
        "--baud", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
    ]
    if flash_mode == "single":
        return base + ["0x0", str(firmware_path)]

    bootloader = FLASH_PACKAGE / "bootloader.bin"
    partitions = FLASH_PACKAGE / "partitions.bin"
    boot_app0 = FLASH_PACKAGE / "boot_app0.bin"
    if not all(p.exists() for p in (bootloader, partitions, boot_app0)):
        return None
    return base + [
        "0x1000", str(bootloader),
        "0x8000", str(partitions),
        "0xe000", str(boot_app0),
        "0x10000", str(firmware_path),
    ]


def build_erase_args(port: str) -> list[str]:
    return [
        "--chip", "esp32",
        "--port", port,
        "--before", "default_reset",
        "--after", "hard_reset",
        "erase_flash",
    ]


class _LineEmittingWriter(io.TextIOBase):
    """File-like object that emits each printed line/carriage-return chunk as a Qt signal."""

    def __init__(self, emit_line):
        super().__init__()
        self._emit_line = emit_line
        self._buffer = ""

    def write(self, text: str) -> int:
        self._buffer += text
        while True:
            for sep in ("\n", "\r"):
                if sep in self._buffer:
                    line, self._buffer = self._buffer.split(sep, 1)
                    if line.strip():
                        self._emit_line(line.strip())
                    break
            else:
                break
        return len(text)

    def flush(self):
        pass


class FlashWorker(QThread):
    """Runs an esptool operation in-process and streams its output back to the GUI."""

    log_line = Signal(str)
    progress = Signal(int)
    finished_ok = Signal(bool, str)

    def __init__(self, args: list[str], parent=None):
        super().__init__(parent)
        self._args = args

    def run(self):
        try:
            import esptool
        except ImportError:
            self.finished_ok.emit(
                False,
                "esptool is not installed. Run: pip install -r evaware_flasher/requirements.txt",
            )
            return

        writer = _LineEmittingWriter(self._emit_log)
        try:
            with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
                esptool.main(self._args)
        except SystemExit as exc:
            code = exc.code or 0
            if code != 0:
                self.finished_ok.emit(False, f"esptool exited with code {code}.")
                return
        except Exception as exc:  # noqa: BLE001 - surface any esptool failure to the console
            self.finished_ok.emit(False, f"esptool error: {exc}")
            return

        self.progress.emit(100)
        self.finished_ok.emit(True, "Operation completed successfully.")

    def _emit_log(self, line: str):
        self.log_line.emit(line)
        match = PROGRESS_RE.search(line)
        if match:
            self.progress.emit(int(match.group(1)))
