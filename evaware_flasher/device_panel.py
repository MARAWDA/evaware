"""Center "live view" panel for the EVAWARE device.

Shows the actual mirrored TFT screen (via the screen_mirror.cpp firmware
protocol, decoded in serial_handler.SerialHandler) plus EVAWARE branding
and a scrolling tail of the device's debug UART output underneath it.
Click the screen mirror itself to expand/collapse it to fill the window.
"""
from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QLabel, QPlainTextEdit, QSizePolicy, QVBoxLayout, QWidget

try:
    from PIL import Image, ImageFilter, ImageOps
    _HAS_PIL = True
except ImportError:
    _HAS_PIL = False


class MirrorScreenLabel(QLabel):
    """QLabel that rescales the most recently captured (small, downsampled)
    frame whenever it resizes -- using Lanczos resampling via Pillow, which
    looks noticeably smoother than Qt's bilinear "smooth" scaling for the
    kind of large upscale factors this mirror needs (device screens are
    captured at reduced resolution, then blown up to fill the panel)."""

    clicked = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._source_rgb: bytes | None = None
        self._source_size: tuple[int, int] | None = None

    def set_source_frame(self, rgb888: bytes | None, width: int, height: int):
        self._source_rgb = rgb888
        self._source_size = (width, height) if rgb888 else None
        self._refresh_scaled()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._refresh_scaled()

    def mousePressEvent(self, event):
        super().mousePressEvent(event)
        self.clicked.emit()

    def _refresh_scaled(self):
        if self._source_rgb is None or self._source_size is None:
            return
        box_w, box_h = max(self.width(), 1), max(self.height(), 1)
        src_w, src_h = self._source_size
        scale = min(box_w / src_w, box_h / src_h)
        target_w = max(1, round(src_w * scale))
        target_h = max(1, round(src_h * scale))

        if _HAS_PIL:
            image = Image.frombuffer("RGB", (src_w, src_h), self._source_rgb, "raw", "RGB", 0, 1)
            # The device's dark theme (dark red/orange on near-black) is very
            # low-contrast once mirrored to a desktop display -- convert to
            # grayscale, stretch to the full black-to-white range, then tint
            # the midtones (where most UI text/icons/borders land) a warm
            # yellow-orange while keeping true blacks and true whites intact.
            image = image.convert("L")
            image = ImageOps.autocontrast(image, cutoff=1)
            image = ImageOps.colorize(image, black="#000000", white="#FFFFFF", mid="#FFB020")
            image = image.resize((target_w, target_h), Image.LANCZOS)
            # Upscaled source pixels are inherently soft -- an unsharp mask
            # pass makes edges/text read as crisper without more real detail.
            image = image.filter(ImageFilter.UnsharpMask(radius=2, percent=130, threshold=2))
            qimage = QImage(image.tobytes(), target_w, target_h, target_w * 3, QImage.Format_RGB888).copy()
            self.setPixmap(QPixmap.fromImage(qimage))
        else:
            source_image = QImage(self._source_rgb, src_w, src_h, src_w * 3, QImage.Format_RGB888)
            scaled = QPixmap.fromImage(source_image).scaled(
                target_w, target_h, Qt.KeepAspectRatio, Qt.SmoothTransformation,
            )
            self.setPixmap(scaled)


class DevicePanel(QWidget):
    screen_clicked = Signal()


    def __init__(self, parent=None):
        super().__init__(parent)

        self._title = QLabel("EVAWARE")
        self._title.setAlignment(Qt.AlignCenter)
        self._title.setStyleSheet("font-size: 16px; font-weight: bold; letter-spacing: 4px;")

        self._subtitle = QLabel("NO DEVICE CONNECTED")
        self._subtitle.setAlignment(Qt.AlignCenter)
        self._subtitle.setStyleSheet("font-size: 11px;")

        self._screen_label = MirrorScreenLabel("Screen mirror will appear here once you press LIVE VIEW.")
        self._screen_label.setAlignment(Qt.AlignCenter)
        self._screen_label.setStyleSheet("border: 1px solid #FFFFFF;")
        self._screen_label.setWordWrap(True)
        self._screen_label.setCursor(Qt.PointingHandCursor)
        self._screen_label.setToolTip("Click to expand/collapse")
        # Without this, setting a pixmap makes the label's sizeHint grow to the
        # image's native size, which then grows the layout, which lets the next
        # frame scale even bigger -- a runaway feedback loop that eats the window.
        self._screen_label.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Ignored)
        self._screen_label.setMinimumSize(1, 1)
        self._screen_label.clicked.connect(self.screen_clicked)

        self._console = QPlainTextEdit()
        self._console.setReadOnly(True)
        self._console.setMaximumBlockCount(500)
        self._console.setFixedHeight(70)
        self._console.setPlaceholderText("Live device output will appear here once connected...")

        layout = QVBoxLayout(self)
        layout.addWidget(self._title)
        layout.addWidget(self._subtitle)
        layout.addSpacing(4)
        layout.addWidget(self._screen_label, stretch=1)
        layout.addWidget(self._console)

        self._header_widgets = [self._title, self._subtitle]

    def set_expanded(self, expanded: bool):
        """Hide the branding/console chrome so the mirrored screen fills the panel."""
        for widget in self._header_widgets:
            widget.setVisible(not expanded)
        self._console.setVisible(not expanded)

    def set_connected(self, board: str, firmware_version: str):
        self._subtitle.setText(f"{board}  //  {firmware_version}")

    def set_disconnected(self):
        self._subtitle.setText("NO DEVICE CONNECTED")
        self._console.clear()
        self._screen_label.set_source_frame(None, 0, 0)
        self._screen_label.clear()
        self._screen_label.setText("Screen mirror will appear here once you press LIVE VIEW.")

    def append_line(self, line: str):
        self._console.appendPlainText(line)

    def show_frame(self, rgb888: bytes, width: int, height: int, rotation: int):
        self._screen_label.set_source_frame(rgb888, width, height)

    def show_frame_error(self, message: str):
        self._screen_label.set_source_frame(None, 0, 0)
        self._screen_label.setText(f"Screen mirror failed:\n{message}")

