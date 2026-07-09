#include "src/ui/screens/bookmarks/session.h"

BookmarkSession g_bookmarkSession;

void resetBookmarkSession() {
  g_bookmarkSession.bookIndex = 0;
  g_bookmarkSession.selectedIndex = 0;
  g_bookmarkSession.count = 0;
}
