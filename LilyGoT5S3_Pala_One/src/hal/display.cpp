#include "src/hal/display.h"

U8G2_FOR_ADAFRUIT_GFX u8g2;
HeltecGFXAdapter gfx(display);

// ============================================================================
//  Drawing primitives
// ============================================================================
void beginPageCanvas(bool clearMem) {
  if (clearMem) display.clearMemory();
  display.landscape();
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(1);
  u8g2.setBackgroundColor(0);
}
