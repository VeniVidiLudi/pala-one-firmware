#include "src/ui/text.h"

#include "src/hal/display.h"            // u8g2
#include "src/pure/bookmarks_codec.h"   // kOffsetUnset
#include "src/storage/epub_import.h"    // EPUB_IMG_SENTINEL
#include "src/storage/page_cache.h"     // on-disk page-offset cache
#include "src/ui/epub_image.h"          // drawEpubImagePage / image-page flag
#include "src/ui/font.h"                // Font::useBody / bodyLayout / measureBionicLine / layoutForCache

// Measure-width adapter for the paginator. Routes through Font::measureBionicLine
// so the bionic 1-px-per-split-word adjustment is folded into the same width
// budget the paginator wraps against — without bionic this collapses to a
// plain getUTF8Width under the Body face.
static int bodyMeasure(const char* s) {
  return Font::measureBionicLine(s);
}

// ============================================================================
//  Page primitives — one page at `startPos`, three flavors.
//
//  All set the body font before calling the paginator so the measure
//  function and the metrics describe the same face. Each returns the byte
//  offset where the next page begins.
// ============================================================================
uint32_t drawPageAt(File& f, uint32_t startPos) {
  BufferedFileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();

  epubResetPageImageFlag();
  int cursorY = TOP_PAD + m.ascent;
  auto onLine = [&](const char* buf, size_t /*len*/) {
    // nullptr indicates a paragraph break. Advance by gap height.
    if (buf == nullptr) { cursorY += m.paragraphGapH; return; }
    // An image page is a single line prefixed with EPUB_IMG_SENTINEL; the rest
    // is the SD path. Hand it to the JPEG decoder, which paints full-screen and
    // flags the page so the caller drops the status bar.
    if ((unsigned char)buf[0] == EPUB_IMG_SENTINEL) {
      drawEpubImagePage(buf + 1);
      return;
    }
    // drawBionicLine sets the active u8g2 font back to Body before returning,
    // so the next line measurement (via bodyMeasure) is consistent.
    Font::drawBionicLine(MARGIN_X, cursorY, buf);
    cursorY += m.lineH;
  };

  return paginatePage(stream, startPos, m, bodyMeasure, onLine);
}

uint32_t extractPageText(File& f, uint32_t startPos, String& out) {
  BufferedFileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();

  auto onLine = [&](const char* buf, size_t len) {
    // nullptr indicates paragraph break.
    if (buf == nullptr) { out.concat('\n'); return; }
    // Image page: emit a readable placeholder rather than the raw sentinel +
    // SD path (which would be meaningless to a web "read as text" viewer).
    if ((unsigned char)buf[0] == EPUB_IMG_SENTINEL) { out.concat("[image]\n"); return; }
    // Trim leading whitespace (paginator already trims trailing).
    const char* start = buf;
    size_t remaining = len;
    while (remaining > 0 && (*start == ' ' || *start == '\t')) { start++; remaining--; }
    out.concat(start, remaining);
    out.concat('\n');
  };

  return paginatePage(stream, startPos, m, bodyMeasure, onLine);
}

uint32_t nextPageOffset(File& f, uint32_t startPos) {
  BufferedFileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();
  return paginatePage(stream, startPos, m, bodyMeasure, nullptr);
}

// ============================================================================
//  Cross-book offset lookup
// ============================================================================
uint32_t pageOffsetForPage(File& f, const String& path, int page) {
  if (page < 0) page = 0;

  // On-disk fast path: O(1) seek to the highest cached page <= target.
  // The active reader keeps this file fresh for books it visits; cross-book
  // lookups (web bookmark resolve, page-text export) ride along.
  uint32_t off = 0;
  int startPage = loadOffsetForPageFromDisk(path, f.size(),
                                            Font::layoutForCache(),
                                            page, &off);
  if (startPage < 0) startPage = 0;

  for (int p = startPage; p < page; p++) {
    uint32_t next = nextPageOffset(f, off);
    if (next == off) break;
    off = next;
  }
  return off;
}

uint32_t resolveBookmarkOffset(const String& path, uint16_t page, uint32_t storedOffset) {
  File f = FS.open(path, "r");
  if (!f) return 0;

  size_t size = f.size();
  if (storedOffset != kOffsetUnset && storedOffset < size) {
    f.close();
    return storedOffset;
  }

  uint32_t off = pageOffsetForPage(f, path, page);
  f.close();
  return off;
}
