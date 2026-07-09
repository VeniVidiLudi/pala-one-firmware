#ifndef PALA_HAL_DISPLAY_H
#define PALA_HAL_DISPLAY_H

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "src/config.h"
#include "src/hal/orientation.h"
#include "src/state.h"
#include "src/ui/screen_settings.h"

// ============================================================================
//  Display adapter — writes Adafruit_GFX draw calls straight into the epdiy
//  framebuffer (g_canvas, declared in hal/epd_backend.h) via epd_draw_pixel,
//  rather than going through an EInkDisplay-style object as the Heltec
//  adapter does.
//
//  Rotation follows the runtime orientation (hal/orientation.h): portrait
//  does a 90° rotation onto the physically-landscape panel; landscape uses
//  the native mapping.
// ============================================================================
class EpdGFXAdapter : public Adafruit_GFX {
public:
  EpdGFXAdapter() : Adafruit_GFX(SCREEN_W, SCREEN_H) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    if (!g_canvas) return;
    int16_t xx = (SCREEN_W - 1) - x;
    int16_t yy = (SCREEN_H - 1) - y;
    int16_t px, py;
    if (Orientation::isPortrait()) {
	  if (ScreenSettings::isScreenFlipped()) {
        px = yy;                    // 270° rotation onto the landscape panel
        py = x;
	  } else {
        px = y;                    // 90° rotation onto the landscape panel
        py = xx;
	  }
    } else {
	  if (ScreenSettings::isScreenFlipped()) {
        px = xx;                    // 180° rotation on native panel orientation:
        py = yy;                    // buttons on the bottom edge
	  } else {
        px = x;                    // native panel orientation: buttons on the top
        py = y;                    // edge
	  }
    }
    // Adafruit_GFX: color!=0 => ink (black). epdiy: 0=black .. 255=white.
    epd_draw_pixel(px, py, color ? 0 : 255, g_canvas);
  }
};

extern EpdGFXAdapter gfx;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

// ============================================================================
//  Drawing primitives
// ============================================================================

// Set up the device for any drawing pass: clear the offscreen buffer (unless
// the caller has already managed that) and put u8g2 into the project's
// transparent-foreground mode. Higher-level helpers (drawCenter,
// prepareMenuFrame in ui/widgets.h) call this internally.
void beginPageCanvas(bool clearMem = true);

#endif  // PALA_HAL_DISPLAY_H
