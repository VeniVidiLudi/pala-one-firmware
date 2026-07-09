#ifndef PALA_UI_SCREENS_BOOKMARKS_BOOKMARK_LIST_SCREEN_H
#define PALA_UI_SCREENS_BOOKMARKS_BOOKMARK_LIST_SCREEN_H

#include "src/ui/screen.h"

class BookmarkListScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
};

extern BookmarkListScreen g_bmListScreen;

#endif  // PALA_UI_SCREENS_BOOKMARKS_BOOKMARK_LIST_SCREEN_H
