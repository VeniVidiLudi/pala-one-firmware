#include "src/storage/book_metadata.h"

// ============================================================================
//  Testable KV-store-backed API
// ============================================================================
Bookmarks loadBookmarks(KeyValueStore& kv, const String& bookKey) {
  Bookmarks bm;
  uint8_t buf[BOOKMARKS_ENCODED_MAX_SIZE] = {0};
  size_t got = kv.getBytes(bmKeyFor(bookKey).c_str(), buf, sizeof(buf));
  decodeBookmarks(buf, got, bm);
  return bm;
}

void saveBookmarks(KeyValueStore& kv, const String& bookKey, const Bookmarks& bm) {
  uint8_t buf[BOOKMARKS_ENCODED_MAX_SIZE];
  size_t n = encodeBookmarks(bm, buf);
  kv.putBytes(bmKeyFor(bookKey).c_str(), buf, n);
}

int loadSavedPage(KeyValueStore& kv, const String& bookKey) {
  int p = kv.getInt((bookKey + "_p").c_str(), 0);
  return (p < 0) ? 0 : p;
}

void saveSavedPage(KeyValueStore& kv, const String& bookKey, int pageIndex) {
  kv.putInt((bookKey + "_p").c_str(), pageIndex);
}

uint32_t loadSavedOffset(KeyValueStore& kv, const String& bookKey) {
  // Stored as int; -1 (default when key absent) sentinels "not set."
  int v = kv.getInt((bookKey + "_off").c_str(), -1);
  return (v < 0) ? kOffsetUnset : (uint32_t)v;
}

void saveSavedOffset(KeyValueStore& kv, const String& bookKey, uint32_t byteOffset) {
  kv.putInt((bookKey + "_off").c_str(), (int)byteOffset);
}

bool clearBookMetadata(KeyValueStore& kv, const String& bookKey) {
  kv.remove((bookKey + "_p").c_str());
  kv.remove((bookKey + "_off").c_str());
  kv.remove(bmKeyFor(bookKey).c_str());
  return true;
}

void renameBookMetadata(KeyValueStore& kv, const String& oldKey, const String& newKey) {
  int progress = kv.getInt((oldKey + "_p").c_str(), -1);
  if (progress >= 0) {
    kv.putInt((newKey + "_p").c_str(), progress);
    kv.remove((oldKey + "_p").c_str());
  }

  int off = kv.getInt((oldKey + "_off").c_str(), -1);
  if (off >= 0) {
    kv.putInt((newKey + "_off").c_str(), off);
    kv.remove((oldKey + "_off").c_str());
  }

  uint8_t buf[BOOKMARKS_ENCODED_MAX_SIZE] = {0};
  size_t got = kv.getBytes(bmKeyFor(oldKey).c_str(), buf, sizeof(buf));
  if (got > 0) {
    kv.putBytes(bmKeyFor(newKey).c_str(), buf, got);
    kv.remove(bmKeyFor(oldKey).c_str());
  }
}

#ifdef ARDUINO
// ============================================================================
//  Firmware glue
// ============================================================================
#include "src/storage/fs_util.h"           // removeTreeRecursive — EPUB image dir cleanup
#include "src/storage/library.h"           // g_library (bulk invalidation iterates books)
#include "src/storage/page_cache.h"        // deletePageCacheForBook / renamePageCacheForBook
#include "src/storage/preferences_store.h"

// Sibling directory holding a book's extracted EPUB images, mirroring the
// derivation in epub_import.cpp's orchestrator: "<book>.txt" -> "<book>.imgs".
// Only EPUBs imported with image retention have one; for any other book this
// yields a path that simply doesn't exist (removeTreeRecursive no-ops on it).
static String imgDirForBook(const String& path) {
  String d = path;
  if (d.endsWith(".txt")) d = d.substring(0, d.length() - 4);
  return d + ".imgs";
}

uint8_t loadBookmarksForKey(const String& bookKey,
                            uint16_t outPages[MAX_BOOKMARKS],
                            uint32_t outOffsets[MAX_BOOKMARKS]) {
  PreferencesStore kv(prefs);
  Bookmarks bm = loadBookmarks(kv, bookKey);
  for (uint8_t i = 0; i < bm.count; i++) {
    outPages[i] = bm.pages[i];
    outOffsets[i] = bm.offsets[i];
  }
  return bm.count;
}

void saveBookmarksForKey(const String& bookKey,
                         const uint16_t pages[MAX_BOOKMARKS],
                         const uint32_t offsets[MAX_BOOKMARKS],
                         uint8_t count) {
  Bookmarks bm;
  bm.count = (count > MAX_BOOKMARKS) ? MAX_BOOKMARKS : count;
  for (uint8_t i = 0; i < bm.count; i++) {
    bm.pages[i] = pages[i];
    bm.offsets[i] = offsets[i];
  }
  PreferencesStore kv(prefs);
  saveBookmarks(kv, bookKey, bm);
}

int savedPageForBookPath(const String& path) {
  PreferencesStore kv(prefs);
  return loadSavedPage(kv, prefKeyForBook(path));
}

void deleteBookMetadata(const String& path) {
  PreferencesStore kv(prefs);
  clearBookMetadata(kv, prefKeyForBook(path));   // NVS: progress + bookmarks
  deletePageCacheForBook(path);                  // disk: pc_<hash>.bin
  removeTreeRecursive(imgDirForBook(path));      // disk: <book>.imgs/ (EPUB images)
}

void migrateBookMetadata(const String& oldPath, const String& newPath) {
  PreferencesStore kv(prefs);
  renameBookMetadata(kv, prefKeyForBook(oldPath), prefKeyForBook(newPath));
  renamePageCacheForBook(oldPath, newPath);
  // For now we DO NOT move the ".<book>.imgs" dir alongside the book. The
  // flattened .txt embeds ABSOLUTE paths to the extracted JPEGs (see
  // epub_import.cpp), so the images must stay where they were written or the
  // reader can't find them. Trade-off: moving a book and later
  // deleting it leaves the original .imgs behind (a storage leak, not a
  // correctness bug) — it's hidden from the library and reclaimed by a
  // factory reset / wipeFilesystem() or by manual cleanup of the SD.
}
#endif  // ARDUINO
