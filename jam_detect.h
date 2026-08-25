#ifndef JAM_DETECT_H
#define JAM_DETECT_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Jamming Detection Module
// Defensive RF spectrum monitoring — WiFi + NRF24 (no SubGHz, no Full Spectrum)
// Created: 2026-03-02 / Restored (WiFi + 2.4GHz only): 2026-08-24
// ═══════════════════════════════════════════════════════════════════════════
//
// DETECTION MODULES:
// ┌──────────────────────────────────────────────────────────────────────────┐
// │ WiFiGuardian   - Deauth/disassoc/beacon flood detection (promiscuous)   │
// │ GHzWatchdog    - NRF24 RPD channel occupancy (2400-2484 MHz)            │
// └──────────────────────────────────────────────────────────────────────────┘
//
// THREAT LEVELS:
//   CALIBRATING → CLEAR → SUSPICIOUS → JAMMING
//                   ↑         ↓            ↓
//                   └─── (3s clear) ───────┘
//
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// SHARED THREAT LEVEL ENUM
// ═══════════════════════════════════════════════════════════════════════════

enum ThreatLevel {
    THREAT_CALIBRATING = 0,
    THREAT_CLEAR,
    THREAT_SUSPICIOUS,
    THREAT_JAMMING
};

// ═══════════════════════════════════════════════════════════════════════════
// WIFI GUARDIAN — Deauth / Disassoc / Beacon Flood Detection
// Uses ESP32 built-in WiFi in promiscuous mode (no external radio)
// ═══════════════════════════════════════════════════════════════════════════

namespace WiFiGuardian {

// Initialize promiscuous mode, draw UI, start calibration
void setup();

// Main loop — called repeatedly from feature runner
void loop();

// Check if user requested exit
bool isExitRequested();

// Cleanup — disable promiscuous mode, release WiFi
void cleanup();

}  // namespace WiFiGuardian

// ═══════════════════════════════════════════════════════════════════════════
// 2.4GHZ WATCHDOG — NRF24 RPD Channel Occupancy Detection
// 85-channel RPD sweep, baseline comparison
// ═══════════════════════════════════════════════════════════════════════════

namespace GHzWatchdog {

// Initialize NRF24, draw UI, start calibration
void setup();

// Main loop — called repeatedly from feature runner
void loop();

// Check if user requested exit
bool isExitRequested();

// Cleanup — release NRF24/SPI
void cleanup();

}  // namespace GHzWatchdog

#endif // JAM_DETECT_H
