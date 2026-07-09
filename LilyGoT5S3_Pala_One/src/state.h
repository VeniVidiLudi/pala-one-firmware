#ifndef PALA_STATE_H
#define PALA_STATE_H

#include <Arduino.h>
#include <heltec-eink-modules.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>

#include "src/config.h"
#include "src/pure/paginator.h"     // LayoutMetrics

// Use LittleFS as the project's filesystem. Defined AFTER FS.h has declared
// `class FS`, so the macro doesn't collide with that class name.
#define FS LittleFS

// ============================================================================
//  Globals (definitions live in state.cpp)
// ============================================================================
extern WebServer server;
extern Preferences prefs;

extern char AP_SSID[24];
extern const char* AP_PASS;

// Pick the right Heltec display class based on the env-selected build flag.
// Set in platformio.ini: [env:wireless-paper-v1_2] / [env:wireless-paper-v1_1].
// The default branch covers IDE IntelliSense (which parses without PIO env
// flags); real builds always come through pio with one of the flags set.
#if defined(DISPLAY_V1_1)
  using EInkDisplay = EInkDisplay_WirelessPaperV1_1;
#else  // DISPLAY_V1_2 or IntelliSense fallback
  using EInkDisplay = EInkDisplay_WirelessPaperV1_2;
#endif

extern EInkDisplay display;

#endif  // PALA_STATE_H
