#ifndef PALA_UI_TOAST_H
#define PALA_UI_TOAST_H

#include "src/pure/arduino_compat.h"  // String

// ============================================================================
//  Toast — transient bottom-of-screen status message.
//
//  Lifecycle is owned by the main loop, not the screens. After `Toast::show`
//  the toast is "active" until either:
//    - it expires naturally (TOAST_MS elapsed), in which case the next
//      `Toast::clearIfExpired` returns true and the main loop redraws the
//      current screen so the toast pixels actually disappear; or
//    - `Toast::reset` is called explicitly (factory reset, etc.).
//
//  Screens that want to participate (currently just the reader) check
//  `Toast::isActive()` to decide between drawing their normal bottom-bar
//  content and calling `Toast::draw()`.
// ============================================================================
namespace Toast {

void show(const String& msg);

// True iff there's a non-expired toast to render.
bool isActive();

// Render the toast bar at the bottom of the screen. No-op if `!isActive()`.
void draw();

// Main-loop hook: if the toast just expired this tick, clear it and return
// true. The caller (main loop) should then trigger a redraw of the current
// screen so the toast pixels are actually erased from the e-ink.
bool clearIfExpired();

// Immediate clear — no expiry wait, no caller-driven redraw. Used at
// factory-reset / leaving-mode moments where we already plan to redraw.
void reset();

}  // namespace Toast

#endif  // PALA_UI_TOAST_H
