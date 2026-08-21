#ifndef BADUSB_ATTACKS_H
#define BADUSB_ATTACKS_H

// ═══════════════════════════════════════════════════════════════════════════
// EVAWARE BadUSB Module
// BLE HID keystroke injection with a curated library of 10 premade payloads
// Created: 2026-08-20
// ═══════════════════════════════════════════════════════════════════════════
//
// HONEST HARDWARE NOTE:
// This board's ESP32 (WROOM-32, non-S2/S3) has no native USB device controller,
// so it cannot emulate a wired USB HID keyboard the way a real "Rubber Ducky" or
// "O.MG Cable" does over a physical USB port. This module instead pairs as a
// Bluetooth LE HID keyboard (same mechanism as the existing BLE Ducky feature)
// and injects keystrokes wirelessly once a target device pairs with it. It is
// kept as its own menu section — separate from BLE Ducky — with a larger,
// curated payload library (10 premade scripts) and its own dedicated UI.
//
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

namespace BadUSB {

// Initialize BadUSB screen + BLE HID keyboard
void setup();

// Main loop function - call repeatedly
void loop();

// Check if user requested exit
bool isExitRequested();

// Cleanup and release BLE
void cleanup();

}  // namespace BadUSB

#endif // BADUSB_ATTACKS_H
