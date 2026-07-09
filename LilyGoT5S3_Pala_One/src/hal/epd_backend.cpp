#include "src/hal/epd_backend.h"

#include <string.h>  // memcmp, memcpy, memset

uint8_t* g_canvas = nullptr;

namespace {

static const uint32_t FB_BYTES  = (EPD_WIDTH * EPD_HEIGHT) / 2;
static const uint32_t FB_STRIDE = EPD_WIDTH / 2;   // bytes per panel row

uint8_t* g_fb     = nullptr;   // live framebuffer (4bpp)
uint8_t* g_fbPrev = nullptr;   // last frame committed to the panel — drives the
                                // "skip identical partial update" check

#if FAST_DRAW_1BIT
static const uint32_t FB1_STRIDE = EPD_WIDTH / 8;           // bytes per row, 1bpp
static const uint32_t FB1_BYTES  = FB1_STRIDE * EPD_HEIGHT;
uint8_t* g_fb1 = nullptr;      // 1-bit black/white shadow of g_fb, FAST_DRAW_1BIT scratch

// Threshold the full 4bpp framebuffer into the 1-bit shadow g_fb1.
// Mapping (derived from epdiy's lut_1bpp vs the 4bpp path — see the speed-
// lever comments in epd_backend.h): byte index = (x>>3) ^ 1 (adjacent byte
// pairs swapped), bit = x&7 LSB-first. Confirmed correct on the 2.1 port's
// hardware; DRAW_1BIT_BYTESWAP / DRAW_1BIT_MSB_FIRST flip it if a different
// panel revision needs it.
void pack1bpp(const uint8_t* fb4) {
  if (!g_fb1) return;
  memset(g_fb1, 0, FB1_BYTES);
  for (uint32_t y = 0; y < EPD_HEIGHT; y++) {
    const uint8_t* src = fb4   + y * FB_STRIDE;
    uint8_t*       dst = g_fb1 + y * FB1_STRIDE;
    for (uint32_t x = 0; x < EPD_WIDTH; x++) {
      uint8_t bpair = src[x >> 1];
      uint8_t v = (x & 1) ? (bpair >> 4) : (bpair & 0x0F);   // 0=black..15=white
      if (v < DRAW_1BIT_THRESHOLD) {
        uint32_t bx = (x >> 3);
#if DRAW_1BIT_BYTESWAP
        bx ^= 1;
#endif
#if DRAW_1BIT_MSB_FIRST
        dst[bx] |= (uint8_t)(1u << (7 - (x & 7)));
#else
        dst[bx] |= (uint8_t)(1u << (x & 7));
#endif
      }
    }
  }
}
#endif  // FAST_DRAW_1BIT

// Lever B power helpers: when KEEP_PANEL_POWERED, the rails are powered once
// in begin() and never cycled per update; otherwise power them around every
// panel write.
inline void panelOn() {
#if !KEEP_PANEL_POWERED
  epd_poweron();
#endif
}
inline void panelOff() {
#if !KEEP_PANEL_POWERED
  epd_poweroff_all();
#endif
}

// Commit a 4bpp buffer to `area`. Full-screen draws can take the fast 1-bit
// path; sub-areas (dirty-rect) always use the 15-frame greyscale path.
void drawImage(Rect_t area, uint8_t* buf4, bool fullScreen) {
#if FAST_DRAW_1BIT
  if (fullScreen && g_fb1) {
    pack1bpp(buf4);
    // One pass renders text but very faint; ink darkens with cumulative
    // drive, so repeat the frame.
    for (int i = 0; i < DRAW_1BIT_PASSES; i++) {
      epd_draw_frame_1bit(area, g_fb1, BLACK_ON_WHITE, DRAW_1BIT_TIME);
    }
    return;
  }
#endif
  epd_draw_grayscale_image(area, buf4);
}

// Fast partial: light flash-erase of the area, then redraw the framebuffer.
void fastBlit(Rect_t area, uint8_t* buf, bool fullScreen) {
  panelOn();
#if PARTIAL_ERASE_METHOD == 1
  // Hand-rolled push sequence: optional dark frame(s) then white frame(s).
  // Each epd_push_pixels is one frame; end on white (the redraw assumes a
  // white start). color 0 = darken, 1 = lighten/white.
  for (int i = 0; i < PARTIAL_PUSH_DARK_PASSES;  i++) epd_push_pixels(area, PARTIAL_PUSH_TIME, 0);
  for (int i = 0; i < PARTIAL_PUSH_WHITE_PASSES; i++) epd_push_pixels(area, PARTIAL_PUSH_TIME, 1);
#else
  if (PARTIAL_CLEAR_CYCLES > 0) epd_clear_area_cycles(area, PARTIAL_CLEAR_CYCLES, PARTIAL_CLEAR_TIME);
#endif
  drawImage(area, buf, fullScreen);
  panelOff();
}

void partialUpdate() {
  // FAST_PARTIAL_DIRTY_RECT stays off (see epd_backend.h) — always blit the
  // full screen on a partial update. Bounding-box dirty-rect tracking was
  // left unported: the 2.1 source itself flags its sub-rectangle packing as
  // hardware-unverified, and transplanting an already-untested path isn't
  // "preserve the converged state."
  fastBlit(epd_full_screen(), g_fb, true);
}

}  // namespace

void EpdDisplay::begin() {
  epd_init();
  if (!g_fb)     g_fb     = (uint8_t*)heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM);
  if (!g_fbPrev) g_fbPrev = (uint8_t*)heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM);
#if FAST_DRAW_1BIT
  if (!g_fb1) g_fb1 = (uint8_t*)heap_caps_malloc(FB1_BYTES, MALLOC_CAP_SPIRAM);
#endif
  g_canvas = g_fb;   // draw to the live framebuffer unless redirected
  clearMemory();
  if (g_fbPrev) memset(g_fbPrev, 0xFF, FB_BYTES);
  fast_        = false;
  justCleared_ = false;
#if KEEP_PANEL_POWERED
  epd_poweron();   // lever B: power the rails once and leave them on
#endif
}

void EpdDisplay::clearMemory() {
  if (g_fb) memset(g_fb, 0xFF, FB_BYTES);
}

void EpdDisplay::clear() {
  panelOn();
  epd_clear();
  panelOff();
  justCleared_ = true;
}

void EpdDisplay::update() {
  if (justCleared_) {
    panelOn();
    drawImage(epd_full_screen(), g_fb, true);   // panel already white
    panelOff();
  } else if (fast_ && g_fbPrev) {
    // Skip the refresh entirely when nothing changed (redundant re-render).
    if (memcmp(g_fb, g_fbPrev, FB_BYTES) != 0) {
      partialUpdate();
    }
  } else {
    panelOn();
    epd_clear_area_cycles(epd_full_screen(), FULL_CLEAR_CYCLES, FULL_CLEAR_TIME);  // crisp full refresh
    drawImage(epd_full_screen(), g_fb, true);
    panelOff();
  }
  if (g_fbPrev) memcpy(g_fbPrev, g_fb, FB_BYTES);
  justCleared_ = false;
}

namespace Platform {
// Cut the EPD high-voltage rails before deep sleep. Matches the pre-deep-sleep
// epd_poweroff_all() in LilyGoT5S3_Pala_One_2_1. epd_poweroff_all() is safe to
// call even if the rails are already down (e.g. KEEP_PANEL_POWERED off).
void prepareToSleep() {
  epd_poweroff_all();
}
}  // namespace Platform
