#ifndef PALA_UI_EPUB_IMAGE_H
#define PALA_UI_EPUB_IMAGE_H

// ============================================================================
//  EPUB image pages
//
//  The epub converter (storage/epub_import.h), writes image pages into the flattened
//  .txt as a single line: <0x0C><sd-card-path>\n. The pure paginator
//  (pure/paginator.cpp) treats that sentinel as a hard page boundary so an
//  image always owns a page of its own, and emits the line — sentinel byte
//  included — to the draw callback. text.cpp recognises the leading sentinel
//  and routes the page here for JPEG decode + ordered-dither rendering.
//
//  This module is the ONLY place that depends on JPEGDEC, so the rest of the
//  reader (and the pure pagination engine) stays free of it.
// ============================================================================

// Clear the "current page is an image" flag — call once per page render before
// the paginator may emit an image line.
void epubResetPageImageFlag();

// True if the most recent page render was an image page, so renderCurrentPage
// can suppress the status bar. Valid until the next epubResetPageImageFlag().
bool epubLastPageWasImage();

// Decode the JPEG at `sdPath` and paint it centred, aspect-fit, full-screen
// into the already-cleared canvas; sets the image-page flag. Draws a "[image]"
// placeholder on any failure (or when image retention is compiled out).
void drawEpubImagePage(const char* sdPath);

#endif  // PALA_UI_EPUB_IMAGE_H
