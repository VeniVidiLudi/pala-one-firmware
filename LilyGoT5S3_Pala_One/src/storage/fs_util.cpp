#include "src/storage/fs_util.h"

bool fsBegin() {
  // First try to mount without formatting — protects existing data.
  if (FS.begin(false)) return true;

  // Mount failed. This happens on a brand-new device where the LittleFS
  // partition has never been formatted. Format once, then mount.
  // If the partition already had data but is now corrupt, this wipes it —
  // which is the correct recovery action (same as factory reset).
  Serial.println("[FS] Mount failed — formatting LittleFS...");
  if (!FS.format()) {
    Serial.println("[FS] Format failed.");
    return false;
  }
  Serial.println("[FS] Format OK, mounting...");
  return FS.begin(false);
}

size_t fsTotalBytesSafe() { return FS.totalBytes(); }
size_t fsUsedBytesSafe()  { return FS.usedBytes(); }
size_t fsFreeBytesSafe() {
  size_t total = fsTotalBytesSafe();
  size_t used = fsUsedBytesSafe();
  return (total >= used) ? (total - used) : 0;
}

void ensureBooksDir() {
  if (!FS.exists("/books")) FS.mkdir("/books");
}

bool ensureDirRecursive(const String& path) {
  if (path.length() == 0 || path == "/") return true;
  if (FS.exists(path)) return true;

  int slash = path.lastIndexOf('/');
  if (slash > 0) {
    String parent = path.substring(0, slash);
    if (parent.length() > 0 && !FS.exists(parent)) {
      if (!ensureDirRecursive(parent)) return false;
    }
  }
  return FS.mkdir(path);
}

bool isDirEmpty(const String& path) {
  File dir = FS.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }
  File f = dir.openNextFile();
  bool empty = !f;
  if (f) f.close();
  dir.close();
  return empty;
}
