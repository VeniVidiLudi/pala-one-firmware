#ifndef PALA_STORAGE_FS_UTIL_H
#define PALA_STORAGE_FS_UTIL_H

#include "src/config.h"
#include "src/state.h"

// LittleFS mount + size accounting + directory helpers. Firmware-only.

bool     fsBegin();
uint64_t fsTotalBytesSafe();
uint64_t fsUsedBytesSafe();
uint64_t fsFreeBytesSafe();
void   ensureBooksDir();
bool   ensureDirRecursive(const String& path);
bool   isDirEmpty(const String& path);

// Recursively delete a file or directory tree. On a directory, removes every
// child (depth-first) then the directory itself. Safe no-op if the path
// doesn't exist. Used by the factory reset on the SD-backed build, which has
// no FS.format() to fall back on (the Heltec/LittleFS build formats instead).
void   removeTreeRecursive(const String& path);

// Wipe the filesystem to a clean state — the SD-card stand-in for LittleFS's
// FS.format(). Removes every top-level entry under "/", then recreates the
// books directory. (SDFS has no format() method.)
void   wipeFilesystem();

#endif  // PALA_STORAGE_FS_UTIL_H
