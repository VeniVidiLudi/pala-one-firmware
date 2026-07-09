#ifndef PALA_PURE_BOOKMARK_LABEL_H
#define PALA_PURE_BOOKMARK_LABEL_H

#include "arduino_compat.h"
#include "stream.h"

// Reads the first few words at byte offset `off` and produces a UI label like
// "It was the best of - p. 42". Stops after 5 words, 44 chars, or 240 scanned
// bytes (whichever comes first). Pure — caller provides an IReadStream.
String readBookmarkLabelAtOffset(IReadStream& in, uint32_t off, int page);

#endif  // PALA_PURE_BOOKMARK_LABEL_H
