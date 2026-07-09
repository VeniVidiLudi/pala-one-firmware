#include "src/storage/fs_util.h"

bool fsBegin() {
  // microSD, not LittleFS — there's no in-firmware reformat path (unlike a
  // raw flash partition, the card normally arrives pre-formatted FAT32, and we
  // can't safely just reformat on failed mount. A failed mount here almost
  // always means "no card" or "not FAT32" —
  // surfaced to the user as a storage error.
  sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  return FS.begin(SD_CS, sdSpi);
}

uint64_t fsTotalBytesSafe() { return FS.totalBytes(); }
uint64_t fsUsedBytesSafe()  { return FS.usedBytes(); }
uint64_t fsFreeBytesSafe() {
  uint64_t total = fsTotalBytesSafe();
  uint64_t used = fsUsedBytesSafe();
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

void removeTreeRecursive(const String& path) {
  if (path.length() == 0 || path == "/") return;
  if (!FS.exists(path)) return;

  File entry = FS.open(path);
  if (!entry) return;

  if (!entry.isDirectory()) {
    entry.close();
    FS.remove(path);
    return;
  }

  // Collect child names first, then act — deleting while an openNextFile()
  // iterator is live is not reliable across SD/FAT.
  for (;;) {
    File child = entry.openNextFile();
    if (!child) break;
    // child.name() may be a bare leaf or a full path depending on core
    // version; normalise to an absolute path under `path`.
    String cn = child.name();
    bool isDir = child.isDirectory();
    child.close();
    int slash = cn.lastIndexOf('/');
    if (slash >= 0) cn = cn.substring(slash + 1);
    String full = path + "/" + cn;
    if (isDir) removeTreeRecursive(full);
    else       FS.remove(full);
  }
  entry.close();
  FS.rmdir(path);
}

void wipeFilesystem() {
  // SDFS has no format(); emulate it by removing every top-level entry.
  File root = FS.open("/");
  if (root) {
    for (;;) {
      File child = root.openNextFile();
      if (!child) break;
      String cn = child.name();
      bool isDir = child.isDirectory();
      child.close();
      int slash = cn.lastIndexOf('/');
      if (slash >= 0) cn = cn.substring(slash + 1);
      if (cn.length() == 0) continue;
      String full = "/" + cn;
      if (isDir) removeTreeRecursive(full);
      else       FS.remove(full);
    }
    root.close();
  }
  ensureBooksDir();
}
