#ifndef PALA_UI_SCREENS_BOOKMARKS_BOOK_SELECT_SCREEN_H
#define PALA_UI_SCREENS_BOOKMARKS_BOOK_SELECT_SCREEN_H

#include "src/ui/screen.h"

class BookmarkBookSelectScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern BookmarkBookSelectScreen g_bmBookSelectScreen;

#endif  // PALA_UI_SCREENS_BOOKMARKS_BOOK_SELECT_SCREEN_H
