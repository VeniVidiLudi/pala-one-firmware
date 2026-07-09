#ifndef PALA_HAL_ORIENTATION_H
#define PALA_HAL_ORIENTATION_H

// ============================================================================
//  Runtime screen orientation — owns the logical screen dimensions
//  (g_screenW/g_screenH, read everywhere as config.h's SCREEN_W/SCREEN_H)
//  and the flag hal/display.h's EpdGFXAdapter::drawPixel consults for its
//  rotation math. Selected on the web settings page; persisted in NVS.
//
//  An orientation change swaps the text-line width and lines-per-page both,
//  so set() invalidates Font's layout cache; the page cache self-invalidates
//  through its layout stamp (PageCacheLayout::orient), and the web settings
//  POST handler re-locates the reader cursor exactly as it does for a font
//  change.
// ============================================================================
namespace Orientation {

// Read the persisted orientation from NVS and apply it. Call once from
// setup() after `prefs.begin`, before the first oriented draw (the boot-time
// defaults are portrait, so a full-panel clear before this is safe).
void loadSettings();

bool isPortrait();

// Apply + persist + invalidate Font's layout cache. No-op if unchanged.
void set(bool portrait);

// Apply WITHOUT persisting or touching the font layout cache — a low-level
// affordance for full-panel blits in a fixed format. sleep.cpp forces
// portrait around drawSleepScreen (screensaver slots are portrait-format
// files) and restores the live orientation afterwards.
void applyRuntimeOnly(bool portrait);

}  // namespace Orientation

#endif  // PALA_HAL_ORIENTATION_H
