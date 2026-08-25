"""Serial communication handler for the EVAWARE / HaleHound-CYD device.

The device is a plain ESP32 board (CYD 2.8", QDtech E32R28T/E32R35T) that
exposes a USB-UART bridge (CP2102, CH340, or FTDI depending on the board
batch) rather than a native USB CDC VID:PID like the Flipper Zero. So
instead of matching one VID:PID, we scan for the common USB-UART bridge
chips used by these boards and let the user override the port manually.

On boot the firmware prints a banner over Serial at 115200 baud:
    "        EVAWARE-CYD v4.0.0"
(see cyd_config.h FW_DEVICE/FW_VERSION and HaleHound-CYD.ino Serial.begin).
We watch for that line to learn the connected board/firmware version.
"""
from __future__ import annotations

import re
import time
from dataclasses import dataclass

import serial
import serial.tools.list_ports
from PySide6.QtCore import QThread, Signal

# USB-UART bridge chips commonly fitted to CYD / QDtech boards.
KNOWN_BRIDGE_VIDS = {
    0x10C4,  # Silicon Labs CP210x
    0x1A86,  # WCH CH340 / CH9102
    0x0403,  # FTDI
}

BAUD_RATE = 115200
POLL_INTERVAL_S = 0.1

# Matches the boot banner: "        EVAWARE-CYD v4.0.0"
BANNER_RE = re.compile(r"(EVAWARE-[A-Z0-9-]+)\s+(v[\d.]+)")

# Screen-mirror wire protocol (see screen_mirror.cpp on the device).
MIRROR_TRIGGER_BYTE = b"\x02"
MIRROR_BAUD = 1500000
MIRROR_MAGIC = b"EVFB"
MIRROR_END_MAGIC = b"FEND"
MIRROR_TIMEOUT_S = 12.0


@dataclass
class DeviceInfo:
    port: str
    description: str = "Unknown"
    board: str = "Unknown"
    firmware_version: str = "Unknown"


def list_candidate_ports() -> list[serial.tools.list_ports_common.ListPortInfo]:
    """Return all serial ports, with known USB-UART bridges listed first."""
    ports = list(serial.tools.list_ports.comports())
    ports.sort(key=lambda p: 0 if p.vid in KNOWN_BRIDGE_VIDS else 1)
    return ports


def find_default_port() -> str | None:
    """Best-guess port: the first known USB-UART bridge found, if any."""
    for port in list_candidate_ports():
        if port.vid in KNOWN_BRIDGE_VIDS:
            return port.device
    return None


class SerialHandler(QThread):
    """Background thread that owns the serial connection to the device."""

    connected = Signal(DeviceInfo)
    disconnected = Signal()
    banner_detected = Signal(str, str)  # board, firmware_version
    log_message = Signal(str)
    error = Signal(str)
    frame_ready = Signal(bytes, int, int, int)  # rgb888 bytes, width, height, rotation
    frame_error = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._running = False
        self._serial: serial.Serial | None = None
        self._device_info: DeviceInfo | None = None
        self._requested_port: str | None = None
        self._line_buffer = ""
        self._frame_requested = False
        self._suspended = False

    @property
    def is_connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    def set_port(self, port: str | None):
        """Request a specific port (None = auto-detect)."""
        self._requested_port = port

    def request_frame(self):
        """Ask the background thread to grab one screen-mirror frame on its next pass."""
        self._frame_requested = True

    def suspend_for_flashing(self):
        """Close the port and stop auto-reconnecting so esptool can own it exclusively."""
        self._suspended = True
        self.close_port_for_flashing()

    def resume_after_flashing(self):
        """Let the background thread resume auto-detecting/reconnecting to the device."""
        self._suspended = False

    def start_monitoring(self):
        self._running = True
        self.start()

    def stop_monitoring(self):
        self._running = False
        self.wait(2000)

    def run(self):
        while self._running:
            if self._suspended:
                time.sleep(0.2)
                continue

            if not self.is_connected:
                port = self._requested_port or find_default_port()
                if port:
                    self._connect(port)
                else:
                    time.sleep(1.0)
                    continue

            try:
                assert self._serial is not None
                if self._frame_requested:
                    self._frame_requested = False
                    self._capture_frame()
                if self._serial.in_waiting > 0:
                    data = self._serial.read(self._serial.in_waiting)
                    if data:
                        self._handle_data(data)
                time.sleep(POLL_INTERVAL_S)
            except (serial.SerialException, OSError):
                self._disconnect()

    def _read_exact(self, count: int) -> bytes:
        assert self._serial is not None
        chunks = bytearray()
        while len(chunks) < count:
            chunk = self._serial.read(count - len(chunks))
            if not chunk:
                raise TimeoutError(f"Timed out reading screen-mirror data ({len(chunks)}/{count} bytes)")
            chunks += chunk
        return bytes(chunks)

    def _sync_to_magic(self, magic: bytes, timeout_s: float):
        """Read one byte at a time until the trailing bytes match `magic`.

        Discards any leading noise from the baud-switch race instead of
        assuming the magic is the very first thing on the wire.
        """
        assert self._serial is not None
        window = bytearray()
        total_seen = 0
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            byte = self._serial.read(1)
            if not byte:
                continue
            total_seen += 1
            window += byte
            if len(window) > len(magic):
                del window[0:len(window) - len(magic)]
            if bytes(window) == magic:
                return
        if total_seen == 0:
            raise TimeoutError(
                f"Timed out looking for frame magic {magic!r} (device never responded)"
            )
        raise TimeoutError(
            f"Timed out looking for frame magic {magic!r} "
            f"({total_seen} bytes seen, last: {bytes(window)!r})"
        )

    def _capture_frame(self):
        assert self._serial is not None
        original_baud = self._serial.baudrate
        original_timeout = self._serial.timeout
        try:
            self._serial.reset_input_buffer()
            self._serial.write(MIRROR_TRIGGER_BYTE)
            self._serial.flush()
            time.sleep(0.1)  # let the device finish switching its UART baud
            self._serial.baudrate = MIRROR_BAUD
            self._serial.timeout = MIRROR_TIMEOUT_S

            self._sync_to_magic(MIRROR_MAGIC, MIRROR_TIMEOUT_S)

            header = self._read_exact(5)
            width = header[0] | (header[1] << 8)
            height = header[2] | (header[3] << 8)
            rotation = header[4]

            pixel_count = width * height
            rgb = bytearray(pixel_count * 3)
            decoded = 0
            while decoded < pixel_count:
                run_len = self._read_exact(1)[0]
                color_bytes = self._read_exact(2)
                color = color_bytes[0] | (color_bytes[1] << 8)
                # Panel GRAM is BGR565 (see applyColorOrder() in utils.cpp), not RGB565
                b = ((color >> 11) & 0x1F) * 255 // 31
                g = ((color >> 5) & 0x3F) * 255 // 63
                r = (color & 0x1F) * 255 // 31
                run_len = min(run_len, pixel_count - decoded)
                rgb[decoded * 3:(decoded + run_len) * 3] = bytes((r, g, b)) * run_len
                decoded += run_len

            self._read_exact(4)  # trailing "FEND" sanity marker, best-effort
            self.frame_ready.emit(bytes(rgb), width, height, rotation)
        except (serial.SerialException, OSError, TimeoutError, ValueError, IndexError) as exc:
            self.frame_error.emit(str(exc))
        finally:
            try:
                self._serial.reset_input_buffer()
                self._serial.baudrate = original_baud
                self._serial.timeout = original_timeout
            except (serial.SerialException, OSError):
                pass

    def _handle_data(self, data: bytes):
        text = data.decode("utf-8", errors="replace")
        self._line_buffer += text
        while "\n" in self._line_buffer:
            line, self._line_buffer = self._line_buffer.split("\n", 1)
            line = line.rstrip("\r")
            if line.strip():
                self.log_message.emit(line)
            match = BANNER_RE.search(line)
            if match and self._device_info is not None:
                board, version = match.group(1), match.group(2)
                self._device_info.board = board
                self._device_info.firmware_version = version
                self.banner_detected.emit(board, version)

    def _connect(self, port: str):
        try:
            self._serial = serial.Serial(port, BAUD_RATE, timeout=0.1)
            description = next(
                (p.description for p in serial.tools.list_ports.comports() if p.device == port),
                "Unknown",
            )
            self._device_info = DeviceInfo(port=port, description=description)
            self.log_message.emit(f"Device connected on {port}")
            self.connected.emit(self._device_info)
        except (serial.SerialException, OSError) as exc:
            self.error.emit(f"Failed to open {port}: {exc}")
            self._serial = None

    def _disconnect(self):
        if self._serial is not None:
            try:
                self._serial.close()
            except (serial.SerialException, OSError):
                pass
        self._serial = None
        self._device_info = None
        self._line_buffer = ""
        self.log_message.emit("Device disconnected")
        self.disconnected.emit()

    def close_port_for_flashing(self):
        """Release the serial port so esptool can use it."""
        if self._serial is not None:
            try:
                self._serial.close()
            except (serial.SerialException, OSError):
                pass
            self._serial = None

    def get_device_info(self) -> DeviceInfo | None:
        return self._device_info
