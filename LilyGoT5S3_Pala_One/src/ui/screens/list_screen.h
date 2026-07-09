#ifndef PALA_UI_SCREENS_LIST_SCREEN_H
#define PALA_UI_SCREENS_LIST_SCREEN_H

#include "src/ui/screen.h"

class ListScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern ListScreen g_listScreen;

#endif  // PALA_UI_SCREENS_LIST_SCREEN_H
