#include "src/ui/screens/bookmarks/preview_screen.h"

#include "src/ui/reader.h"
#include "src/ui/screens/bookmarks/bookmark_list_screen.h"
#include "src/ui/screens/reader_screen.h"
#include "src/ui/text.h"

void BookmarkPreviewScreen::onEnter() {
  draw();
}

void BookmarkPreviewScreen::draw() {
  renderCurrentPage();
}

void BookmarkPreviewScreen::onButton(const ButtonEvent& e) {
  if (e.kind == ButtonEvent::Triple) {
    // Cancel — full clear so we don't leave a half-open state for the next
    // screen. The bookmark list screen opens its own file handle for labels.
    resetBookView();
    nextScreen = &g_bmListScreen;
    return;
  }

  if (e.kind == ButtonEvent::Long) {
    // Commit — keep the book open, hand off to reader. Force-save progress
    // at the bookmark's page so a sleep-before-render still resumes here.
    persistReaderState();
    nextScreen = &g_readerScreen;
    return;
  }

  if (e.kind == ButtonEvent::Double) {
    if (retreatPage()) renderCurrentPage();
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    if (advancePage()) renderCurrentPage();
    return;
  }
}

void BookmarkPreviewScreen::onSleep() {
  // Preview is transient — don't commit progress, don't arm wake state.
  // Wake state stays empty, so next boot lands in library; resetBookView()
  // drops the file handle plus the cursor/pages so we don't leave any
  // half-state for the (eventual) library entry to mop up.
  resetBookView();
}
