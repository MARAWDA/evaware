// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Jamming Detection Module Implementation
// Defensive RF spectrum monitoring — WiFi + NRF24
// Created: 2026-03-02 / Restored (WiFi + 2.4GHz only): 2026-08-24
// ═══════════════════════════════════════════════════════════════════════════
//
// WiFiGuardian:  Deauth/disassoc/beacon flood via promiscuous mode
// GHzWatchdog:   NRF24 85-ch RPD occupancy analysis
//
// GHzWatchdog reuses the shared nrf24Radio object + claim/release helpers from
// nrf24_config.cpp (same object BLE Jammer / Scanner / MouseJack use) instead
// of touching the NRF24 SPI registers directly, so it can't desync hardware
// state with those other modules.
//
// ═══════════════════════════════════════════════════════════════════════════

#include <esp_wifi.h>
#include <esp_bt.h>
#include <WiFi.h>

#include "jam_detect.h"
#include "cyd_config.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "spi_manager.h"
#include "wifi_attacks.h"
#include "nrf24_config.h"
#include "icon.h"
#include "skull_bg.h"
#include "nosifer_font.h"

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// SHARED HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Jesse's custom 16x16 skull icons — cycle through all 8 (same as every module)
static const unsigned char* jdSkulls[] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};
#define JD_NUM_SKULLS 8

// Teal-to-hotpink color interpolation (matches Scanner/Deauther/BLE Spoofer)
static uint16_t tealToHotPink(float ratio) {
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
    return tft.color565(r, g, b);
}

// 8-phase skull wave color (matches Deauther/BLE Spoofer skull animations)
static uint16_t skullWaveColor(int skullFrame, int idx) {
    int phase = (skullFrame + idx) % 8;
    float ratio;
    if (phase < 4) {
        ratio = (float)phase / 3.0f;
    } else {
        ratio = (float)(phase - 4) / 3.0f;
        ratio = 1.0f - ratio;
    }
    return tealToHotPink(ratio);
}

// Threat level text
static const char* threatText(ThreatLevel level) {
    switch (level) {
        case THREAT_CALIBRATING: return "CALIBRATING";
        case THREAT_CLEAR:       return "CLEAR";
        case THREAT_SUSPICIOUS:  return "SUSPICIOUS";
        case THREAT_JAMMING:     return "JAMMED!";
        default:                 return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR — Back (left), status area (center), Power icon (right)
// ═══════════════════════════════════════════════════════════════════════════

#define JD_ICON_SIZE 16

static void drawJdIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, JD_ICON_SIZE, JD_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(SCALE_X(210), ICON_BAR_Y, bitmap_icon_power, JD_ICON_SIZE, JD_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void drawJdIconBarStatus(const char* text) {
    tft.fillRect(SCALE_X(30), ICON_BAR_Y, SCALE_W(170), 16, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(SCALE_X(35), ICON_BAR_Y + 4);
    tft.print(text);
}

static bool isJdBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_TOUCH_TOP && ty <= ICON_BAR_TOUCH_BOTTOM && tx >= 10 && tx < 26) {
            consumeTouch();
            delay(150);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// CALIBRATION PROGRESS BAR — Teal→Hotpink gradient fill
// ═══════════════════════════════════════════════════════════════════════════

static void drawCalibrationBar(int y, int percent) {
    int barX = 20;
    int barW = SCREEN_WIDTH - 40;
    int barH = 10;

    tft.drawRect(barX - 1, y - 1, barW + 2, barH + 2, HALEHOUND_MAGENTA);

    int fillW = (barW * percent) / 100;
    for (int x = 0; x < fillW; x++) {
        float t = (float)x / (float)barW;
        tft.drawFastVLine(barX + x, y, barH, tealToHotPink(t));
    }

    if (fillW < barW) {
        tft.fillRect(barX + fillW, y, barW - fillW, barH, TFT_BLACK);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// THREAT STATUS BAR — Teal→Hotpink gradient with text overlay
// ═══════════════════════════════════════════════════════════════════════════

static void drawThreatBar(int y, ThreatLevel level, bool pulseOn, int barH = 14) {
    int barX = 5;
    int barW = SCREEN_WIDTH - 10;

    float threatRatio;
    switch (level) {
        case THREAT_CLEAR:       threatRatio = 0.0f;  break;
        case THREAT_SUSPICIOUS:  threatRatio = 0.5f;  break;
        case THREAT_JAMMING:     threatRatio = 1.0f;  break;
        default:                 threatRatio = 0.0f;  break;
    }

    for (int x = 0; x < barW; x++) {
        float t = (float)x / (float)barW;
        float brightness = 0.6f + 0.4f * sinf(t * 3.14159f);

        if (level == THREAT_JAMMING && pulseOn) {
            brightness *= 1.0f;
        } else if (level == THREAT_JAMMING) {
            brightness *= 0.4f;
        }

        uint16_t base = tealToHotPink(threatRatio);
        uint8_t r = ((base >> 11) & 0x1F) * brightness;
        uint8_t g = ((base >> 5) & 0x3F) * brightness;
        uint8_t b = (base & 0x1F) * brightness;
        tft.drawFastVLine(barX + x, y, barH, (r << 11) | (g << 5) | b);
    }

    const char* txt = threatText(level);
    int tw = strlen(txt) * 6;
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor((SCREEN_WIDTH - tw) / 2, y + 3);
    tft.print(txt);
}

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ROW ANIMATION — 8-phase wave, matches Deauther/BLE Spoofer
// ═══════════════════════════════════════════════════════════════════════════

static int jdSkullFrame = 0;

static void drawSkullMeter(int y, ThreatLevel level) {
    int numSkulls = JD_NUM_SKULLS;
    int spacing = (SCREEN_WIDTH - 20) / numSkulls;
    int startX = 10;

    for (int i = 0; i < numSkulls; i++) {
        int x = startX + (i * spacing);
        tft.fillRect(x, y, 16, 16, HALEHOUND_BLACK);

        uint16_t color;
        if (level == THREAT_CLEAR) {
            color = HALEHOUND_GUNMETAL;
        } else if (level == THREAT_SUSPICIOUS) {
            if (i < 4) {
                color = skullWaveColor(jdSkullFrame, i);
            } else {
                color = HALEHOUND_GUNMETAL;
            }
        } else if (level == THREAT_JAMMING) {
            color = skullWaveColor(jdSkullFrame, i);
        } else {
            color = HALEHOUND_GUNMETAL;
        }

        tft.drawBitmap(x, y, jdSkulls[i], 16, 16, color);
    }

    if (level >= THREAT_SUSPICIOUS) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(startX + numSkulls * spacing + 2, y + 4);
        tft.setTextSize(1);
        tft.print("!");
    }
}

#define THREAT_CLEAR_TIMEOUT_MS 3000

// ═══════════════════════════════════════════════════════════════════════════
//
//  WIFI GUARDIAN — Deauth / Disassoc / Beacon Flood Detection
//
// ═══════════════════════════════════════════════════════════════════════════

namespace WiFiGuardian {

// Promiscuous counters (volatile — updated from interrupt callback)
static volatile uint32_t deauthCount = 0;
static volatile uint32_t disassocCount = 0;
static volatile uint32_t beaconCount = 0;
static volatile int32_t  lastRssi = 0;

// Per-second rate tracking
static uint32_t prevDeauth = 0;
static uint32_t prevDisassoc = 0;
static uint32_t prevBeacon = 0;
static uint32_t deauthRate = 0;
static uint32_t disassocRate = 0;
static uint32_t beaconRate = 0;
static unsigned long lastRateCalc = 0;

// Baseline (learned during calibration)
static uint32_t baseDeauthRate = 0;
static uint32_t baseDisassocRate = 0;
static uint32_t baseBeaconRate = 0;
static uint32_t calSamples = 0;
static uint32_t calDeauthSum = 0;
static uint32_t calDisassocSum = 0;
static uint32_t calBeaconSum = 0;

// Channel hopping
static uint8_t currentChannel = 1;
static const uint8_t hopChannels[] = {1, 6, 11};
static uint8_t hopIndex = 0;
static unsigned long lastHop = 0;
#define HOP_INTERVAL_MS 500

// Threat state
static ThreatLevel threat = THREAT_CALIBRATING;
static unsigned long calStartTime = 0;
#define WIFI_CAL_DURATION_MS 5000
static unsigned long threatClearTimer = 0;

// Thresholds
#define DEAUTH_SUSPICIOUS_DELTA  5
#define DEAUTH_JAMMING_DELTA    20
#define BEACON_SUSPICIOUS_MULT   3
#define BEACON_JAMMING_MULT     10

// Event log
#define MAX_EVENTS 6
struct JdEvent {
    unsigned long timestamp;
    char msg[28];
};
static JdEvent events[MAX_EVENTS];
static int eventHead = 0;
static int eventCount = 0;

// Display
static bool initialized = false;
static volatile bool exitRequested = false;
static unsigned long lastDraw = 0;
static unsigned long lastStatusDraw = 0;
static bool pulseState = false;
static unsigned long lastPulse = 0;

// Promiscuous callback — IRAM for interrupt safety
static void IRAM_ATTR wifiPromiscCB(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    lastRssi = pkt->rx_ctrl.rssi;

    uint8_t frameType = pkt->payload[0];
    if (frameType == 0xA0) deauthCount++;
    else if (frameType == 0xC0) disassocCount++;
    else if (frameType == 0x80) beaconCount++;
}

static void addEvent(const char* msg) {
    JdEvent& e = events[eventHead];
    e.timestamp = millis() / 1000;
    strncpy(e.msg, msg, 27);
    e.msg[27] = '\0';
    eventHead = (eventHead + 1) % MAX_EVENTS;
    if (eventCount < MAX_EVENTS) eventCount++;
}

// Rate bar — full-width teal-to-hotpink gradient fill (label drawn separately above)
static void drawRateBar(int y, uint32_t rate, uint32_t baseline) {
    bool elevated = (rate > baseline + DEAUTH_SUSPICIOUS_DELTA);

    int barX = 10;
    int barW = SCREEN_WIDTH - 20;
    int barH = 14;

    tft.drawRect(barX, y, barW, barH, elevated ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA);

    uint32_t maxRate = max((uint32_t)50, baseline * 15);
    int fillW = constrain((int)((rate * (barW - 2)) / maxRate), 0, barW - 2);
    for (int x = 0; x < fillW; x++) {
        float t = (float)x / (float)(barW - 2);
        tft.drawFastVLine(barX + 1 + x, y + 1, barH - 2, tealToHotPink(t));
    }
    if (fillW < barW - 2) {
        tft.fillRect(barX + 1 + fillW, y + 1, barW - 2 - fillW, barH - 2, TFT_BLACK);
    }

    char buf[12];
    snprintf(buf, sizeof(buf), "%lu/s", (unsigned long)rate);
    int tw = strlen(buf) * 6;
    tft.setTextColor(elevated ? TFT_WHITE : HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(barX + barW - tw - 4, y + 3);
    tft.print(buf);
}

void setup() {
    if (initialized) return;

    tft.fillScreen(HALEHOUND_BLACK);
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x0041);
    drawStatusBar();
    drawJdIconBar();
    drawGlitchText(SCALE_Y(55), "GUARDIAN", &Nosifer_Regular10pt7b);

    deauthCount = 0; disassocCount = 0; beaconCount = 0;
    prevDeauth = 0; prevDisassoc = 0; prevBeacon = 0;
    deauthRate = 0; disassocRate = 0; beaconRate = 0;
    calSamples = 0; calDeauthSum = 0; calDisassocSum = 0; calBeaconSum = 0;
    baseDeauthRate = 0; baseDisassocRate = 0; baseBeaconRate = 0;
    eventHead = 0; eventCount = 0;
    jdSkullFrame = 0;

    threat = THREAT_CALIBRATING;
    exitRequested = false;
    hopIndex = 0;
    currentChannel = hopChannels[0];

    // Full radio teardown first — handles any prior WiFi/BT state
    wifiCleanup();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(wifiPromiscCB);

    calStartTime = millis();
    lastRateCalc = millis();
    lastHop = millis();
    lastDraw = 0;
    lastStatusDraw = 0;
    lastPulse = millis();
    pulseState = false;

    drawCenteredText(SCALE_Y(72), "Learning baseline...", HALEHOUND_MAGENTA, 1);

    initialized = true;
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();
    if (isJdBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    unsigned long now = millis();

    if (now - lastHop >= HOP_INTERVAL_MS) {
        hopIndex = (hopIndex + 1) % 3;
        currentChannel = hopChannels[hopIndex];
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        lastHop = now;
    }

    if (now - lastRateCalc >= 1000) {
        deauthRate = deauthCount - prevDeauth;
        disassocRate = disassocCount - prevDisassoc;
        beaconRate = beaconCount - prevBeacon;
        prevDeauth = deauthCount;
        prevDisassoc = disassocCount;
        prevBeacon = beaconCount;
        lastRateCalc = now;

        if (threat == THREAT_CALIBRATING) {
            calDeauthSum += deauthRate;
            calDisassocSum += disassocRate;
            calBeaconSum += beaconRate;
            calSamples++;

            int elapsed = now - calStartTime;
            int pct = constrain((elapsed * 100) / WIFI_CAL_DURATION_MS, 0, 100);
            drawCalibrationBar(SCALE_Y(90), pct);

            if (elapsed >= WIFI_CAL_DURATION_MS && calSamples > 0) {
                baseDeauthRate = calDeauthSum / calSamples;
                baseDisassocRate = calDisassocSum / calSamples;
                baseBeaconRate = max(calBeaconSum / calSamples, (uint32_t)1);
                threat = THREAT_CLEAR;
                threatClearTimer = now;
                addEvent("Baseline learned");

                tft.fillScreen(HALEHOUND_BLACK);
                drawStatusBar();
                drawJdIconBar();
                drawGlitchText(SCALE_Y(55), "GUARDIAN", &Nosifer_Regular10pt7b);
            }
        }

        if (threat != THREAT_CALIBRATING) {
            ThreatLevel newThreat = THREAT_CLEAR;

            uint32_t attackRate = deauthRate + disassocRate;
            uint32_t attackBase = baseDeauthRate + baseDisassocRate;
            if (attackRate > attackBase + DEAUTH_JAMMING_DELTA) {
                newThreat = THREAT_JAMMING;
                char buf[28];
                snprintf(buf, sizeof(buf), "Deauth %lu/s ch%d", (unsigned long)attackRate, currentChannel);
                addEvent(buf);
            } else if (attackRate > attackBase + DEAUTH_SUSPICIOUS_DELTA) {
                newThreat = THREAT_SUSPICIOUS;
                char buf[28];
                snprintf(buf, sizeof(buf), "Deauth %lu/s ch%d", (unsigned long)attackRate, currentChannel);
                addEvent(buf);
            }

            if (beaconRate > baseBeaconRate * BEACON_JAMMING_MULT) {
                newThreat = THREAT_JAMMING;
                char buf[28];
                snprintf(buf, sizeof(buf), "Bcn flood %lu/s", (unsigned long)beaconRate);
                addEvent(buf);
            } else if (beaconRate > baseBeaconRate * BEACON_SUSPICIOUS_MULT && newThreat < THREAT_SUSPICIOUS) {
                newThreat = THREAT_SUSPICIOUS;
            }

            if (newThreat > THREAT_CLEAR) {
                threat = newThreat;
                threatClearTimer = now;
            } else if (threat > THREAT_CLEAR && now - threatClearTimer >= THREAT_CLEAR_TIMEOUT_MS) {
                threat = THREAT_CLEAR;
                addEvent("Threat cleared");
            }
        }
    }

    if (now - lastPulse >= 100) {
        pulseState = !pulseState;
        jdSkullFrame++;
        lastPulse = now;
    }

    if (now - lastDraw >= 100 && threat != THREAT_CALIBRATING) {
        lastDraw = now;

        int y = CONTENT_Y_START + 20;

        drawThreatBar(y, threat, pulseState, 20);
        y += 24;

        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, y);
        tft.print("DEAUTH");
        y += 12;
        drawRateBar(y, deauthRate, baseDeauthRate + 1);
        y += 18;

        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(10, y);
        tft.print("DISASSOC");
        y += 12;
        drawRateBar(y, disassocRate, baseDisassocRate + 1);
        y += 18;

        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(10, y);
        tft.print("BEACON");
        y += 12;
        drawRateBar(y, beaconRate, baseBeaconRate);
        y += 18;

        for (int gx = 0; gx < SCREEN_WIDTH; gx++)
            tft.drawFastVLine(gx, y, 2, tealToHotPink((float)gx / SCREEN_WIDTH));
        y += 6;

        tft.fillRect(5, y, SCREEN_WIDTH - 10, 10, TFT_BLACK);
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, y);
        tft.printf("Ch:%d  RSSI:%d dBm", currentChannel, (int)lastRssi);
        y += 16;

        for (int gx = 0; gx < SCREEN_WIDTH; gx++)
            tft.drawFastVLine(gx, y, 2, tealToHotPink((float)gx / SCREEN_WIDTH));
        y += 6;

        int idx = (eventHead - eventCount + MAX_EVENTS) % MAX_EVENTS;
        for (int i = 0; i < 5; i++) {
            tft.fillRect(5, y + i * 14, SCREEN_WIDTH - 10, 12, TFT_BLACK);
            if (i < eventCount) {
                JdEvent& e = events[(idx + eventCount - 1 - i) % MAX_EVENTS];
                tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
                tft.setCursor(5, y + i * 14);
                tft.printf("[%lus] %s", (unsigned long)e.timestamp, e.msg);
            }
        }
        y += 5 * 14;

        for (int gx = 0; gx < SCREEN_WIDTH; gx++)
            tft.drawFastVLine(gx, y + 2, 2, tealToHotPink((float)gx / SCREEN_WIDTH));

        drawSkullMeter(SCREEN_HEIGHT - 30, threat);
    }

    if (now - lastStatusDraw >= 200 && threat != THREAT_CALIBRATING) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Ch:%d %s", currentChannel, threatText(threat));
        drawJdIconBarStatus(buf);
        lastStatusDraw = now;
    }
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    WiFi.mode(WIFI_OFF);
    initialized = false;
    exitRequested = false;
}

}  // namespace WiFiGuardian

// ═══════════════════════════════════════════════════════════════════════════
//
//  2.4GHZ WATCHDOG — NRF24 RPD Channel Occupancy Detection
//  Uses the shared nrf24Radio object (nrf24_config.cpp) so hardware state
//  stays consistent with BLE Jammer / Scanner / MouseJack.
//
// ═══════════════════════════════════════════════════════════════════════════

namespace GHzWatchdog {

#define GW_CHANNELS 85

#define GW_BAR_X       10
#define GW_BAR_Y       (CONTENT_Y_START + 4)
#define GW_BAR_W       CONTENT_INNER_W
#define GW_BAR_H       SCALE_Y(210)

#define GW_WIFI_CH1    12
#define GW_WIFI_CH6    37
#define GW_WIFI_CH11   62
#define GW_WIFI_CH13   72

// Display-level smoothed values (0-125 range, exponential decay)
static uint8_t gwDisplayLevel[GW_CHANNELS];

// Raw RPD per frame — binary 0/1 per channel (for detection logic)
static uint8_t gwRpdRaw[GW_CHANNELS];

// Calibration
static uint32_t calAccum[GW_CHANNELS];
static int calSweepCount = 0;
#define GW_CAL_DURATION_MS 5000

// Baseline: average RPD detection rate per channel (0-100)
static uint8_t gwBaseline[GW_CHANNELS];

// Per-channel elevated counter for targeted jam detection
static uint8_t gwElevated[GW_CHANNELS];

#define GW_ELEVATED_FRAMES    3
#define GW_SUSPICIOUS_CHANS   1
#define GW_JAMMING_CHANS      3
#define GW_BROADBAND_JAM_PCT  50

// State
static ThreatLevel threat = THREAT_CALIBRATING;
static unsigned long calStartTime = 0;
static unsigned long threatClearTimer = 0;
static bool initialized = false;
static bool hardwareFound = false;
static volatile bool exitRequested = false;
static unsigned long lastStatusDraw = 0;
static bool pulseState = false;
static unsigned long lastPulse = 0;

// One full 85-channel RPD sweep using the shared nrf24Radio object
static void gwScanSweep() {
    for (int ch = 0; ch < GW_CHANNELS; ch++) {
        nrf24Radio.setChannel(ch);
        delayMicroseconds(200);
        int rpd = nrf24Radio.testRPD() ? 1 : 0;
        gwDisplayLevel[ch] = (gwDisplayLevel[ch] + rpd * 125) / 2;
        gwRpdRaw[ch] = rpd;
    }
}

static uint16_t gwBarColor(int height, int maxHeight) {
    float ratio = (float)height / (float)maxHeight;
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
    return tft.color565(r, g, b);
}

static void drawGwFrame() {
    tft.drawFastVLine(GW_BAR_X - 2, GW_BAR_Y, GW_BAR_H, HALEHOUND_MAGENTA);
    tft.drawFastHLine(GW_BAR_X, GW_BAR_Y + GW_BAR_H, GW_BAR_W, HALEHOUND_MAGENTA);

    int x1  = GW_BAR_X + (GW_WIFI_CH1  * GW_BAR_W / GW_CHANNELS);
    int x6  = GW_BAR_X + (GW_WIFI_CH6  * GW_BAR_W / GW_CHANNELS);
    int x11 = GW_BAR_X + (GW_WIFI_CH11 * GW_BAR_W / GW_CHANNELS);
    int x13 = GW_BAR_X + (GW_WIFI_CH13 * GW_BAR_W / GW_CHANNELS);

    for (int y = GW_BAR_Y; y < GW_BAR_Y + GW_BAR_H; y += 6) {
        tft.drawPixel(x1,  y, HALEHOUND_HOTPINK);
        tft.drawPixel(x6,  y, HALEHOUND_HOTPINK);
        tft.drawPixel(x11, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x13, y, HALEHOUND_VIOLET);
    }

    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x1 - 2, GW_BAR_Y - 10);
    tft.print("1");
    tft.setCursor(x6 - 2, GW_BAR_Y - 10);
    tft.print("6");
    tft.setCursor(x11 - 6, GW_BAR_Y - 10);
    tft.print("11");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(x13 - 6, GW_BAR_Y - 10);
    tft.print("13");

    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(GW_BAR_X - 5, GW_BAR_Y + GW_BAR_H + 4);
    tft.print("2400");
    tft.setCursor(GW_BAR_X + GW_BAR_W / 2 - 12, GW_BAR_Y + GW_BAR_H + 4);
    tft.print("2442");
    tft.setCursor(GW_BAR_X + GW_BAR_W - 28, GW_BAR_Y + GW_BAR_H + 4);
    tft.print("2484");

    tft.drawFastHLine(0, GW_BAR_Y + GW_BAR_H + 16, SCREEN_WIDTH, HALEHOUND_HOTPINK);
}

static void drawGwBarGraph() {
    tft.fillRect(GW_BAR_X, GW_BAR_Y, GW_BAR_W, GW_BAR_H, TFT_BLACK);

    int x1  = GW_BAR_X + (GW_WIFI_CH1  * GW_BAR_W / GW_CHANNELS);
    int x6  = GW_BAR_X + (GW_WIFI_CH6  * GW_BAR_W / GW_CHANNELS);
    int x11 = GW_BAR_X + (GW_WIFI_CH11 * GW_BAR_W / GW_CHANNELS);
    int x13 = GW_BAR_X + (GW_WIFI_CH13 * GW_BAR_W / GW_CHANNELS);

    for (int y = GW_BAR_Y; y < GW_BAR_Y + GW_BAR_H; y += 6) {
        tft.drawPixel(x1,  y, HALEHOUND_HOTPINK);
        tft.drawPixel(x6,  y, HALEHOUND_HOTPINK);
        tft.drawPixel(x11, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x13, y, HALEHOUND_VIOLET);
    }

    int peakChannel = 0;
    uint8_t peakLevel = 0;

    for (int ch = 0; ch < GW_CHANNELS; ch++) {
        uint8_t level = gwDisplayLevel[ch];

        if (level > peakLevel) {
            peakLevel = level;
            peakChannel = ch;
        }

        if (level > 0) {
            int x = GW_BAR_X + (ch * GW_BAR_W / GW_CHANNELS);
            int barH = (level * GW_BAR_H) / 125;
            if (barH > GW_BAR_H) barH = GW_BAR_H;
            if (barH < 4 && level > 0) barH = 4;

            int barY = GW_BAR_Y + GW_BAR_H - barH;

            for (int py = 0; py < barH; py++) {
                uint16_t color = gwBarColor(py, GW_BAR_H);
                tft.drawFastHLine(x, barY + barH - 1 - py, 2, color);
            }
        }
    }

    int statusY = GW_BAR_Y + GW_BAR_H + 6;

    tft.drawFastHLine(0, statusY - 2, SCREEN_WIDTH, HALEHOUND_HOTPINK);

    drawThreatBar(statusY, threat, pulseState);
    statusY += 16;

    int peakFreq = 2400 + peakChannel;
    int activeCount = 0;
    for (int ch = 0; ch < GW_CHANNELS; ch++) {
        if (gwRpdRaw[ch] > 0) activeCount++;
    }
    int activePct = (activeCount * 100) / GW_CHANNELS;

    tft.fillRect(5, statusY + 2, SCREEN_WIDTH - 10, 10, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(5, statusY + 2);
    tft.print("PEAK:");
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.printf(" %d", peakFreq);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.print("MHz");

    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(SCALE_X(140), statusY + 2);
    tft.printf("%d/%d", activeCount, GW_CHANNELS);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.printf(" (%d%%)", activePct);

    statusY += 14;

    int skullStartX = 10;
    int skullSpacing = SCALE_X(28);

    int litSkulls = (peakLevel * 8) / 4;
    if (litSkulls > JD_NUM_SKULLS) litSkulls = JD_NUM_SKULLS;

    for (int i = 0; i < JD_NUM_SKULLS; i++) {
        int sx = skullStartX + (i * skullSpacing);
        tft.fillRect(sx, statusY, 16, 16, HALEHOUND_BLACK);

        if (i < litSkulls && peakLevel > 0) {
            tft.drawBitmap(sx, statusY, jdSkulls[i], 16, 16,
                skullWaveColor(jdSkullFrame, i));
        } else {
            tft.drawBitmap(sx, statusY, jdSkulls[i], 16, 16, HALEHOUND_GUNMETAL);
        }
    }

    jdSkullFrame++;

    int pctX = skullStartX + (JD_NUM_SKULLS * skullSpacing) + 2;
    tft.fillRect(pctX, statusY + 4, 30, 10, TFT_BLACK);
    int pct = (peakLevel * 100) / 125;
    if (pct > 100) pct = 100;
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(pctX, statusY + 4);
    tft.printf("%d%%", pct);
}

void setup() {
    if (initialized) return;

    tft.fillScreen(HALEHOUND_BLACK);
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x0041);
    drawStatusBar();
    drawJdIconBar();
    drawGlitchText(SCALE_Y(55), "WATCHDOG", &Nosifer_Regular10pt7b);

    if (!nrf24IsActive() && !nrf24Setup()) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        drawCenteredText(100, "NRF24 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check SPI wiring", HALEHOUND_MAGENTA, 1);
        delay(2000);
        hardwareFound = false;
        exitRequested = true;
        initialized = true;
        return;
    }
    hardwareFound = true;

    nrf24Radio.setAutoAck(false);
    nrf24Radio.disableCRC();
    nrf24Radio.startListening();

    memset(gwDisplayLevel, 0, sizeof(gwDisplayLevel));
    memset(gwRpdRaw, 0, sizeof(gwRpdRaw));
    memset(calAccum, 0, sizeof(calAccum));
    memset(gwBaseline, 0, sizeof(gwBaseline));
    memset(gwElevated, 0, sizeof(gwElevated));
    calSweepCount = 0;

    threat = THREAT_CALIBRATING;
    exitRequested = false;
    calStartTime = millis();
    threatClearTimer = millis();
    lastStatusDraw = 0;
    lastPulse = millis(); pulseState = false;

    drawCenteredText(SCALE_Y(72), "Calibrating 2.4GHz...", HALEHOUND_MAGENTA, 1);

    initialized = true;
}

void loop() {
    if (!initialized) return;
    if (!hardwareFound) return;

    touchButtonsUpdate();
    if (isJdBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    unsigned long now = millis();

    if (now - lastPulse >= 300) {
        pulseState = !pulseState;
        lastPulse = now;
    }

    gwScanSweep();

    if (threat == THREAT_CALIBRATING) {
        for (int ch = 0; ch < GW_CHANNELS; ch++) calAccum[ch] += gwRpdRaw[ch];
        calSweepCount++;

        int elapsed = now - calStartTime;
        int pct = constrain((elapsed * 100) / GW_CAL_DURATION_MS, 0, 100);
        drawCalibrationBar(SCALE_Y(90), pct);

        if (elapsed >= GW_CAL_DURATION_MS && calSweepCount >= 10) {
            for (int ch = 0; ch < GW_CHANNELS; ch++) {
                gwBaseline[ch] = (calAccum[ch] * 100) / calSweepCount;
            }
            threat = THREAT_CLEAR;
            threatClearTimer = now;
            memset(gwElevated, 0, sizeof(gwElevated));

            tft.fillScreen(HALEHOUND_BLACK);
            drawStatusBar();
            drawJdIconBar();
            drawGwFrame();
        }
    } else {
        int flaggedChans = 0;
        int rawActive = 0;

        for (int ch = 0; ch < GW_CHANNELS; ch++) {
            if (gwRpdRaw[ch]) rawActive++;

            if (gwRpdRaw[ch] && gwBaseline[ch] < 30) {
                if (gwElevated[ch] < 255) gwElevated[ch]++;
            } else {
                if (gwElevated[ch] > 0) gwElevated[ch]--;
            }

            if (gwElevated[ch] >= GW_ELEVATED_FRAMES) flaggedChans++;
        }

        int rawPct = (rawActive * 100) / GW_CHANNELS;

        ThreatLevel newThreat = THREAT_CLEAR;
        if (flaggedChans >= GW_JAMMING_CHANS || rawPct >= GW_BROADBAND_JAM_PCT) {
            newThreat = THREAT_JAMMING;
        } else if (flaggedChans >= GW_SUSPICIOUS_CHANS) {
            newThreat = THREAT_SUSPICIOUS;
        }

        if (newThreat > THREAT_CLEAR) {
            threat = newThreat;
            threatClearTimer = now;
        } else if (threat > THREAT_CLEAR && now - threatClearTimer >= THREAT_CLEAR_TIMEOUT_MS) {
            threat = THREAT_CLEAR;
        }

        drawGwBarGraph();
    }

    if (now - lastStatusDraw >= 200 && threat != THREAT_CALIBRATING) {
        lastStatusDraw = now;
        int activeCount = 0;
        for (int ch = 0; ch < GW_CHANNELS; ch++) {
            if (gwRpdRaw[ch]) activeCount++;
        }
        char buf[24];
        snprintf(buf, sizeof(buf), "%d%% %s", (activeCount * 100) / GW_CHANNELS, threatText(threat));
        drawJdIconBarStatus(buf);
    }
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    if (hardwareFound) {
        nrf24Radio.stopListening();
        nrf24Shutdown();
    }
    initialized = false;
    exitRequested = false;
    hardwareFound = false;
}

}  // namespace GHzWatchdog
