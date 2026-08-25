// =============================================================================
// HaleHound-CYD USB Screen Mirror
// See screen_mirror.h for overview.
//
// Wire protocol (little-endian):
//   PC -> device : single byte 0x02 (frame request trigger)
//   device -> PC : "EVFB" magic (4 bytes)
//                  width  (uint16, already reduced — see MIRROR_SCALE)
//                  height (uint16, already reduced)
//                  rotation (uint8)
//                  row-major BGR565 pixels (native panel GRAM order), RLE:
//                    repeated (run_len uint8 [1-255], color uint16) tuples
//                    until width*height pixels have been emitted
//                  "FEND" magic (4 bytes)
//
// The transfer temporarily bumps the UART to MIRROR_BAUD so a frame doesn't
// take forever at the normal 115200 debug baud, then reverts afterward.
// Pixels are captured at MIRROR_SCALE (1.0 = full native resolution).
// All upscaling for display is done on the PC side (device_panel.py) -- the
// firmware only ever sends the smaller captured resolution, never scales up.
// =============================================================================

#include "screen_mirror.h"
#include "cyd_config.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

#define MIRROR_TRIGGER_BYTE 0x02
#define MIRROR_BAUD 1500000
#define MIRROR_SCALE 1.0f

static void sendMirrorFrame() {
    uint16_t fullW = (uint16_t)tft.width();
    uint16_t fullH = (uint16_t)tft.height();
    uint16_t w = (uint16_t)(fullW / MIRROR_SCALE);
    uint16_t h = (uint16_t)(fullH / MIRROR_SCALE);
    uint8_t rot = (uint8_t)tft.getRotation();

    Serial.flush();
    Serial.end();
    delay(10);
    Serial.begin(MIRROR_BAUD);
    delay(20);

    Serial.write((const uint8_t *)"EVFB", 4);
    Serial.write((uint8_t)(w & 0xFF));
    Serial.write((uint8_t)(w >> 8));
    Serial.write((uint8_t)(h & 0xFF));
    Serial.write((uint8_t)(h >> 8));
    Serial.write(rot);

    // Read back one full-resolution source row at a time (display MISO is
    // wired), then take every MIRROR_SCALE'th pixel/row and RLE-encode the
    // result. Each row's encoded bytes are batched into rowOutBuf and sent
    // with a single Serial.write() -- calling Serial.write() per 1-3 byte
    // tuple (thousands of times per frame) was the real throughput
    // bottleneck, far more than the UART bit rate itself.
    static uint16_t rowBuf[512];
    static uint8_t rowOutBuf[512 * 3];
    uint16_t safeFullWidth = fullW > 512 ? 512 : fullW;

    for (uint16_t y = 0; y < h; y++) {
        uint16_t srcY = (uint16_t)(y * MIRROR_SCALE);
        tft.readRect(0, srcY, safeFullWidth, 1, rowBuf);

        uint16_t outLen = 0;
        uint16_t x = 0;
        while (x < w) {
            uint16_t srcX = (uint16_t)(x * MIRROR_SCALE);
            if (srcX >= safeFullWidth) srcX = safeFullWidth - 1;
            uint16_t color = rowBuf[srcX];
            uint16_t runLen = 1;
            while (x + runLen < w) {
                uint16_t nextSrcX = (uint16_t)((x + runLen) * MIRROR_SCALE);
                if (nextSrcX >= safeFullWidth) nextSrcX = safeFullWidth - 1;
                if (rowBuf[nextSrcX] != color || runLen >= 255) break;
                runLen++;
            }
            rowOutBuf[outLen++] = (uint8_t)runLen;
            rowOutBuf[outLen++] = (uint8_t)(color & 0xFF);
            rowOutBuf[outLen++] = (uint8_t)(color >> 8);
            x += runLen;
        }
        Serial.write(rowOutBuf, outLen);

        if ((y & 0x0F) == 0) {
            yield();  // keep WiFi/BT stacks and the watchdog happy mid-transfer
        }
    }

    Serial.write((const uint8_t *)"FEND", 4);
    Serial.flush();
    delay(10);
    Serial.end();
    delay(10);
    Serial.begin(CYD_DEBUG_BAUD);
}

void checkScreenMirrorRequest() {
    if (Serial.available() > 0 && Serial.peek() == MIRROR_TRIGGER_BYTE) {
        Serial.read();  // consume the trigger byte
        sendMirrorFrame();
    }
}
