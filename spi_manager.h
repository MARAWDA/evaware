#ifndef SPI_MANAGER_H
#define SPI_MANAGER_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD SPI Bus Manager
// Manages shared VSPI bus between SD Card, CC1101, NRF24L01, and PN532
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════
//
// VSPI BUS SHARING:
// ┌──────────────────────────────────────────────────────────────────────────┐
// │                         VSPI BUS (GPIO 18/19/23)                         │
// ├──────────┬─────────┬─────────────────────────────────────────────────────┤
// │ Device   │ CS Pin  │ Usage                                               │
// ├──────────┼─────────┼─────────────────────────────────────────────────────┤
// │ SD Card  │ GPIO 5  │ DuckyScript payloads, logs, wardriving data        │
// │ CC1101   │ GPIO 27 │ Kept for hardware, no attack features use it        │
// │ NRF24    │ GPIO 4  │ BLE Jammer only (2.4GHz carrier)                    │
// └──────────┴─────────┴─────────────────────────────────────────────────────┘
//
// RULES:
// 1. Only ONE device can be active at a time
// 2. Before using a device, call spiSelect(device)
// 3. When done, call spiDeselect() to release the bus
// 4. CS pins: LOW = selected, HIGH = deselected
//
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <SPI.h>
#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// SPI DEVICE IDENTIFIERS
// ═══════════════════════════════════════════════════════════════════════════

enum SPIDevice {
    SPI_DEVICE_NONE,      // No device selected (all CS HIGH)
    SPI_DEVICE_SD,        // SD Card (GPIO 5)
    SPI_DEVICE_CC1101,    // CC1101 SubGHz (GPIO 27)
    SPI_DEVICE_NRF24      // NRF24L01 2.4GHz (GPIO 4)
};

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

// Initialize SPI bus and all CS pins
// Call this once in setup() before using any SPI device
void spiManagerSetup();

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE SELECTION
// ═══════════════════════════════════════════════════════════════════════════

// Select a device (deselects all others first)
// Returns true if device was selected successfully
// Returns false if device is disabled in config
bool spiSelect(SPIDevice device);

// Deselect all devices (all CS pins HIGH)
// Call this when done with SPI operations
void spiDeselect();

// Get currently selected device
SPIDevice spiGetSelected();

// Check if a specific device is currently selected
bool spiIsSelected(SPIDevice device);

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE-SPECIFIC HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Select SD card for file operations
// Returns true if SD card is enabled and selected
bool spiSelectSD();

// Select CC1101 for SubGHz operations
// Returns true if CC1101 is enabled and selected
bool spiSelectCC1101();

// Select NRF24 for 2.4GHz operations
// Returns true if NRF24 is enabled and selected
bool spiSelectNRF24();

// ═══════════════════════════════════════════════════════════════════════════
// BUS LOCKING (for multi-step operations)
// ═══════════════════════════════════════════════════════════════════════════

// Lock the bus for exclusive use by current device
// Prevents other code from switching devices mid-operation
void spiLock();

// Unlock the bus
void spiUnlock();

// Check if bus is locked
bool spiIsLocked();

// ═══════════════════════════════════════════════════════════════════════════
// SPI SETTINGS FOR EACH DEVICE
// ═══════════════════════════════════════════════════════════════════════════

// Get optimal SPI settings for each device
// Use with SPI.beginTransaction(spiGetSettings(device))
SPISettings spiGetSettings(SPIDevice device);

// Default SPI speeds (can be adjusted if needed)
#define SPI_SPEED_SD        4000000   // 4 MHz for SD card
#define SPI_SPEED_CC1101    4000000   // 4 MHz for CC1101
#define SPI_SPEED_NRF24     8000000   // 8 MHz for NRF24 (can handle 10MHz)

// ═══════════════════════════════════════════════════════════════════════════
// DEBUG
// ═══════════════════════════════════════════════════════════════════════════

// Print current SPI bus state to Serial
void spiPrintStatus();

#endif // SPI_MANAGER_H
