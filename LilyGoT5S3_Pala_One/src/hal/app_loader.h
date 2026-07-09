#ifndef PALA_HAL_APP_LOADER_H
#define PALA_HAL_APP_LOADER_H

#include <stdint.h>

#include "pala_api.h"   // PalaAPI

// ============================================================================
//  App loader — reads a position-independent .bin from LittleFS, allocates
//  exec-capable RAM, relocates, and calls the app's entry function.
//
//  Platform-specific by nature: the IRAM↔DRAM mapping trick (ESP32-S3 keeps
//  exec-capable SRAM at instruction-bus addresses that aren't writable from
//  the data bus) is what motivates this file existing as a hal-layer module
//  instead of inlining the call in `runApp`.
//
//  Validation logic (header field ranges + reloc entry ranges) is shared
//  with the upload route via `pure/app_header` — this file calls that
//  validator on the loaded buffer, then performs the actual fixup pass.
//
//  Error reporting: returns a `LoadResult`; rendering the error string is
//  the caller's job (AppsScreen). Keeps the loader free of `ui/widgets`
//  dependencies and lets the screen pick the right message phrasing.
// ============================================================================

enum class LoadResult {
  Ok,
  FileNotFound,
  FileTooSmall,    // < sizeof(PalaAppHeader) + 4
  FileTooLarge,    // > MAX_APP_BINARY
  ReadError,       // partial read from LittleFS
  OutOfMemory,     // heap_caps_malloc(MALLOC_CAP_EXEC) returned null
  BadMagic,        // header.magic != PALA_APP_MAGIC
  ApiMismatch,     // header.api_version != PALA_API_VERSION
  BadEntryOffset,  // entry_offset out of file range
  BadRelocTable,   // reloc table out of file range
  BadRelocEntry,   // an entry inside the reloc table is out of range
};

// Load `path` into exec-capable RAM, validate, relocate, and call
// `app_main(api)`. Blocks until the app returns; the loaded buffer is
// freed automatically before this returns.
//
// `*outFileApiVersion`, if non-null, is populated on `ApiMismatch` so the
// caller can show "API v3, need v4"-style messages.
LoadResult loadAndRunApp(const char* path, const PalaAPI* api,
                         uint32_t* outFileApiVersion = nullptr);

#endif  // PALA_HAL_APP_LOADER_H
