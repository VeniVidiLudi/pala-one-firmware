#ifndef PALA_PURE_APP_HEADER_H
#define PALA_PURE_APP_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "pala_app.h"

// ============================================================================
//  App-binary header validation.
//
//  Pure (no Arduino, no FS, no display). The loader (hal/app_loader) calls
//  this on a buffer it has just read from disk; the upload route calls the
//  status-only flavor on the first sizeof(PalaAppHeader) bytes of an
//  in-flight file before committing it.
//
//  Two-stage design:
//    1. validateAppHeader(buf, fileSize) — needs only the header bytes
//       (cheap, can be called before reading the whole file).
//    2. validateRelocEntries(buf, fileSize) — needs the reloc table loaded
//       into `buf` already. Walks each entry to ensure it points at a
//       4-byte slot inside the code/data region (i.e. before the reloc
//       table itself).
//
//  These mirror the ad-hoc checks that lived inline in the old
//  loadAndRunApp(); pulling them out lets us host-test the validation
//  logic without ESP-specific exec-memory wiring.
// ============================================================================

enum class AppHeaderStatus {
  Ok,
  TooSmall,        // fileSize < sizeof(PalaAppHeader) + 4
  BadMagic,        // header.magic != PALA_APP_MAGIC
  ApiMismatch,     // header.api_version != PALA_API_VERSION
  BadEntryOffset,  // entry_offset out of [sizeof(header), fileSize)
  BadRelocTable,   // reloc_offset / reloc_offset+count*4 out of file
};

// Inspect just the header. `buf` must contain at least sizeof(PalaAppHeader)
// bytes; only header fields are touched. Does NOT walk the reloc entries —
// see validateRelocEntries for that.
//
// `outApiVersion` and `outFileVersion` (optional) are filled in on
// ApiMismatch so callers can present "API v3, need v4" messages.
AppHeaderStatus validateAppHeader(const void* buf, size_t fileSize,
                                  uint32_t* outFileApiVersion = nullptr);

// Walk the reloc table that lives inside `buf` and verify every entry
// targets a 4-byte slot inside the code/data region (before the reloc
// table). Requires validateAppHeader(buf, fileSize) to have returned Ok
// first — entries are read directly from `buf[reloc_offset...]`.
//
// Returns true if all entries are in range. Returns true for reloc_count == 0.
bool validateRelocEntries(const void* buf, size_t fileSize);

#endif  // PALA_PURE_APP_HEADER_H
