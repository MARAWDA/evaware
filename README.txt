# EVAWARE - ESP32 Offensive Security Toolkit

> **Educational & Experimental Firmware for the Cheap Yellow Display (CYD)**

---

## 📌 Overview

**Evaware** is a multi-protocol offensive security research toolkit built for the ESP32 "Cheap Yellow Display" (CYD) platform. This firmware serves as my personal learning sandbox—a place to understand wireless security, RF protocols, and embedded systems development.

**Based on:** The foundation of this project comes from the incredible work by **Jesse C. Hale** ([HaleHound-CYD](https://github.com/JesseCHale/HaleHound-CYD)). I'm building upon that solid base to learn, experiment, and contribute back to the community. ❤️

> *"Love you Jesse <3 You inspire me"*

---

## ⚠️ Legal & Responsible Use

> **IMPORTANT: READ THIS BEFORE USING**

This firmware is provided **STRICTLY FOR**:

- ✅ Educational purposes and learning about wireless security
- ✅ Authorized security testing on systems you own or have explicit permission to test
- ✅ Security research in controlled lab environments
- ✅ Ham radio and RF experimentation (compliance with local regulations required)

**THIS FIRMWARE IS NOT FOR:**

- ❌ Unauthorized network access or disruption
- ❌ Jamming communications or interfering with services
- ❌ Any illegal or malicious activities
- ❌ Use in jurisdictions where RF security tools are restricted

> **YOU ARE RESPONSIBLE FOR YOUR ACTIONS.** The authors assume no liability for misuse, damage, or legal consequences.

---

## 🛠️ Hardware Requirements

### Base Board (Choose One)

| Component | CYD 2.8" (ESP32-2432S028) | QDtech E32R35T (3.5") |
|-----------|---------------------------|----------------------|
| MCU | ESP32-WROOM-32 / 32UE | ESP32-WROOM-32 |
| Display | 2.8" ILI9341 240x320 | 3.5" ST7796 320x480 |
| Touch | XPT2046 Resistive | XPT2046 Resistive |
| Flash | 4MB+ recommended | 4MB+ recommended |
| USB | CH340C (Micro-USB/USB-C) | CH340C (USB-C) |
| SD Card | Built-in MicroSD | Built-in MicroSD |
| Backlight | GPIO 21 | GPIO 27 |

### Required External Modules (For Full Functionality)

| Module | Model | Purpose |
|--------|-------|---------|
| Sub-GHz Radio | CC1101 (HW-863 red board) | 300-928 MHz capture/replay/jamming |
| 2.4GHz Radio | NRF24L01+PA+LNA | WiFi/BLE jamming, MouseJack |
| NFC/RFID | PN532 V3 | 13.56MHz scanning/cloning |
| GPS | GT-U7 or NEO-6M | Wardriving, location logging |

### Optional Components
- 10uF capacitor (NRF24 power stability)
- MicroSD card (payload storage, PCAP saves)
- LiPo battery + boost converter (portable operation)

---

## 🚀 Features

### WiFi Module
- **Packet Monitor** — Real-time 802.11 frame capture
- **Beacon Spammer** — Flood SSIDs (custom or random)
- **WiFi Deauther** — Target deauth with network scan
- **Probe Sniffer** — Capture probe requests for Evil Twin
- **WiFi Scanner** — Scan APs → Tap-to-Deauth/Clone
- **Captive Portal** — Evil Twin credential harvesting
- **Station Scanner** — Enumerate connected clients
- **Auth Flood** — 802.11 auth frame flood attack

### Bluetooth
- **BLE Jammer** — 2.4GHz BLE channel flood (NRF24)
- **BLE Spoofer** — Multi-platform BLE spam engine
- **BLE Beacon** — iBeacon / Eddystone transmitter
- **Sniffer** — Passive BLE advertisement monitor
- **BLE Scanner** — Discover nearby BLE devices
- **WhisperPair** — CVE-2025-36911 Fast Pair exploit
- **AirTag Hub** — FindMy attack suite (Detect/Phantom/Replay/Find You)
- **Lunatic Fringe** — Multi-platform tracker scanner
- **BLE Ducky** — BLE HID keyboard injection

### 2.4GHz (NRF24)
- **Scanner** — Channel activity scanner
- **Spectrum Analyzer** — Visual RF spectrum display
- **NRF Sniffer** — Promiscuous 2.4GHz packet capture
- **MouseJack** — Wireless keyboard injection
- **WLAN Jammer** — 2.4GHz broadband disruption
- **Proto Kill** — Multi-protocol attack suite

### SIGINT (Signal Intelligence)
- **EAPOL Capture** — WPA handshake/PMKID capture
- **Karma Attack** — Auto-respond to probe requests
- **Wardriving** — GPS-tagged AP scanning
- **Saved Captures** — Browse captured handshakes

### Defensive Tools
- **Jam Detect** — Detect WiFi/BLE/Sub-GHz jamming attacks
  - WiFi Guardian — Detect 802.11 deauth floods
  - BLE Watchdog — Detect BLE advertisement floods
  - SubGHz Sentinel — Detect SubGHz carrier jamming

### Utilities
- **Serial Monitor** — UART passthrough terminal
- **Update Firmware** — Flash .bin from SD card
- **Touch Calibrate** — Touchscreen recalibration
- **GPS** — Live satellite view & NMEA data
- **Radio Test** — SPI radio hardware verification
- **Settings** — Brightness, timeout, colors, rotation, PIN lock

---

## 📱 Supported Boards

| Board | Build Target | Display | Status |
|-------|-------------|---------|--------|
| ESP32-2432S028 (2.8") | `esp32-cyd` | 240x320 ILI9341 | ✅ Fully Tested |
| QDtech E32R35T (3.5") | `esp32-e32r35t` | 320x480 ST7796 | ✅ Fully Tested |
| QDtech E32R28T (2.8") | `esp32-e32r28t` | 240x320 ILI9341 | ✅ Supported |

### Build Commands
```bash
pio run -e esp32-cyd       # 2.8" CYD
pio run -e esp32-e32r35t   # E32R35T 3.5"
pio run -e esp32-e32r28t   # E32R28T 2.8"
```

---

## 📂 Project Structure

```
evaware/
├── src/
│   ├── main.cpp              # Entry point
│   ├── cyd_config.h          # Hardware configuration
│   ├── menu/                 # UI menu system
│   ├── modules/              # Attack modules
│   │   ├── wifi/
│   │   ├── bluetooth/
│   │   ├── nrf24/
│   │   ├── sigint/
│   │   └── tools/
│   ├── drivers/              # Hardware drivers
│   └── utils/                # Helper functions
├── include/                  # Header files
├── lib/                      # External libraries
├── data/                     # SD card assets
├── platformio.ini           # PlatformIO configuration
├── LICENSE
└── README.md
```

---

## 🧠 Learning Journey

I'm documenting my learning process as I go. Some things I've learned so far:

- **SPI Communication** — Understanding how the ESP32 talks to CC1101 and NRF24
- **Touch Input** — Handling resistive touch with XPT2046
- **RF Protocols** — Differentiating between WiFi, BLE, and Sub-GHz
- **Memory Management** — Dealing with ESP32's RAM constraints
- **UI Development** — Building responsive touch interfaces with TFT_eSPI

**This is still a work in progress.** My code might not be pretty, but it works (mostly 😅).

---

## 🤝 Credits & Attribution

### Original Foundation

**HaleHound-CYD** by [Jesse C. Hale (JesseCHale)](https://github.com/JesseCHale)

This project would not exist without Jesse's incredible work. From the hardware pin mappings to the comprehensive module structure, HaleHound-CYD laid the groundwork for everything you see here.

- **GitHub:** [github.com/JesseCHale/HaleHound-CYD](https://github.com/JesseCHale/HaleHound-CYD)
- **License:** MIT (see below)

### Key Contributors

- **Jesse C. Hale** — Original creator, hardware wizard, inspiration
- **Duggie** — Lunatic Fringe concept, bug reports
- **CiferTech** — Original ESP32-DIV project

### Dependencies

This project uses several open-source libraries:
- ESP32 Arduino Core (LGPL)
- TFT_eSPI
- RF24
- TinyGPSPlus
- ArduinoJson
- SmartRC-CC1101-Driver-Lib
- And more...

Please respect their individual licenses.

---

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

```
MIT License

Copyright (c) 2026 [Your Name] (Evaware Edition)
Copyright (c) 2026 Jesse C. Hale (HaleHound-CYD Edition)
Copyright (c) 2023 CiferTech (original ESP32-DIV)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction...
```

### Attribution Requirements

When using, modifying, or distributing this software, please retain:

1. Original copyright notices
2. This LICENSE file
3. Credit to the original authors

**Suggested attribution:**
```
"Based on Evaware (https://github.com/yourusername/evaware)"
"Built upon HaleHound-CYD by Jesse C. Hale (https://github.com/JesseCHale/HaleHound-CYD)"
```

---

## 💌 A Note From Me

This project represents my journey into embedded systems and wireless security. I'm learning as I go, making mistakes, and (hopefully) getting better every day.

**To Jesse:** Your work inspired me to dive into this world. Thank you for sharing your knowledge and creating such an awesome foundation to build upon. ❤️

**To everyone else:** If you find this useful, great! If you want to learn from my code (flaws and all), even better! I'm open to feedback and suggestions—just don't expect professional-grade code yet. 😅

---

*"I do not plan on maintaining and publicly releasing anything other than my shit code to see what I did right or you did wrong"* — Me, 2026 😂

---

## 🏷️ Badges

![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)
![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue)
![Status: Experimental](https://img.shields.io/badge/Status-Experimental-red)
![Made with: PlatformIO](https://img.shields.io/badge/Made%20with-PlatformIO-orange)

---

## 📬 Contact & Links

- **GitHub:** [yourusername](https://github.com/yourusername)
- **Project Page:** [github.com/yourusername/evaware](https://github.com/yourusername/evaware)
