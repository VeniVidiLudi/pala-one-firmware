#ifndef PALA_UI_SCREENS_ABOUT_SCREEN_H
#define PALA_UI_SCREENS_ABOUT_SCREEN_H

#include "src/ui/screen.h"

class AboutScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern AboutScreen g_aboutScreen;

#endif  // PALA_UI_SCREENS_ABOUT_SCREEN_H
