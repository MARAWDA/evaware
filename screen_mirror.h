#ifndef SCREEN_MIRROR_H
#define SCREEN_MIRROR_H

// =============================================================================
// HaleHound-CYD USB Screen Mirror
// Lets the EvaWare Flasher desktop app request a snapshot of what's currently
// on the TFT and stream it back over serial (RLE-compressed RGB565 rows).
// Hooked into getTouchPoint() so it works from inside any screen's own loop,
// not just the idle main menu.
// Created: 2026-08-25
// =============================================================================

#include <Arduino.h>

// Called on every touch poll. If the PC has sent a mirror-request byte,
// this captures and streams one frame, then returns (no-op otherwise).
void checkScreenMirrorRequest();

#endif // SCREEN_MIRROR_H
