// ═══════════════════════════════════════════════════════════════════════════
// EVAWARE BadUSB Module Implementation
// BLE HID keystroke injection — runs script files straight off the SD card
// Created: 2026-08-20 · Rebuilt: 2026-08-21 (SD-only rewrite)
//
// PLAIN-ENGLISH OVERVIEW
// Same trick as BLE Ducky: the board pairs as a Bluetooth keyboard, and once
// a computer accepts the pairing, this module "types" whatever text file you
// pick from the SD card's /badusb folder at machine speed. There are no
// built-in payloads — put your own script on the SD card and run it.
// ═══════════════════════════════════════════════════════════════════════════

#include "badusb_attacks.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "icon.h"
#include "skull_bg.h"
#include "rei_bg.h"
#include "nosifer_font.h"
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BleKeyboard.h>
#include "esp_bt.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>

extern TFT_eSPI tft;

namespace BadUSB {

// HaleHound only uses BLE — release Classic BT memory once before BLE init,
// otherwise the BT controller can crash on repeated init/deinit cycles.
static bool buClassicBtReleased = false;
static void releaseClassicBtMemory() {
    if (!buClassicBtReleased) {
        esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (err == ESP_OK) {
            buClassicBtReleased = true;
        }
    }
}

// ── State ─────────────────────────────────────────────────────────────────
struct BuState {
    bool     connected;
    bool     injecting;
    uint32_t bytesTotal;
    uint32_t bytesSent;
};

static BuState* bu = nullptr;
static BleKeyboard* bleKb = nullptr;
static bool buExitRequested = false;
static unsigned long buLastDisplay = 0;
static bool buBleInitialized = false;
static bool buDirty = true;

#define BU_SD_DIR "/badusb"
#define BU_MAX_SD_FILES 20
static char buSdFiles[BU_MAX_SD_FILES][32];
static int buSdFileCount = 0;
static int buSdFileIndex = 0;
static bool buSdReady = false;
static char buStatusMsg[48] = "";

// ── Icon bar: Back | Start/Stop | Prev | Next ──────────────────────────────
#define BU_ICON_NUM 4
static const int buIconX[BU_ICON_NUM] = {10, SCALE_X(70), SCALE_X(105), SCALE_X(140)};
static const unsigned char* const buIcons[BU_ICON_NUM] = {
    bitmap_icon_go_back,
    bitmap_icon_start,
    bitmap_icon_LEFT,
    bitmap_icon_RIGHT
};

static void drawBuIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    for (int i = 0; i < BU_ICON_NUM; i++) {
        tft.drawBitmap(buIconX[i], ICON_BAR_Y, buIcons[i], 16, 16, HALEHOUND_MAGENTA);
    }
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void setStatus(const char* msg) {
    strncpy(buStatusMsg, msg, sizeof(buStatusMsg) - 1);
    buStatusMsg[sizeof(buStatusMsg) - 1] = '\0';
}

static bool isAllowedScriptExt(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".txt") || lower.endsWith(".ducky") || lower.endsWith(".ps1") ||
           lower.endsWith(".cmd") || lower.endsWith(".sh");
}

// ── Scan /badusb for script files ──────────────────────────────────────────
static bool scanSdScripts() {
    buSdFileCount = 0;

    File dir = SD.open(BU_SD_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        setStatus("Missing /badusb directory");
        return false;
    }

    File entry = dir.openNextFile();
    while (entry && buSdFileCount < BU_MAX_SD_FILES) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);

            if (name.length() > 0 && isAllowedScriptExt(name)) {
                strncpy(buSdFiles[buSdFileCount], name.c_str(), sizeof(buSdFiles[buSdFileCount]) - 1);
                buSdFiles[buSdFileCount][sizeof(buSdFiles[buSdFileCount]) - 1] = '\0';
                buSdFileCount++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    if (buSdFileCount <= 0) {
        buSdFileIndex = 0;
        setStatus("No scripts in /badusb");
        return false;
    }

    if (buSdFileIndex >= buSdFileCount) buSdFileIndex = 0;
    setStatus("READY - Press START to run");
    return true;
}

static bool ensureSdAndScripts() {
    if (!buSdReady) {
        buSdReady = SD.begin(SD_CS);
        if (!buSdReady) {
            buSdReady = SD.begin(SD_CS, SPI, 4000000);
        }
        if (!buSdReady) {
            setStatus("SD mount failed");
            return false;
        }
    }

    return scanSdScripts();
}

// ── Stream the selected file straight to the BLE keyboard ─────────────────
static void runSelectedScript() {
    if (!bu || !bleKb || !bleKb->isConnected()) return;
    if (buSdFileIndex < 0 || buSdFileIndex >= buSdFileCount) return;

    String path = String(BU_SD_DIR) + "/" + buSdFiles[buSdFileIndex];
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
        setStatus("Open failed");
        return;
    }

    bu->bytesTotal = f.size();
    bu->bytesSent = 0;

    while (f.available() && bu->injecting) {
        if (!bleKb->isConnected()) {
            bu->connected = false;
            setStatus("Disconnected mid-run");
            break;
        }
        char c = (char)f.read();
        if (c != '\r') {
            bleKb->write((uint8_t)c);
        }
        bu->bytesSent++;
        delay(50);  // BLE HID needs spacing between reports, matches BLE Ducky
    }

    f.close();
    bool disconnectedEarly = !bleKb->isConnected();
    bu->injecting = false;
    if (!disconnectedEarly) {
        setStatus(bu->bytesSent >= bu->bytesTotal ? "RUN COMPLETE" : "STOPPED");
    }
}

// ── Draw the screen ─────────────────────────────────────────────────────
static void drawBuDisplay() {
    tft.fillScreen(HALEHOUND_BLACK);
    tft.drawBitmap(0, 0, rei_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x0861);
    drawStatusBar();
    drawGlitchText(SCALE_Y(55), "BLU-USB", &Nosifer_Regular10pt7b);
    drawBuIconBar();

    int y = SCALE_Y(75);
    tft.setTextColor(bu->connected ? HALEHOUND_GREEN : HALEHOUND_GUNMETAL, HALEHOUND_BLACK);
    tft.setCursor(10, y);
    tft.print(bu->connected ? "PAIRED" : "WAITING FOR PAIRING...");
    y += 14;

    tft.drawLine(10, y, SCREEN_WIDTH - 10, y, HALEHOUND_HOTPINK);
    y += 6;

    tft.setTextColor(HALEHOUND_HOTPINK, HALEHOUND_BLACK);
    tft.setCursor(10, y);
    tft.print("SD SCRIPTS  /badusb");
    y += 12;

    if (buSdFileCount == 0) {
        tft.setCursor(20, y);
        tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_BLACK);
        tft.print("No scripts found");
        y += 12;
    } else {
        for (int i = 0; i < buSdFileCount; i++) {
            tft.setCursor(16, y);
            tft.setTextColor(i == buSdFileIndex ? HALEHOUND_BRIGHT : HALEHOUND_GUNMETAL,
                             HALEHOUND_BLACK);
            tft.print(i == buSdFileIndex ? "> " : "  ");
            tft.print(buSdFiles[i]);
            y += 12;
        }
    }

    y += 4;
    tft.drawLine(10, y, SCREEN_WIDTH - 10, y, HALEHOUND_HOTPINK);
    y += 6;

    if (bu->injecting) {
        tft.setTextColor(HALEHOUND_HOTPINK, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.print("RUNNING...");
        y += 14;

        int barX = 10;
        int barW = SCREEN_WIDTH - 20;
        int barH = SCALE_H(14);
        tft.drawRect(barX, y, barW, barH, HALEHOUND_MAGENTA);
        int progress = 0;
        if (bu->bytesTotal > 0) {
            progress = (int)((bu->bytesSent * (uint32_t)(barW - 2)) / bu->bytesTotal);
        }
        tft.fillRect(barX + 1, y + 1, progress, barH - 2, HALEHOUND_HOTPINK);
        y += barH + 6;

        tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.printf("SENT: %u/%u bytes", (unsigned)bu->bytesSent, (unsigned)bu->bytesTotal);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.print(buStatusMsg[0] != '\0' ? buStatusMsg : "Select a script and press START");
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    buExitRequested = false;
    buLastDisplay = 0;
    buBleInitialized = false;
    buDirty = true;
    buSdReady = false;
    buSdFileCount = 0;
    buSdFileIndex = 0;
    buStatusMsg[0] = '\0';

    if (bu) { free(bu); bu = nullptr; }
    bu = (BuState*)calloc(1, sizeof(BuState));
    if (!bu) {
        tft.fillScreen(TFT_BLACK);
        drawCenteredText(120, "HEAP ALLOC FAILED", HALEHOUND_HOTPINK, 2);
        return;
    }

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawGlitchText(SCALE_Y(55), "BLU-USB", &Nosifer_Regular10pt7b);
    drawBuIconBar();

    esp_wifi_stop();
    delay(100);

    releaseClassicBtMemory();

    if (bleKb) { delete bleKb; bleKb = nullptr; }
    bleKb = new BleKeyboard("EVAWARE Blu-USB", "EVAWARE", 100);
    bleKb->begin();
    delay(150);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    ensureSdAndScripts();

    buBleInitialized = true;
    drawBuDisplay();

    #if CYD_DEBUG
    Serial.println("[BADUSB] Setup complete, advertising as 'EVAWARE Blu-USB'");
    Serial.printf("[BADUSB] Free heap: %u\n", ESP.getFreeHeap());
    #endif
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
    if (!bu || !bleKb) {
        // Setup failed to allocate — still let the user back out.
        touchButtonsUpdate();
        uint16_t ttx, tty;
        if (getTouchPoint(&ttx, &tty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            buExitRequested = true;
        }
        return;
    }

    if (millis() - buLastDisplay >= 250) {
        bool wasConnected = bu->connected;
        bu->connected = bleKb->isConnected();
        if (bu->connected != wasConnected) {
            buDirty = true;
            #if CYD_DEBUG
            Serial.printf("[BADUSB] %s\n", bu->connected ? "DEVICE CONNECTED" : "DEVICE DISCONNECTED");
            #endif
        }
    }

    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_TOUCH_TOP && ty <= ICON_BAR_TOUCH_BOTTOM) {
            // Back
            if (tx < 40) {
                buExitRequested = true;
                return;
            }
            // Run / Stop
            if (tx >= buIconX[1] - 10 && tx < buIconX[1] + 25) {
                waitForTouchRelease();
                delay(200);
                if (bu->injecting) {
                    bu->injecting = false;
                } else if (bu->connected && buSdFileCount > 0) {
                    bu->injecting = true;
                    bu->bytesSent = 0;
                    bu->bytesTotal = 0;
                    buDirty = true;
                    drawBuDisplay();
                    runSelectedScript();
                }
                buDirty = true;
                return;
            }
            // Previous script
            if (tx >= buIconX[2] - 10 && tx < buIconX[2] + 25) {
                waitForTouchRelease();
                delay(200);
                if (buSdFileCount > 0) {
                    buSdFileIndex = (buSdFileIndex - 1 + buSdFileCount) % buSdFileCount;
                    setStatus("READY - Press START to run");
                }
                buDirty = true;
                return;
            }
            // Next script
            if (tx >= buIconX[3] - 10 && tx < buIconX[3] + 25) {
                waitForTouchRelease();
                delay(200);
                if (buSdFileCount > 0) {
                    buSdFileIndex = (buSdFileIndex + 1) % buSdFileCount;
                    setStatus("READY - Press START to run");
                }
                buDirty = true;
                return;
            }
        }

        // Tap a file row to select it directly.
        int listY = SCALE_Y(75) + 14 + 6 + 12;
        if (tx >= 10 && tx <= SCREEN_WIDTH - 10 && ty >= listY - 2 &&
            ty < listY + (buSdFileCount * 12)) {
            int idx = (ty - listY) / 12;
            if (idx >= 0 && idx < buSdFileCount && idx != buSdFileIndex) {
                waitForTouchRelease();
                delay(150);
                buSdFileIndex = idx;
                setStatus("READY - Press START to run");
                buDirty = true;
                return;
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        buExitRequested = true;
        return;
    }

    if (buDirty && millis() - buLastDisplay >= 100) {
        drawBuDisplay();
        buLastDisplay = millis();
        buDirty = false;
    }
}

bool isExitRequested() {
    return buExitRequested;
}

void cleanup() {
    if (bu) bu->injecting = false;

    if (bleKb) {
        bleKb->end();
        delete bleKb;
        bleKb = nullptr;
    }

    if (buBleInitialized) {
        BLEDevice::deinit(false);
        buBleInitialized = false;
    }

    if (bu) { free(bu); bu = nullptr; }
    buExitRequested = false;

    #if CYD_DEBUG
    Serial.println("[BADUSB] Cleanup complete");
    #endif
}

}  // namespace BadUSB
