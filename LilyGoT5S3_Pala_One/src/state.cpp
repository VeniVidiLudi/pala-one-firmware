#include "src/state.h"

// ============================================================================
//  Display + adapter (definitions; adapter class lives in display.h)
// ============================================================================
EpdDisplay display;

// ============================================================================
//  Globals — instances of the shared state structs declared in state.h.
//  Pure definitions; no logic lives in this file.
// ============================================================================
WebServer server(80);
Preferences prefs;
SPIClass sdSpi(HSPI);

char AP_SSID[24] = "PALA-";
const char* AP_PASS = "palaread";
