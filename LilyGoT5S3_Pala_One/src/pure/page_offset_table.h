#ifndef PALA_PURE_PAGE_OFFSET_TABLE_H
#define PALA_PURE_PAGE_OFFSET_TABLE_H

#include "arduino_compat.h"
#include "../config.h"

// A book's "page index" — the in-memory map from page number to absolute
// byte offset in the source file. Filled incrementally by the paginator as
// the reader walks forward; persisted to flash as a `pc_<hash>.bin` file so
// reopening a book doesn't have to re-paginate from scratch.
//
// `offsets[i]` is the start byte of page `i`. Valid entries are `[0, count)`.
// `count == 0` means "empty / no entries yet" (caller will typically seed
// `offsets[0] = 0` and set `count = 1` before reading).
//
// `eofReached` distinguishes "we haven't paginated this far yet" from
// "there are no more pages." Without it, `pageIndex >= count` is ambiguous.
struct PageOffsetTable {
  uint32_t offsets[MAX_PAGES];
  int  count = 0;
  bool eofReached = false;
};

#endif  // PALA_PURE_PAGE_OFFSET_TABLE_H
