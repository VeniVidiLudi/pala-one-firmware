#ifndef PALA_HAL_EPD_BACKEND_H
#define PALA_HAL_EPD_BACKEND_H

#include <Arduino.h>

// MUST precede epd_driver.h: both Adafruit_GFX and epdiy define GFXglyph/
// GFXfont. Pulling Adafruit's gfxfont.h in first sets its _GFXFONT_H_ guard
// so epdiy's copies yield, leaving Adafruit's layout in place — the one
// U8g2_for_Adafruit_GFX actually uses. The 2.1 port's own comment notes this
// requires a *patched* epdiy that honours that guard (a vanilla checkout
// doesn't) — confirm the project's epdiy dependency is that patched fork
// before building; this include order alone is necessary but not sufficient.
#include <Adafruit_GFX.h>
#include "epd_driver.h"   // LilyGo-EPD47 / epdiy — EPD_WIDTH(960) x EPD_HEIGHT(540)

#include "src/config.h"

// ============================================================================
//  Low-level e-paper backend for the LilyGo T5 4.7" S3 (ED047TC1) panel,
//  driven directly through epdiy rather than a higher-level Arduino_GFX-style
//  display library (the Heltec build's heltec-eink-modules dependency has no
//  epdiy equivalent).
//
//  Plays the same role heltec-eink-modules.h plays for the Heltec build:
//  state.h includes this for the `EpdDisplay` type and `extern EpdDisplay
//  display;`, and hal/display.h builds its Adafruit_GFX adapter + u8g2
//  glue on top, same as it does for the Heltec target.
//
//  Framebuffer model: all drawing goes into a 4-bit greyscale buffer (g_fb,
//  two pixels per byte, 0x0=black..0xF=white) in PSRAM. EpdDisplay::update()
//  commits it to the panel, either via a fast partial-refresh path (1-bit
//  thresholded, single waveform frame, used for page turns / menu moves) or
//  a periodic full-quality refresh that scrubs accumulated ghosting.
// ============================================================================

// All GFX drawing goes through this pointer (normally == the live framebuffer
// g_fb). Exposed here (not file-static in epd_backend.cpp) because hal/
// display.h's EpdGFXAdapter::drawPixel writes through it directly, mirroring
// how the original monolith's EpdGFXAdapter and EpdDisplay shared one
// translation unit. Nothing outside epd_backend.cpp/display.cpp should touch
// this.
extern uint8_t* g_canvas;

// ── Platform power hook ─────────────────────────────────────────────────────
// ui/sleep.cpp calls Platform::prepareToSleep() just before esp_deep_sleep_
// start() to let the board cut any board-specific rails. On this LilyGo build
// that means powering down the EPD high-voltage rails (epd_poweroff_all()).
// The upstream Pala_One_3.0 baseline references this symbol from sleep.cpp but
// never defines it — see the note in ui/sleep.cpp.
namespace Platform {
void prepareToSleep();
}

// ── Speed levers (independently switchable so each can be measured alone,
//    same knobs and same converged defaults as the 2.1 port) ────────────────
//
// A — FAST_DRAW_1BIT: render the 4bpp framebuffer as 1-bit black/white via
//     epd_draw_frame_1bit() (ONE waveform frame) instead of epd_draw_
//     grayscale_image() (15 frames, ~700ms). Reader/menu pages are pure
//     black text on white, so the 16 grey levels are wasted; this is the
//     biggest lever. epd_draw_frame_1bit only *adds* black where a bit is
//     set — it never whitens — so it must always follow a white erase
//     (every path here does). Full-screen draws only; a dirty-rect sub-area
//     falls back to greyscale (sub-rect 1bpp packing isn't implemented).
#define FAST_DRAW_1BIT      1
#define DRAW_1BIT_PASSES    4    // repeat the 1-bit frame N times to darken the ink
#define DRAW_1BIT_TIME      50   // per-row drive strength per 1-bit frame
#define DRAW_1BIT_THRESHOLD 8    // 4bpp value < this => black ink (0=black..15=white)
// Packing: g_fb->1bpp mapping (byte index = (x>>3)^1, bit = x&7, LSB-first).
#define DRAW_1BIT_BYTESWAP  1
#define DRAW_1BIT_MSB_FIRST 0
//
// B — KEEP_PANEL_POWERED: leave the EPD rails powered across updates instead
//     of epd_poweron()/epd_poweroff_all() around every page turn.
#define KEEP_PANEL_POWERED  0

// ── Partial-update strategy (adopted from yetisoldier's t5-ereader, same
//    panel + board; see LilyGoT5S3_Pala_One_2_1/LICENSE.yetisoldier) ───────
// A partial refresh is a single fast flash-erase via epd_push_pixels()
// followed by epd_draw_grayscale_image()/epd_draw_frame_1bit(). Far fewer
// waveform frames than the naive two-pass approach that originally made
// page turns slow.
//
// FAST_PARTIAL_DIRTY_RECT (opt-in, default OFF in the 2.1 port — left off
// here too): confines the flash+redraw to the bounding box of changed
// pixels. Don't enable without testing.
#define FAST_PARTIAL_DIRTY_RECT 0

#define PARTIAL_ERASE_METHOD 1   // 0 = clear-cycles (slow, thorough), 1 = push-flash (fast)

static const int PARTIAL_CLEAR_CYCLES      = 1;
static const int PARTIAL_CLEAR_TIME        = 40;

static const int PARTIAL_PUSH_DARK_PASSES  = 1;
static const int PARTIAL_PUSH_WHITE_PASSES = 2;
static const int PARTIAL_PUSH_TIME         = 40;

static const int FULL_CLEAR_CYCLES = 2;
static const int FULL_CLEAR_TIME   = 50;

// ============================================================================
//  EpdDisplay — fills the same role as the Heltec build's EInkDisplay_*
//  classes (begin/clearMemory/clear/fastmodeOn/fastmodeOff/landscape/update).
//  state.h aliases `EpdDisplay` in for `EInkDisplay` so the rest of the
//  firmware (hal/display.h, ui/*, the .ino) doesn't need to know which
//  backend it's talking to.
// ============================================================================
class EpdDisplay {
public:
  void begin();

  // White out the in-memory framebuffer only (no panel I/O).
  void clearMemory();

  // Flush the physical panel to white — drives out ghosting before a full draw.
  void clear();

  // fastmode selects the update path: off => crisp multi-cycle full refresh;
  // on => fast 1-cycle flash-erase + redraw for snappy menu/page changes.
  void fastmodeOn()  { fast_ = true; }
  void fastmodeOff() { fast_ = false; }

  // Orientation is applied by the GFX adapter (EpdGFXAdapter::drawPixel's
  // rotation math), so this is a no-op shim — kept only so callers written
  // against the Heltec interface don't need an #ifdef.
  void landscape() {}

  // Commit the framebuffer to the panel.
  void update();

private:
  bool fast_        = false;
  bool justCleared_ = false;
};

#endif  // PALA_HAL_EPD_BACKEND_H
