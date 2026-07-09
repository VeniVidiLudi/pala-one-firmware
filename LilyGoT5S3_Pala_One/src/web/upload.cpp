#include "src/web/upload.h"

#include "src/config.h"
#include "src/state.h"
#include "src/pure/paths.h"
#include "src/pure/text_util.h"
#include "src/storage/epub_import.h"
#include "src/storage/fs_util.h"
#include "src/storage/library.h"
#include "src/ui/screensavers.h"   // SCREENSAVER_BYTES (sleep-image size check)
#include "src/web/chrome.h"

// ── Upload diagnostics ──────────────────────────────────────────────────────
// Set to 1 to trace upload byte-flow over Serial (115200). Costs only a few
// counters + a printf at END, so it can be left on. The "Upload incomplete (SD
// full or write error)" message is the EPUB integrity check (storedSize !=
// totalSize): bytes are lost *after* the WebServer counted them into totalSize,
// i.e. inside our SD write/flush. The counters below split that pipeline into
// delivered -> written -> stored so the next failed upload says exactly where
// the bytes vanish:
//   delivered : sum of HTTPUpload.currentSize seen across WRITE events
//   written   : sum of File::write() return values (bytes accepted to cache)
//   stored    : file size re-read after close() (bytes that survived flush)
// delivered<totalSize => WebServer dropped/ended the stream short.
// written<delivered   => write() returned short and we didn't recover.
// stored<written      => a sector flush failed silently at close().
#define UPLOAD_DIAG 1
#if UPLOAD_DIAG
  #define UDIAG(fmt, ...) Serial.printf("[upload] " fmt "\n", ##__VA_ARGS__)
#else
  #define UDIAG(fmt, ...) do {} while (0)
#endif

// ============================================================================
//  Per-session state. File-static because the route handlers below are the
//  only readers; the screen reaches it only through the reset functions.
// ============================================================================
namespace {
struct BookUpload {
  File   tmpFile;
  String tmpPath;
  String pendingUtf8Tail;
  String finalName;
  bool   ok = false;
  bool   isEpub = false;   // set at UPLOAD_FILE_START from the filename extension
  String error;
  uint64_t maxBytes = 0;   // free-space cap; uint64 so a >4 GB card isn't truncated
  // Cross-chunk state for streaming compactText() during upload, so a
  // whitespace or newline run that spans a chunk boundary collapses
  // correctly. Reset in UPLOAD_FILE_START. See pure/text_util.h.
  bool   compactLastWasSpace = false;
  int    compactNewlineCount = 0;
  // Upload diagnostics (see UPLOAD_DIAG above). Tracked for every upload so a
  // failure prints a full byte-flow trace; near-free when UPLOAD_DIAG is off.
  uint32_t diagWriteCalls = 0;   // UPLOAD_FILE_WRITE events seen
  uint64_t diagDelivered  = 0;   // sum of currentSize delivered to us
  uint64_t diagWritten    = 0;   // sum of File::write() return values
  uint32_t diagShortWrites = 0;  // write() returned fewer bytes than requested
  uint32_t diagZeroStalls  = 0;  // write() returned 0 (transient SD stall)
  uint32_t diagRetryGiveups = 0; // write() still 0 after the retry budget
};

struct SleepUpload {
  File   tmpFile;
  String tmpPath;
  bool   ok = false;
  String error;
};

BookUpload  s_book;
SleepUpload s_sleep;
}  // namespace

void resetBookUpload() {
  if (s_book.tmpFile) s_book.tmpFile.close();
  s_book = BookUpload{};
}

void resetSleepUpload() {
  if (s_sleep.tmpFile) s_sleep.tmpFile.close();
  s_sleep = SleepUpload{};
}

// ============================================================================
//  Book upload
//
//  Two handlers per route: a streaming chunk receiver (`*Stream`) and a final
//  response handler (`*Done`). The stream handler maintains a 4-byte UTF-8
//  tail across chunks so a multibyte codepoint isn't split mid-character;
//  each chunk is normalized + compacted before being written to the temp
//  file. On END, atomic rename into place.
// ============================================================================

static void handleUploadDone() {
  if (!s_book.ok) {
    server.send(400, "text/plain; charset=utf-8",
                s_book.error.length()
                  ? s_book.error
                  : String(D_WEB_UPLOAD_ERR_FALLBACK));
    return;
  }

  // An uploaded .epub is stored verbatim by the stream handler above; flatten
  // it to a .txt the existing byte-offset reader can paginate unchanged, then
  // drop the .epub (basic flatten — text + reading order only, no images;
  // see storage/epub_import.h).
  if (s_book.isEpub) {
    String epubPath = "/books/" + s_book.finalName;
    String txtName  = s_book.finalName.substring(0, s_book.finalName.length() - 5) + ".txt";
    String txtPath  = "/books/" + txtName;
    String title, author, err;
    bool convertOk = epubConvertToTxt(FS, epubPath.c_str(), txtPath.c_str(), &title, &author, &err);
    if (FS.exists(epubPath)) FS.remove(epubPath);
    if (!convertOk) {
      server.send(400, "text/plain; charset=utf-8",
                  err.length() ? err : String(D_WEB_ERR_EPUB_CONVERT_FAILED));
      return;
    }
    s_book.finalName = txtName;
  }

  loadBooks();   // refresh after the stream handler appended the new book

  String finalPath = "/books/" + s_book.finalName;
  size_t storedSize = 0;
  File stored = FS.open(finalPath, "r");
  if (stored) {
    storedSize = stored.size();
    stored.close();
  }

  String inner;
  inner.reserve(1200);
  inner += "<div class='card'><h2>" D_WEB_UPLOAD_COMPLETE_HEADING "</h2><p class='muted'>" D_WEB_UPLOAD_COMPLETE_DESC "</p>";
  inner += "<div class='stats'>";
  inner += "<div class='stat'><span class='muted'>" D_WEB_UPLOAD_BOOK_LABEL  "</span><b>" + htmlEscape(s_book.finalName) + "</b></div>";
  inner += "<div class='stat'><span class='muted'>" D_WEB_UPLOAD_STORED_SIZE "</span><b>" + humanBytes(storedSize)         + "</b></div>";
  inner += "<div class='stat'><span class='muted'>" D_WEB_UPLOAD_BOOKS_NOW   "</span><b>" + String(g_library.bookCount)    + "</b></div>";
  inner += "<div class='stat'><span class='muted'>" D_WEB_UPLOAD_FREE_SPACE  "</span><b>" + humanBytes(fsFreeBytesSafe())  + "</b></div>";
  inner += "</div><div class='actions'><a class='btn' href='/'>" D_WEB_UPLOAD_ANOTHER "</a><a class='btn secondary' href='/files'>" D_WEB_OPEN_FILES_BUTTON "</a></div></div>";
  inner += storageCardHtml();

  String page = successPage(
    D_WEB_UPLOAD_COMPLETE_HEADING,
    D_WEB_UPLOAD_BOOK_SAVED,
    D_WEB_UPLOAD_FINISHED,
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

static void handleUploadBookStream() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    s_book.ok = false;
    s_book.error = "";
    s_book.finalName = "";
    s_book.pendingUtf8Tail = "";
    s_book.tmpPath = "";
    s_book.compactLastWasSpace = false;
    s_book.compactNewlineCount = 0;
    s_book.diagWriteCalls = 0;
    s_book.diagDelivered = 0;
    s_book.diagWritten = 0;
    s_book.diagShortWrites = 0;
    s_book.diagZeroStalls = 0;
    s_book.diagRetryGiveups = 0;

    loadBooks();   // defensive — protects MAX_BOOKS check from a stale catalog
    if (g_library.bookCount >= MAX_BOOKS) {
      s_book.error = D_WEB_ERR_LIBRARY_FULL;
      return;
    }

    uint64_t freeBytes = fsFreeBytesSafe();
    if (freeBytes < 8192) {
      s_book.error = D_WEB_ERR_NOT_ENOUGH_SPACE;
      return;
    }
    s_book.maxBytes = freeBytes;

    String clean = sanitizeUploadedFilename(up.filename);
    String lc = clean; lc.toLowerCase();
    s_book.isEpub    = lc.endsWith(".epub");
    s_book.finalName = clean;
    s_book.tmpPath   = "/books/" + clean + ".tmp";

    if (FS.exists(s_book.tmpPath)) FS.remove(s_book.tmpPath);
    s_book.tmpFile = FS.open(s_book.tmpPath, "w");
    if (!s_book.tmpFile) {
      s_book.error = D_WEB_ERR_CANT_CREATE_TEMP_BOOK;
      s_book.tmpPath = "";
    }
    UDIAG("START name=%s epub=%d free=%llu maxBytes=%llu tmpOk=%d",
          s_book.finalName.c_str(), (int)s_book.isEpub,
          (unsigned long long)fsFreeBytesSafe(),
          (unsigned long long)s_book.maxBytes, (int)(bool)s_book.tmpFile);
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    s_book.diagWriteCalls++;
    s_book.diagDelivered += up.currentSize;
    if (s_book.error.length() > 0) return;
    if (s_book.tmpFile && up.currentSize > 0
        && s_book.tmpFile.size() + up.currentSize > s_book.maxBytes) {
      s_book.error = D_WEB_ERR_WRITE_FAILED;
      s_book.tmpFile.close();
      if (s_book.tmpPath.length() > 0 && FS.exists(s_book.tmpPath)) FS.remove(s_book.tmpPath);
      return;
    }
    if (s_book.isEpub) {
      // EPUB is a binary ZIP — write raw bytes, no text normalization. Loop
      // so a short SD write can't silently truncate the archive into a bad
      // zip (a zero-length write is usually a transient SD stall, not a hard
      // failure — retry a few times before giving up).
      if (s_book.tmpFile && up.currentSize > 0) {
        size_t off = 0;
        while (off < up.currentSize) {
          size_t want = up.currentSize - off;
          size_t w = s_book.tmpFile.write(up.buf + off, want);
          if (w == 0) {
            s_book.diagZeroStalls++;
            for (int attempt = 0; attempt < 5 && w == 0; attempt++) {
              delay(2);
              w = s_book.tmpFile.write(up.buf + off, want);
            }
            if (w == 0) {
              s_book.diagRetryGiveups++;
              UDIAG("ZERO-WRITE giveup at off=%llu chunk=%u call=%u",
                    (unsigned long long)(s_book.diagWritten + off),
                    (unsigned)up.currentSize, (unsigned)s_book.diagWriteCalls);
              s_book.error = D_WEB_ERR_WRITE_FAILED;
              break;
            }
          }
          if (w < want) s_book.diagShortWrites++;
          s_book.diagWritten += w;
          off += w;
        }
      }
      return;
    }
    if (s_book.tmpFile && up.currentSize > 0) {
      String chunk = s_book.pendingUtf8Tail + String(reinterpret_cast<const char*>(up.buf), up.currentSize);
      int len = (int)chunk.length();
      if (len > 4) {
        s_book.pendingUtf8Tail = chunk.substring(len - 4);
        chunk = chunk.substring(0, len - 4);
      } else {
        s_book.pendingUtf8Tail = chunk;
        chunk = "";
      }
      if (chunk.length() > 0) {
        String cleaned = normalizeTypography(chunk);
        cleaned = compactText(cleaned,
                              &s_book.compactLastWasSpace,
                              &s_book.compactNewlineCount,
                              /*trimTail=*/false);
        size_t cleanedLen = cleaned.length();
        size_t wrote = s_book.tmpFile.print(cleaned);
        s_book.diagWritten += wrote;
        if (wrote != cleanedLen) {
          // Short write — out of space or FS error. Abort so a truncated
          // file isn't promoted to a finalized book.
          if (wrote < cleanedLen) s_book.diagShortWrites++;
          s_book.error = D_WEB_ERR_WRITE_FAILED;
          s_book.tmpFile.close();
          if (s_book.tmpPath.length() > 0
              && FS.exists(s_book.tmpPath)) {
            FS.remove(s_book.tmpPath);
          }
        }
      }
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (s_book.error.length() > 0 && !s_book.tmpFile) return;
    if (s_book.tmpFile) {
      if (s_book.pendingUtf8Tail.length() > 0) {
        String cleaned = normalizeTypography(s_book.pendingUtf8Tail);
        cleaned = compactText(cleaned,
                              &s_book.compactLastWasSpace,
                              &s_book.compactNewlineCount,
                              /*trimTail=*/true);
        size_t cleanedLen = cleaned.length();
        size_t wrote = s_book.tmpFile.print(cleaned);
        s_book.diagWritten += wrote;
        if (wrote != cleanedLen && s_book.error.length() == 0) {
          if (wrote < cleanedLen) s_book.diagShortWrites++;
          s_book.error = D_WEB_ERR_WRITE_FAILED;
        }
        s_book.pendingUtf8Tail = "";
      }
      s_book.tmpFile.close();

      // For an EPUB the stored file is a verbatim copy, so its size MUST
      // equal the number of bytes received. A mismatch means the write
      // truncated mid-stream (SD full or a transient failure) — accepting it
      // would strip the zip's trailing central directory and later fail
      // conversion with a misleading "no EOCD" error. Catch it here instead
      // with a clear, actionable message.
      bool truncated = false;
      if (s_book.isEpub && s_book.error.length() == 0 && up.totalSize > 0) {
        size_t storedSize = 0;
        File chk = FS.open(s_book.tmpPath, "r");
        if (chk) { storedSize = chk.size(); chk.close(); }
        truncated = (storedSize != (size_t)up.totalSize);
        if (truncated) s_book.error = D_WEB_ERR_EPUB_TRUNCATED;
      }

#if UPLOAD_DIAG
      {
        // Re-read the on-disk size (post-close) so the trace reflects what
        // actually survived flush, then print the full delivered->written->
        // stored pipeline against totalSize. tmpPath still exists here — the
        // cleanup/rename below runs after this block.
        uint64_t storedNow = 0;
        if (s_book.tmpPath.length() > 0) {
          File chk = FS.open(s_book.tmpPath, "r");
          if (chk) { storedNow = chk.size(); chk.close(); }
        }
        UDIAG("END epub=%d calls=%u delivered=%llu written=%llu stored=%llu "
              "totalSize=%llu short=%u stalls=%u giveups=%u free=%llu err=%s",
              (int)s_book.isEpub, (unsigned)s_book.diagWriteCalls,
              (unsigned long long)s_book.diagDelivered,
              (unsigned long long)s_book.diagWritten,
              (unsigned long long)storedNow,
              (unsigned long long)up.totalSize,
              (unsigned)s_book.diagShortWrites, (unsigned)s_book.diagZeroStalls,
              (unsigned)s_book.diagRetryGiveups,
              (unsigned long long)fsFreeBytesSafe(),
              s_book.error.length() ? s_book.error.c_str() : "-");
        // Pinpoint the leak stage for an EPUB (verbatim copy: every stage
        // should equal totalSize).
        if (s_book.isEpub) {
          if (s_book.diagDelivered < (uint64_t)up.totalSize)
            UDIAG("  -> stream short: WebServer delivered < totalSize");
          else if (s_book.diagWritten < s_book.diagDelivered)
            UDIAG("  -> write() short: SD accepted fewer bytes than delivered");
          else if (storedNow < s_book.diagWritten)
            UDIAG("  -> flush lost bytes: stored < written (silent close() flush failure)");
        }
      }
#endif

      if (s_book.error.length() > 0) {
        // Short write or earlier error — never promote a truncated tmp file
        // to a finalized book.
        if (s_book.tmpPath.length() > 0
            && FS.exists(s_book.tmpPath)) {
          FS.remove(s_book.tmpPath);
        }
      } else if (s_book.tmpPath.length() > 0 && up.totalSize > 0) {
        String finalPath = s_book.tmpPath.substring(0, s_book.tmpPath.length() - 4);
        if (FS.exists(finalPath)) FS.remove(finalPath);
        if (FS.rename(s_book.tmpPath, finalPath)) {
          s_book.ok = true;
        } else {
          if (FS.exists(s_book.tmpPath)) FS.remove(s_book.tmpPath);
          s_book.error = D_WEB_ERR_FINALIZE_UPLOAD;
        }
      } else {
        if (s_book.tmpPath.length() > 0 && FS.exists(s_book.tmpPath)) FS.remove(s_book.tmpPath);
        s_book.error = D_WEB_ERR_EMPTY_UPLOAD;
      }
      s_book.tmpPath = "";
    } else {
      if (s_book.tmpPath.length() > 0 && FS.exists(s_book.tmpPath)) FS.remove(s_book.tmpPath);
      if (s_book.error.length() == 0) s_book.error = D_WEB_UPLOAD_ERR_FALLBACK;
      s_book.tmpPath = "";
    }
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    UDIAG("ABORTED epub=%d calls=%u delivered=%llu written=%llu totalSize=%llu",
          (int)s_book.isEpub, (unsigned)s_book.diagWriteCalls,
          (unsigned long long)s_book.diagDelivered,
          (unsigned long long)s_book.diagWritten,
          (unsigned long long)up.totalSize);
    if (s_book.tmpFile) s_book.tmpFile.close();
    if (s_book.tmpPath.length() > 0 && FS.exists(s_book.tmpPath)) FS.remove(s_book.tmpPath);
    s_book.pendingUtf8Tail = "";
    s_book.tmpPath = "";
    s_book.ok = false;
    s_book.error = D_WEB_ERR_UPLOAD_ABORTED;
  }
}

// ============================================================================
//  Sleep image upload — straight binary, must be exactly SCREENSAVER_BYTES
//  (a full-panel 1-bit XBM; 65280 bytes on this 540x960 panel).
// ============================================================================

static void handleUploadSleepDone() {
  if (!s_sleep.ok) {
    server.send(400, "text/plain; charset=utf-8",
                s_sleep.error.length()
                  ? s_sleep.error
                  : String(D_WEB_SLEEP_UPLOAD_ERR_FALLBACK));
    return;
  }

  String inner;
  inner.reserve(500);
  inner += "<div class='card'><h2>" D_WEB_SLEEP_UPLOAD_HEADING "</h2><p class='muted'>" D_WEB_SLEEP_UPLOAD_DESC "</p><div class='actions'><a class='btn' href='/settings'>" D_WEB_BACK_TO_SETTINGS "</a><a class='btn secondary' href='/'>" D_WEB_NAV_HOME "</a></div></div>";

  String page = successPage(
    D_WEB_UPLOAD_COMPLETE_HEADING,
    D_WEB_SLEEP_UPLOAD_SUBTITLE,
    D_WEB_SLEEP_UPLOAD_BANNER,
    inner
  );
  server.send(200, "text/html; charset=utf-8", page);
}

static void handleUploadSleepStream() {
  HTTPUpload& upS = server.upload();

  if (upS.status == UPLOAD_FILE_START) {
    s_sleep.ok = false;
    s_sleep.error = "";
    s_sleep.tmpPath = "/sleep.bin.tmp";
    if (FS.exists(s_sleep.tmpPath)) FS.remove(s_sleep.tmpPath);
    s_sleep.tmpFile = FS.open(s_sleep.tmpPath, "w");
    if (!s_sleep.tmpFile) s_sleep.error = D_WEB_SLEEP_ERR_TEMP;
  }
  else if (upS.status == UPLOAD_FILE_WRITE) {
    if (s_sleep.error.length() > 0) return;
    if (s_sleep.tmpFile && upS.currentSize > 0) {
      if (s_sleep.tmpFile.size() + upS.currentSize >
          (size_t)Screensavers::SCREENSAVER_BYTES + 1024) {
        s_sleep.tmpFile.close();
        if (FS.exists(s_sleep.tmpPath)) FS.remove(s_sleep.tmpPath);
        s_sleep.error = D_WEB_SLEEP_ERR_SIZE;
        return;
      }
      size_t wrote = s_sleep.tmpFile.write(upS.buf, upS.currentSize);
      if (wrote != upS.currentSize) {
        s_sleep.tmpFile.close();
        if (FS.exists(s_sleep.tmpPath)) FS.remove(s_sleep.tmpPath);
        s_sleep.error = D_WEB_SLEEP_ERR_SAVE;
        return;
      }
    }
  }
  else if (upS.status == UPLOAD_FILE_END) {
    if (s_sleep.tmpFile) s_sleep.tmpFile.close();
    File f = FS.open(s_sleep.tmpPath, "r");
    size_t sz = f ? f.size() : 0;
    if (f) f.close();

    if (sz != (size_t)Screensavers::SCREENSAVER_BYTES) {
      if (FS.exists(s_sleep.tmpPath)) FS.remove(s_sleep.tmpPath);
      s_sleep.error = D_WEB_SLEEP_ERR_SIZE;
      s_sleep.ok = false;
    } else {
      if (FS.exists("/sleep.bin")) FS.remove("/sleep.bin");
      if (FS.rename(s_sleep.tmpPath, "/sleep.bin")) s_sleep.ok = true;
      else {
        if (FS.exists(s_sleep.tmpPath)) FS.remove(s_sleep.tmpPath);
        s_sleep.error = D_WEB_SLEEP_ERR_SAVE;
      }
    }
    s_sleep.tmpPath = "";
  }
  else if (upS.status == UPLOAD_FILE_ABORTED) {
    if (s_sleep.tmpFile) s_sleep.tmpFile.close();
    if (s_sleep.tmpPath.length() > 0 && FS.exists(s_sleep.tmpPath)) FS.remove(s_sleep.tmpPath);
    s_sleep.error = D_WEB_SLEEP_ERR_ABORTED;
    s_sleep.ok = false;
    s_sleep.tmpPath = "";
  }
}

void registerUploadRoutes() {
  server.on("/upload",       HTTP_POST, handleUploadDone,      handleUploadBookStream);
  server.on("/upload-sleep", HTTP_POST, handleUploadSleepDone, handleUploadSleepStream);
}
