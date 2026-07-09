#ifndef PALA_STORAGE_LIBRARY_H
#define PALA_STORAGE_LIBRARY_H

#include "src/pure/arduino_compat.h"
#include "src/pure/library_nav.h"  // Catalog, BookInfo, LibraryEntryType, LibEntry

// ============================================================================
//  The discovered library. Pure types live in `pure/library_nav.h`; this
//  header owns the global instance + the disk-loading operations that
//  populate it. Navigation state (cursor, folder expansion, the derived
//  display-list) lives on the library screen.
// ============================================================================
extern Catalog g_library;

// Catalog-only refresh. Reloads `g_library` from disk. Safe to call
// freely: the library screen keys folder expansion by name, so any
// reshuffle of `g_library.folders[]` here is reflected on the next draw
// without callers having to do anything special.
void loadBooks();

// ============================================================================
//  Index <-> path helpers — bounds checking lives here so callers don't have
//  to repeat `if (idx < 0 || idx >= g_library.bookCount)` everywhere.
// ============================================================================

// Returns the absolute path of the book at `idx`, or nullptr if `idx` is out
// of range. The returned pointer aliases storage owned by `g_library` and is
// valid until the next `loadBooks()`.
const char* bookPath(int idx);

// Returns the catalog index of the book whose path equals `path`, or -1 if
// no book with that path exists in the current catalog.
int bookIndexForPath(const String& path);

#endif  // PALA_STORAGE_LIBRARY_H
