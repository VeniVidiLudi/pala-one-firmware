#include "src/state.h"

// ============================================================================
//  Display + adapter (definitions; adapter class lives in display.h)
// ============================================================================
EInkDisplay display;

// ============================================================================
//  Globals — instances of the shared state structs declared in state.h.
//  Pure definitions; no logic lives in this file.
// ============================================================================
WebServer server(80);
Preferences prefs;

char AP_SSID[24] = "PALA-";
const char* AP_PASS = "palaread";
