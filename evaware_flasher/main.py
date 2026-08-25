"""EVAWARE Flasher - a QFlipper-inspired desktop tool for HaleHound-CYD boards.

Strict black & white monochrome UI (see style.qss). Left panel shows device
info, board selection, and FLASH/ERASE/REBOOT actions. Center panel is the
live device view (device_panel.DevicePanel). Bottom panel is a scrolling
console log. Status bar reports CONNECTED/DISCONNECTED.
"""
from __future__ import annotations

import sys
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from device_panel import DevicePanel
from flasher import BOARD_FIRMWARE, FlashWorker, build_erase_args, build_flash_args, resolve_firmware_path
from serial_handler import DeviceInfo, SerialHandler, list_candidate_ports

APP_DIR = Path(__file__).resolve().parent

# When frozen by PyInstaller, data files (like style.qss) live in _MEIPASS,
# and firmware/flash_package lookups should be relative to the exe itself.
if getattr(sys, "frozen", False):
    RESOURCE_DIR = Path(getattr(sys, "_MEIPASS", APP_DIR))
    PROJECT_ROOT_DIR = Path(sys.executable).resolve().parent
else:
    RESOURCE_DIR = APP_DIR
    PROJECT_ROOT_DIR = APP_DIR.parent


BOARD_LABELS = {
    "esp32-cyd": 'CYD 2.8" (ESP32-2432S028)',
    "esp32-e32r35t": 'QDtech E32R35T (3.5")',
    "esp32-e32r28t": "QDtech E32R28T",
}


def separator() -> QFrame:
    line = QFrame()
    line.setObjectName("separator")
    line.setFrameShape(QFrame.HLine)
    return line


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("EVAWARE Flasher")
        self.resize(1000, 640)

        self._serial_handler = SerialHandler(self)
        self._flash_worker: FlashWorker | None = None
        self._custom_firmware: Path | None = None

        self._live_view_enabled = False
        self._live_view_timer = QTimer(self)
        self._live_view_timer.setSingleShot(True)
        self._live_view_timer.setInterval(200)
        self._live_view_timer.timeout.connect(self._request_live_frame)

        self._expanded = False

        self._build_ui()
        self._connect_signals()

        self._refresh_ports()
        self._serial_handler.start_monitoring()

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)

        self._splitter = QSplitter(Qt.Horizontal)
        root_layout.addWidget(self._splitter, stretch=1)

        self._left_panel = self._build_left_panel()
        self._splitter.addWidget(self._left_panel)
        self._splitter.addWidget(self._build_center_panel())
        self._splitter.setStretchFactor(0, 1)
        self._splitter.setStretchFactor(1, 5)

        self._console = QPlainTextEdit()
        self._console.setReadOnly(True)
        self._console.setMaximumBlockCount(1000)
        self._console.setFixedHeight(90)
        root_layout.addWidget(self._console)

        self._status_label = QLabel("STATUS: DISCONNECTED")
        self.statusBar().addPermanentWidget(self._status_label)

    def _build_left_panel(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)

        device_header = QLabel("DEVICE")
        device_header.setObjectName("sectionHeader")
        layout.addWidget(device_header)

        self._port_combo = QComboBox()
        layout.addWidget(self._port_combo)

        refresh_btn = QPushButton("REFRESH PORTS")
        refresh_btn.clicked.connect(self._refresh_ports)
        layout.addWidget(refresh_btn)

        self._board_label = QLabel("Board: Unknown")
        self._version_label = QLabel("Firmware: Unknown")
        layout.addWidget(self._board_label)
        layout.addWidget(self._version_label)

        layout.addWidget(separator())

        flash_header = QLabel("FIRMWARE")
        flash_header.setObjectName("sectionHeader")
        layout.addWidget(flash_header)

        self._board_combo = QComboBox()
        for board_id, label in BOARD_LABELS.items():
            self._board_combo.addItem(label, userData=board_id)
        layout.addWidget(self._board_combo)

        self._mode_combo = QComboBox()
        self._mode_combo.addItem("Single file (0x0)", userData="single")
        self._mode_combo.addItem("Four file (bootloader/partitions/app0)", userData="split")
        layout.addWidget(self._mode_combo)

        self._custom_firmware_label = QLabel("Custom file: none (using bundled flash_package)")
        self._custom_firmware_label.setWordWrap(True)
        layout.addWidget(self._custom_firmware_label)

        choose_btn = QPushButton("CHOOSE FIRMWARE FILE...")
        choose_btn.clicked.connect(self._choose_firmware_file)
        layout.addWidget(choose_btn)

        layout.addWidget(separator())

        actions_header = QLabel("ACTIONS")
        actions_header.setObjectName("sectionHeader")
        layout.addWidget(actions_header)

        self._flash_btn = QPushButton("FLASH")
        self._flash_btn.clicked.connect(self._on_flash_clicked)
        layout.addWidget(self._flash_btn)

        self._erase_btn = QPushButton("ERASE")
        self._erase_btn.clicked.connect(self._on_erase_clicked)
        layout.addWidget(self._erase_btn)

        self._reboot_btn = QPushButton("REBOOT")
        self._reboot_btn.clicked.connect(self._on_reboot_clicked)
        layout.addWidget(self._reboot_btn)

        self._live_view_btn = QPushButton("LIVE VIEW: OFF")
        self._live_view_btn.setCheckable(True)
        self._live_view_btn.toggled.connect(self._on_live_view_toggled)
        layout.addWidget(self._live_view_btn)

        self._progress_bar = QProgressBar()
        self._progress_bar.setValue(0)
        layout.addWidget(self._progress_bar)

        layout.addStretch(1)
        return panel

    def _build_center_panel(self) -> QWidget:
        self._device_panel = DevicePanel()
        return self._device_panel

    def _connect_signals(self):
        self._serial_handler.connected.connect(self._on_device_connected)
        self._serial_handler.disconnected.connect(self._on_device_disconnected)
        self._serial_handler.banner_detected.connect(self._on_banner_detected)
        self._serial_handler.log_message.connect(self._log)
        self._serial_handler.error.connect(self._log)
        self._serial_handler.frame_ready.connect(self._on_frame_ready)
        self._serial_handler.frame_error.connect(self._on_frame_error)
        self._device_panel.screen_clicked.connect(self._toggle_expanded)

    def _toggle_expanded(self):
        self._expanded = not self._expanded
        self._left_panel.setVisible(not self._expanded)
        self._console.setVisible(not self._expanded)
        self._device_panel.set_expanded(self._expanded)

    # --------------------------------------------------------------- ports
    def _refresh_ports(self):
        self._port_combo.clear()
        self._port_combo.addItem("Auto-detect", userData=None)
        for port in list_candidate_ports():
            label = f"{port.device} ({port.description})"
            self._port_combo.addItem(label, userData=port.device)

    def _selected_port(self) -> str | None:
        return self._port_combo.currentData()

    # ------------------------------------------------------------- logging
    def _log(self, message: str):
        timestamp = datetime.now().strftime("%H:%M:%S")
        self._console.appendPlainText(f"[{timestamp}] {message}")
        self._device_panel.append_line(message)

    # ------------------------------------------------------- serial events
    def _on_device_connected(self, device: DeviceInfo):
        self._status_label.setText("STATUS: CONNECTED")

    def _on_device_disconnected(self):
        self._status_label.setText("STATUS: DISCONNECTED")
        self._board_label.setText("Board: Unknown")
        self._version_label.setText("Firmware: Unknown")
        self._device_panel.set_disconnected()
        self._live_view_btn.setChecked(False)

    def _on_banner_detected(self, board: str, version: str):
        self._board_label.setText(f"Board: {board}")
        self._version_label.setText(f"Firmware: {version}")
        self._device_panel.set_connected(board, version)

    def _on_live_view_toggled(self, enabled: bool):
        self._live_view_enabled = enabled
        self._live_view_btn.setText("LIVE VIEW: ON" if enabled else "LIVE VIEW: OFF")
        if enabled:
            self._request_live_frame()
        else:
            self._live_view_timer.stop()

    def _request_live_frame(self):
        if not self._live_view_enabled or not self._serial_handler.is_connected:
            return
        self._serial_handler.request_frame()

    def _on_frame_ready(self, rgb888: bytes, width: int, height: int, rotation: int):
        self._device_panel.show_frame(rgb888, width, height, rotation)
        if self._live_view_enabled:
            self._live_view_timer.start()  # only queue the next frame once this one is fully done

    def _on_frame_error(self, message: str):
        self._log(f"Screen mirror: {message}")
        self._device_panel.show_frame_error(message)
        if self._live_view_enabled:
            self._live_view_timer.start()

    # -------------------------------------------------------------- flash
    def _choose_firmware_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select firmware file", str(PROJECT_ROOT_DIR), "Firmware (*.bin *.img);;All files (*)"
        )
        if path:
            self._custom_firmware = Path(path)
            self._custom_firmware_label.setText(f"Custom file: {self._custom_firmware.name}")

    def _on_flash_clicked(self):
        if self._flash_worker is not None and self._flash_worker.isRunning():
            return

        port = self._selected_port()
        if not port:
            QMessageBox.warning(self, "No port selected", "Select a serial port before flashing.")
            return

        board = self._board_combo.currentData()
        mode = self._mode_combo.currentData()

        if self._custom_firmware is not None:
            firmware_path = self._custom_firmware
        else:
            firmware_name = BOARD_FIRMWARE.get(board, {}).get(mode)
            firmware_path = resolve_firmware_path(firmware_name)

        if firmware_path is None or not firmware_path.exists():
            QMessageBox.warning(self, "Firmware not found", "Could not resolve a firmware file to flash.")
            return

        args = build_flash_args(port, mode, firmware_path)
        if args is None:
            QMessageBox.warning(
                self, "Missing boot files",
                "Four-file mode requires bootloader.bin, partitions.bin, and boot_app0.bin in flash_package/.",
            )
            return

        self._log(f"Flashing {firmware_path.name} to {port}...")
        self._run_flash_operation(args)

    def _on_erase_clicked(self):
        port = self._selected_port()
        if not port:
            QMessageBox.warning(self, "No port selected", "Select a serial port before erasing.")
            return

        confirm = QMessageBox.question(
            self, "Confirm erase", "This will wipe the device's internal flash storage. Continue?"
        )
        if confirm != QMessageBox.Yes:
            return

        self._log(f"Erasing flash on {port}...")
        self._run_flash_operation(build_erase_args(port))

    def _on_reboot_clicked(self):
        port = self._selected_port()
        if not port:
            QMessageBox.warning(self, "No port selected", "Select a serial port before rebooting.")
            return
        try:
            import serial as pyserial
            with pyserial.Serial(port, 115200) as ser:
                ser.setDTR(False)
                ser.setRTS(True)
                ser.setRTS(False)
            self._log(f"Reboot pulse sent to {port}.")
        except Exception as exc:  # noqa: BLE001 - surfaced to console, must not crash the app
            self._log(f"Reboot failed: {exc}")

    def _run_flash_operation(self, args: list[str]):
        self._live_view_btn.setChecked(False)
        self._serial_handler.suspend_for_flashing()
        self._set_actions_enabled(False)
        self._progress_bar.setValue(0)

        self._flash_worker = FlashWorker(args, self)
        self._flash_worker.log_line.connect(self._log)
        self._flash_worker.progress.connect(self._progress_bar.setValue)
        self._flash_worker.finished_ok.connect(self._on_flash_finished)
        self._flash_worker.start()

    def _on_flash_finished(self, ok: bool, message: str):
        self._log(message)
        self._serial_handler.resume_after_flashing()
        self._set_actions_enabled(True)
        if not ok:
            QMessageBox.critical(self, "Operation failed", message)

    def _set_actions_enabled(self, enabled: bool):
        self._flash_btn.setEnabled(enabled)
        self._erase_btn.setEnabled(enabled)
        self._reboot_btn.setEnabled(enabled)
        self._live_view_btn.setEnabled(enabled)

    def closeEvent(self, event):
        self._live_view_timer.stop()
        self._serial_handler.stop_monitoring()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    qss_path = RESOURCE_DIR / "style.qss"
    if qss_path.exists():
        app.setStyleSheet(qss_path.read_text(encoding="utf-8"))

    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
