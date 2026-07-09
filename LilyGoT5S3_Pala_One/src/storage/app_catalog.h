#ifndef PALA_STORAGE_APP_CATALOG_H
#define PALA_STORAGE_APP_CATALOG_H

#include "src/pure/arduino_compat.h"
#include "src/config.h"

// ============================================================================
//  Apps catalog — inventory of /apps/*.bin discovered on disk.
//
//  Parallels `storage/library.h` but simpler: no folders, no derived view,
//  no per-app metadata beyond display name + path. Navigation state (the
//  cursor) is local to AppsScreen.
//
//  Display name comes from the PalaAppHeader.name field when the file has
//  a valid magic, falling back to the filename stem otherwise — so a
//  partially-corrupt binary still shows up in the menu with *something*
//  readable and the loader's error path takes over when the user opens it.
// ============================================================================

struct AppEntry {
  char name[MAX_APP_NAME + 1];   // display label
  char path[MAX_APP_PATH + 1];   // absolute path, e.g. "/apps/click_counter.bin"
};

struct AppCatalog {
  AppEntry entries[MAX_APPS];
  int      count = 0;
};

extern AppCatalog g_apps;

// (Re)scan /apps/ and populate `g_apps`. Creates /apps/ if missing.
// Entries are sorted by name. Safe to call repeatedly; each call fully
// replaces the previous catalog contents.
void loadApps();

// Bounds-safe path accessor. Returns nullptr if `idx` is out of range.
// Aliases storage owned by `g_apps`; valid until the next `loadApps()`.
const char* appPath(int idx);

#endif  // PALA_STORAGE_APP_CATALOG_H
