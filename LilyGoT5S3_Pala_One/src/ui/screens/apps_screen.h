#ifndef PALA_UI_SCREENS_APPS_SCREEN_H
#define PALA_UI_SCREENS_APPS_SCREEN_H

#include "src/ui/screen.h"

// ============================================================================
//  AppsScreen — list of installed apps under /apps/*.bin.
//
//  Mirrors BookmarkListScreen in shape: short click cycles the cursor,
//  double-click launches the highlighted app (matching the rest of the
//  rewrite — double-click is the activate gesture across LibraryScreen,
//  BookmarkListScreen, etc.), triple-click returns to the library root.
//  Long-press is reserved (potential future: per-app uninstall).
//
//  Behavior while an app runs: this screen stays as `g_currentScreen` —
//  `runApp` blocks until `app_main` returns, after which control comes
//  back to the main loop and our next `draw()` repaints the menu.
//  See docs/APPS_LAYER.md §4.2 for the rationale.
// ============================================================================
class AppsScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern AppsScreen g_appsScreen;

#endif  // PALA_UI_SCREENS_APPS_SCREEN_H
