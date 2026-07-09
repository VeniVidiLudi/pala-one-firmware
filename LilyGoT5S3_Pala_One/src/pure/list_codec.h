#ifndef PALA_PURE_LIST_CODEC_H
#define PALA_PURE_LIST_CODEC_H

#include "arduino_compat.h"
#include "../config.h"

// Pure encoder/decoder for the todo/shopping list byte blob stored in
// Preferences under "list_v1". Format:
//   1 byte count, then for each item:
//     1 byte done flag (0/1)
//     (MAX_LIST_TEXT + 1) bytes of NUL-terminated text

struct ListItemData {
  char text[MAX_LIST_TEXT + 1] = {0};
  uint8_t done = 0;
};

struct ListData {
  ListItemData items[MAX_LIST_ITEMS];
  int count = 0;
};

constexpr size_t LIST_ENCODED_MAX_SIZE =
  1u + (size_t)MAX_LIST_ITEMS * (1u + (size_t)MAX_LIST_TEXT + 1u);

// Normalize a single list item's text:
//   - strip CR
//   - collapse newlines/tabs/runs of spaces into a single space
//   - trim leading/trailing whitespace
//   - truncate to MAX_LIST_TEXT chars
void sanitizeListItemText(String& s);

// Decode `got` bytes from `buf` into `out`. Empty (post-sanitize) items are
// silently dropped. Returns the resulting visible count (== out.count).
int decodeList(const uint8_t* buf, size_t got, ListData& out);

// Encode `data` into `outBuf` (must be at least LIST_ENCODED_MAX_SIZE bytes).
// Returns the number of bytes written.
size_t encodeList(const ListData& data, uint8_t* outBuf);

// True if at least one item has non-empty text.
bool listHasVisible(const ListData& data);

#endif  // PALA_PURE_LIST_CODEC_H
