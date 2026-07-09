#ifndef PALA_STATE_H
#define PALA_STATE_H

#include <Arduino.h>
#include "src/hal/epd_backend.h"    // EpdDisplay — pulls in Adafruit_GFX before epd_driver.h
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>

#include "src/config.h"
#include "src/pure/paginator.h"     // LayoutMetrics

// LilyGo build: storage is a microSD card over SPI, not LittleFS — there's
// no equivalent on-die flash filesystem partition on this board.
// `FS` is a bare-token rewrite (defined AFTER SD.h has declared `class FS`,
// so the macro doesn't collide with that class name). Caveat: this rewrite
// also mangles any later `fs::FS` *type* reference into `fs::SD` nonsense, so
// a header declaring `fs::FS&` parameters (e.g. the EPUB importer) that gets
// included after this line must shield its declaration with
// `#pragma push_macro("FS") / #undef FS / ... / #pragma pop_macro("FS")` —
// see storage/epub_import.h, which does exactly that.
#define FS SD

// microSD over SPI on the LilyGo T5 4.7" S3. The EPD uses a parallel bus, so
// these SPI pins don't conflict with the panel.
#define SD_CS    42
#define SD_MOSI  15
#define SD_MISO  16
#define SD_SCLK  11

// ============================================================================
//  Globals (definitions live in state.cpp)
// ============================================================================
extern WebServer server;
extern Preferences prefs;
extern SPIClass sdSpi;       // dedicated HSPI bus for the SD card

extern char AP_SSID[24];
extern const char* AP_PASS;

// EpdDisplay (epd_backend.h) fills the role the Heltec build's
// EInkDisplay_WirelessPaperV1_x classes fill — see that header for the
// epdiy-specific framebuffer details.
extern EpdDisplay display;

#endif  // PALA_STATE_H
