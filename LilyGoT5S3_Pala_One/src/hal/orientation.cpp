#include "src/hal/orientation.h"

#include "src/config.h"
#include "src/state.h"        // prefs
#include "src/hal/display.h"  // gfx — base-class dims kept in step below
#include "src/ui/font.h"      // invalidateLayoutCache (same contract as Statusbar::setMode)

// Logical screen dimensions, aliased as SCREEN_W/SCREEN_H by config.h.
// Constant-initialized to the portrait defaults so global constructors
// (EpdGFXAdapter's Adafruit_GFX base) and anything that draws before
// loadSettings() see sane values regardless of TU init order.
int g_screenW = 540;
int g_screenH = 960;

namespace Orientation {

static bool s_portrait = true;
static constexpr const char* kKeyOrient = "cfg_orient";

bool isPortrait() { return s_portrait; }

void applyRuntimeOnly(bool portrait) {
  s_portrait = portrait;
  g_screenW = portrait ? 540 : 960;
  g_screenH = portrait ? 960 : 540;
  // Keep the Adafruit_GFX base dims in step: primitives that span the panel
  // (fillScreen) size themselves off _width/_height. gfx is constructed
  // portrait (540x960, rotation 0); rotation 1 swaps width()/height().
  // Our drawPixel does its own transform off isPortrait() and ignores the
  // GFX rotation value itself.
  gfx.setRotation(portrait ? 0 : 1);
}

void loadSettings() {
  applyRuntimeOnly(prefs.getUChar(kKeyOrient, ORIENTATION_DEFAULT)
                   == ORIENTATION_PORTRAIT);
}

void set(bool portrait) {
  if (portrait == s_portrait) return;
  applyRuntimeOnly(portrait);
  prefs.putUChar(kKeyOrient,
                 portrait ? ORIENTATION_PORTRAIT : ORIENTATION_LANDSCAPE);
  // Line width and lines-per-page both changed under the cached metrics.
  Font::invalidateLayoutCache();
}

}  // namespace Orientation
