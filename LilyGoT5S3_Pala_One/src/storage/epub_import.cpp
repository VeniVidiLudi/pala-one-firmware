// EPUB → plain-text flattener for Pala One.
//
// ZIP central-directory parsing, raw-DEFLATE inflate (miniz) and the HTML
// stripper/entity decoder are adapted from yetisoldier's t5-ereader-firmware
// (MIT, see LICENSE.yetisoldier). Changes for Pala One:
//   * file I/O uses an Arduino fs::File (this build: SD, see state.h's
//     `#define FS SD`) instead of POSIX,
//   * the HTML stripper streams straight to the output file
//     and drops <img>/<image> markers — this is the "basic flatten": reflowed
//     text + reading order only, no formatting/fonts. Images are extracted and
//     stored in a sibling directory.

#include "src/storage/epub_import.h"
#include "src/storage/miniz.h"
#define z_stream     mz_stream
#define Z_OK         MZ_OK
#define Z_STREAM_END MZ_STREAM_END
#define Z_NO_FLUSH   MZ_NO_FLUSH
#define Z_FINISH     MZ_FINISH
#define MAX_WBITS    15

#include <vector>
#include <cstring>
#include <cctype>

// ── small endian helpers ────────────────────────────────────────────────────
static uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void* psAlloc(size_t n) {
  void* p = ps_malloc(n);
  return p ? p : malloc(n);
}

// ── ZIP reader over an Arduino fs::File ──────────────────────────────────────
struct ZipEntry {
  String   name;
  uint32_t compressedSize;
  uint32_t uncompressedSize;
  uint32_t localHeaderOffset;
  uint16_t method;            // 0 = STORED, 8 = DEFLATE
};

class ZipReader {
public:
  bool open(fs::FS& fs, const char* path) {
    close();
    _f = fs.open(path, "r");
    if (!_f || _f.isDirectory()) { close(); return false; }
    return parseCentralDirectory();
  }
  void close() {
    if (_f) _f.close();
    _entries.clear();
  }

  size_t entryCount() const { return _entries.size(); }
  bool   hasEntry(const char* name) const {
    for (const auto& e : _entries) if (e.name == name) return true;
    return false;
  }
  void logEntries() const {
    Serial.printf("[EPUB] zip has %u entries:\n", (unsigned)_entries.size());
    for (const auto& e : _entries)
      Serial.printf("[EPUB]   '%s' (method=%u, %u->%u bytes)\n",
                    e.name.c_str(), e.method, e.compressedSize, e.uncompressedSize);
  }

  // Returns a NUL-terminated PSRAM buffer (caller frees). *outSize excludes NUL.
  uint8_t* readFile(const char* name, size_t* outSize) {
    *outSize = 0;
    for (const auto& e : _entries) {
      if (e.name != name) continue;
      if (!seekToData(e)) return nullptr;

      uint8_t* out = (uint8_t*)psAlloc(e.uncompressedSize + 1);
      if (!out) { Serial.printf("[ZIP] readFile('%s'): OOM out buf (%u bytes)\n", name, e.uncompressedSize + 1); return nullptr; }

      if (e.method == 0) {                       // STORED
        if (!readFully(out, e.uncompressedSize)) { Serial.printf("[ZIP] readFile('%s'): STORED read short\n", name); free(out); return nullptr; }
      } else if (e.method == 8) {                // DEFLATE (raw)
        uint8_t* comp = (uint8_t*)psAlloc(e.compressedSize);
        if (!comp) { Serial.printf("[ZIP] readFile('%s'): OOM comp buf (%u bytes)\n", name, e.compressedSize); free(out); return nullptr; }
        if (!readFully(comp, e.compressedSize)) { Serial.printf("[ZIP] readFile('%s'): DEFLATE read short\n", name); free(comp); free(out); return nullptr; }

        z_stream strm; memset(&strm, 0, sizeof(strm));
        strm.next_in = comp; strm.avail_in = e.compressedSize;
        strm.next_out = out; strm.avail_out = e.uncompressedSize;
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) { Serial.printf("[ZIP] readFile('%s'): inflateInit2 failed\n", name); free(comp); free(out); return nullptr; }
        int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        free(comp);
        if (ret != Z_STREAM_END && ret != Z_OK) {
          Serial.printf("[ZIP] readFile('%s'): inflate failed ret=%d (comp=%u uncomp=%u)\n",
                        name, ret, e.compressedSize, e.uncompressedSize);
          free(out); return nullptr;
        }
      } else {
        Serial.printf("[ZIP] readFile('%s'): unsupported method %u\n", name, e.method);
        free(out); return nullptr;
      }
      out[e.uncompressedSize] = 0;
      *outSize = e.uncompressedSize;
      return out;
    }
    return nullptr;
  }

private:
  fs::File _f;
  std::vector<ZipEntry> _entries;

  // fs::File::read() (Arduino SD especially) may return fewer bytes than asked
  // for a single large read, so loop until the whole chunk is in or EOF.
  bool readFully(uint8_t* dst, size_t want) {
    size_t got = 0;
    while (got < want) {
      int r = _f.read(dst + got, want - got);
      if (r <= 0) return false;
      got += (size_t)r;
    }
    return true;
  }

  bool seekToData(const ZipEntry& e) {
    if (!_f.seek(e.localHeaderOffset)) return false;
    uint8_t lfh[30];
    if (!readFully(lfh, 30)) return false;
    if (rd32(lfh) != 0x04034b50) return false;
    uint16_t nameLen = rd16(lfh + 26), extraLen = rd16(lfh + 28);
    return _f.seek(e.localHeaderOffset + 30 + nameLen + extraLen);
  }

  bool parseCentralDirectory() {
    long fileSize = _f.size();
    long searchStart = (fileSize > 65558) ? fileSize - 65558 : 0;
    long searchLen = fileSize - searchStart;
    Serial.printf("[ZIP] parse: fileSize=%ld searchStart=%ld searchLen=%ld\n",
                  fileSize, searchStart, searchLen);
    if (searchLen < 22) { Serial.println("[ZIP] FAIL: file too small for an EOCD"); return false; }
    uint8_t* buf = (uint8_t*)psAlloc(searchLen);
    if (!buf) { Serial.printf("[ZIP] FAIL: psAlloc(searchLen=%ld) returned null (OOM)\n", searchLen); return false; }
    if (!_f.seek(searchStart) || !readFully(buf, searchLen)) {
      Serial.println("[ZIP] FAIL: seek/read of EOCD search window failed"); free(buf); return false;
    }

    long eocd = -1;
    for (long i = searchLen - 22; i >= 0; i--) {
      if (buf[i] == 0x50 && buf[i+1] == 0x4b && buf[i+2] == 0x05 && buf[i+3] == 0x06) { eocd = i; break; }
    }
    if (eocd < 0) { Serial.println("[ZIP] FAIL: no EOCD signature (PK\\x05\\x06) in last 64KB"); free(buf); return false; }
    uint16_t numEntries = rd16(buf + eocd + 10);
    uint32_t cdSize     = rd32(buf + eocd + 12);
    uint32_t cdOffset   = rd32(buf + eocd + 16);
    uint16_t commentLen = rd16(buf + eocd + 20);
    Serial.printf("[ZIP] EOCD@%ld(abs %ld): numEntries=%u cdSize=%u cdOffset=%u commentLen=%u\n",
                  eocd, searchStart + eocd, numEntries, cdSize, cdOffset, commentLen);

    // ZIP64 detection: any of these sentinel values means the real numbers live
    // in a ZIP64 EOCD record this parser does not yet read. Call it out clearly
    // so a failing epub's log distinguishes "ZIP64, unsupported" from genuine
    // corruption or OOM.
    if (numEntries == 0xFFFF || cdSize == 0xFFFFFFFFUL || cdOffset == 0xFFFFFFFFUL) {
      Serial.println("[ZIP] FAIL: ZIP64 archive (sentinel field in EOCD) — not yet supported");
      free(buf); return false;
    }
    free(buf);

    if (cdOffset + cdSize > (uint32_t)fileSize) {
      Serial.printf("[ZIP] WARN: cdOffset+cdSize (%u) exceeds fileSize (%ld) — offsets may be relative/prefixed\n",
                    cdOffset + cdSize, fileSize);
    }

    uint8_t* cd = (uint8_t*)psAlloc(cdSize);
    if (!cd) { Serial.printf("[ZIP] FAIL: psAlloc(cdSize=%u) returned null (OOM)\n", cdSize); return false; }
    if (!_f.seek(cdOffset) || !readFully(cd, cdSize)) {
      Serial.printf("[ZIP] FAIL: seek/read of central directory at offset %u (size %u) failed\n", cdOffset, cdSize);
      free(cd); return false;
    }

    uint32_t pos = 0;
    int parsed = 0;
    for (int i = 0; i < numEntries && pos + 46 <= cdSize; i++) {
      if (rd32(cd + pos) != 0x02014b50) {
        Serial.printf("[ZIP] WARN: bad CD entry signature at pos %u (entry %d/%u) — stopping\n", pos, i, numEntries);
        break;
      }
      ZipEntry e;
      e.method            = rd16(cd + pos + 10);
      e.compressedSize    = rd32(cd + pos + 20);
      e.uncompressedSize  = rd32(cd + pos + 24);
      e.localHeaderOffset = rd32(cd + pos + 42);
      uint16_t nameLen  = rd16(cd + pos + 28);
      uint16_t extraLen = rd16(cd + pos + 30);
      uint16_t commLen  = rd16(cd + pos + 32);
      if (pos + 46 + nameLen > cdSize) { Serial.printf("[ZIP] WARN: nameLen overruns CD at pos %u — stopping\n", pos); break; }
      // Build the name from exactly nameLen bytes (entry names are not NUL-terminated).
      e.name.reserve(nameLen);
      for (uint16_t k = 0; k < nameLen; k++) e.name += (char)cd[pos + 46 + k];
      // Per-entry ZIP64 sentinel: real size/offset would be in the 0x0001 extra
      // field. Flag it so a partially-ZIP64 archive is diagnosable too.
      if (e.compressedSize == 0xFFFFFFFFUL || e.uncompressedSize == 0xFFFFFFFFUL ||
          e.localHeaderOffset == 0xFFFFFFFFUL) {
        Serial.printf("[ZIP] WARN: entry '%s' has a ZIP64 sentinel size/offset (unsupported)\n", e.name.c_str());
      }
      _entries.push_back(e);
      parsed++;
      pos += 46 + nameLen + extraLen + commLen;
    }
    Serial.printf("[ZIP] parsed %d/%u central-directory entries\n", parsed, numEntries);
    return !_entries.empty();
  }
};

// ── tiny XML helpers (strstr-based, no parser dependency) ────────────────────
static String xmlAttr(const char* xml, const char* tag, const char* attr) {
  const char* p = xml;
  while ((p = strstr(p, tag)) != nullptr) {
    const char* tagEnd = strchr(p, '>');
    if (!tagEnd) { p++; continue; }
    String needle = String(attr) + "=\"";
    const char* a = strstr(p, needle.c_str());
    if (a && a < tagEnd) {
      a += needle.length();
      const char* end = strchr(a, '"');
      if (end && end <= tagEnd) return String(a, end - a);
    }
    p = tagEnd;
  }
  return "";
}

static String xmlText(const char* xml, const char* tag) {
  String openTag = String("<") + tag;
  const char* p = strstr(xml, openTag.c_str());
  if (!p) return "";
  const char* start = strchr(p, '>');
  if (!start) return "";
  start++;
  String closeTag = String("</") + tag + ">";
  const char* end = strstr(start, closeTag.c_str());
  if (!end) return "";
  return String(start, end - start);
}

// ── HTML entities ────────────────────────────────────────────────────────────
static String numericEntityToUtf8(const char* body) {  // body points just past "&#"
  uint32_t cp = 0;
  if (body[0] == 'x' || body[0] == 'X') cp = strtoul(body + 1, nullptr, 16);
  else                                  cp = strtoul(body, nullptr, 10);
  if (cp == 0) return "";
  switch (cp) {
    case 160:  return " ";   case 173:  return "";
    case 8194: case 8195: case 8201: return " ";
    case 8211: return " - "; case 8212: return " -- ";
    case 8216: case 8217: case 8218: case 8242: return "'";
    case 8220: case 8221: case 8222: case 8243: return "\"";
    case 8226: return "* ";  case 8230: return "...";
    case 8249: return "<";   case 8250: return ">";
    case 8260: return "/";   case 8364: return "EUR ";
  }
  char b[5];
  if (cp < 0x80)        { b[0] = (char)cp; b[1] = 0; }
  else if (cp < 0x800)  { b[0] = 0xC0 | (cp >> 6);  b[1] = 0x80 | (cp & 0x3F); b[2] = 0; }
  else if (cp < 0x10000){ b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); b[3] = 0; }
  else                  { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F); b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); b[4] = 0; }
  return String(b);
}

// Decode a '&...;' starting at html[i]; returns "" with adv=1 if not an entity.
static String decodeEntityAt(const char* html, size_t i, size_t len, size_t& adv) {
  adv = 1;
  size_t semi = 0;
  for (size_t j = i + 1; j < len && j - i < 12; j++) {
    if (html[j] == ';') { semi = j; break; }
    if (html[j] == ' ' || html[j] == '&') break;
  }
  if (!semi) return "";  // not a well-formed entity → caller emits '&'
  String ent;
  for (size_t j = i; j <= semi; j++) ent += html[j];
  adv = semi - i + 1;
  String lower = ent; lower.toLowerCase();
  if (lower == "&amp;")  return "&";
  if (lower == "&lt;")   return "<";
  if (lower == "&gt;")   return ">";
  if (lower == "&quot;") return "\"";
  if (lower == "&apos;") return "'";
  if (lower == "&nbsp;") return " ";
  if (lower == "&mdash;")return " -- ";
  if (lower == "&ndash;")return " - ";
  if (lower == "&lsquo;"|| lower == "&rsquo;") return "'";
  if (lower == "&ldquo;"|| lower == "&rdquo;") return "\"";
  if (lower == "&hellip;") return "...";
  if (lower == "&bull;") return "* ";
  if (lower == "&shy;")  return "";
  if (ent.startsWith("&#")) return numericEntityToUtf8(ent.c_str() + 2);
  return ent;  // unknown named entity → pass through
}

// ── buffered text sink (collapses whitespace, writes to the output file) ─────
struct TextSink {
  File&   out;
  uint8_t buf[512];
  size_t  n = 0;
  size_t  total = 0;     // running count of characters emitted (for diagnostics)
  char    last = '\n';   // start in "newline" state so leading blank space is dropped
  int     nlRun = 2;     // trailing run of '\n' (start at 2 so no leading blank line)
  bool    ok = true;
  uint8_t pend[2];
  size_t  pendN = 0;


  explicit TextSink(File& f) : out(f) {}
  void flush() { if (n) { ok = ok && (out.write(buf, n) == n); n = 0; } }
  void raw(char c) {
    buf[n++] = c; total++; if (n == sizeof(buf)) flush();
    last = c; nlRun = (c == '\n') ? nlRun + 1 : 0;
  }

  // Whitespace-collapsing emit (post-normalization).
  void put(char c) {
    if (c == ' ') { if (last == ' ' || last == '\n') return; raw(' '); return; }
    if (c == '\n'){ if (last == '\n') return; raw('\n'); return; }
    raw(c);
  }
  // Emit a held prefix verbatim. Called before any raw() writer (parBreak,
  // image sentinel, end of book) so a dangling prefix from truncated UTF-8
  // can't land after bytes emitted behind its back.
  void flushPend() {
    for (size_t k = 0; k < pendN; k++) put((char)pend[k]);
    pendN = 0;
  }

  // Normalizing entry point: the same UTF-8 typography mapping as
  // normalizeTypography() (pure/text_util.cpp — keep in sync), applied
  // before the whitespace collapse. The reader faces are u8g2 `_te` fonts
  // whose glyph coverage ends at U+02BD, so literal curly quotes, dashes,
  // ellipses, NBSPs and BOMs would otherwise be silently skipped at render
  // — which looks like the converter stripped them.
  void ch(char cin) {
    uint8_t c = (uint8_t)cin;
    if (pendN == 2) {                       // held E2 80 or EF BB
      pendN = 0;
      if (pend[0] == 0xE2) {
        if (c == 0x98 || c == 0x99 || c == 0x9A || c == 0x9B) { put('\''); return; }
        if (c == 0x9C || c == 0x9D || c == 0x9E || c == 0x9F ||
            c == 0xB9 || c == 0xBA)                           { put('"');  return; }
        if (c == 0x93 || c == 0x94 || c == 0x95)              { put('-');  return; }
        if (c == 0xA6) { put('.'); put('.'); put('.'); return; }
      } else {                              // EF BB
        if (c == 0xBF) return;              // BOM / zero-width no-break space
      }
      put((char)pend[0]); put((char)pend[1]);
    } else if (pendN == 1) {                // held C2, E2 or EF
      pendN = 0;
      if (pend[0] == 0xC2) {
        if (c == 0xA0)               { put(' ');  return; }   // NBSP
        if (c == 0xAB || c == 0xBB)  { put('"');  return; }   // guillemets
        if (c == 0x91 || c == 0x92)  { put('\''); return; }   // cp1252 leak
      } else if (c == (pend[0] == 0xE2 ? 0x80 : 0xBB)) {
        pend[1] = c; pendN = 2; return;
      }
      put((char)pend[0]);
    }
    if (c == 0xC2 || c == 0xE2 || c == 0xEF) { pend[0] = c; pendN = 1; return; }
    put((char)c);
  }
  void str(const char* s) { while (*s) ch(*s++); }
  void str(const String& s) { str(s.c_str()); }
  // Verbatim string for machine-readable payloads (image sentinel paths):
  // bypasses normalization AND whitespace collapsing, either of which would
  // corrupt an SD path containing UTF-8 or doubled spaces.
  void rawStr(const char* s) { while (*s) raw(*s++); }
  void newline() { ch('\n'); }
  // Paragraph separator: ensure the stream ends with a blank line ("\n\n") so
  // the reader renders vertical space between paragraphs (matching other EPUB
  // converters). Idempotent — adjacent/nested block tags won't stack blanks —
  // and a no-op at the very start so the body doesn't open with a blank line.
  void parBreak() {
    flushPend();
    if (total == 0) return;
    while (nlRun < 2) raw('\n');
  }
};

static bool isBlockTag(const String& t) {
  return t == "p" || t == "/p" || t == "div" || t == "/div" ||
         t == "br" || t == "br/" || t == "li" || t == "/li" ||
         t == "tr" || t == "/tr" || t == "blockquote" || t == "/blockquote" ||
         t == "section" || t == "/section" || t == "article" || t == "/article" ||
         t == "aside" || t == "/aside" || t == "header" || t == "/header" ||
         t == "footer" || t == "/footer" || t == "figcaption" || t == "/figcaption" ||
         t == "ol" || t == "/ol" || t == "ul" || t == "/ul" ||
         t == "dl" || t == "/dl" || t == "dt" || t == "/dt" || t == "dd" || t == "/dd" ||
         t == "pre" || t == "/pre" || t.startsWith("h") || t.startsWith("/h");
}

// On hitting <img>/<image>, the JPEG referenced (relative to the chapter) is
// extracted from the zip to a sibling ".imgs" dir on SD, and a sentinel line
// is emitted so the reader shows it as a standalone page. Only JPEG is handled
// (the reader decodes JPEG); other formats are dropped as before.
struct ImageCtx {
  ZipReader* zip;
  fs::FS*    fs;
  String     chapterBase;   // dir of the current chapter inside the zip, e.g. "OEBPS/text/"
  String     imgDir;        // SD dir for extracted images, e.g. "/books/Foo.imgs"
  int        counter;       // next image index
};
static ImageCtx* g_imgCtx = nullptr;

// Resolve a (possibly ../-relative) href against a base dir, into a zip path.
static String resolveZipPath(const String& baseDir, const String& href) {
  String h = href;
  int hash = h.indexOf('#'); if (hash >= 0) h = h.substring(0, hash);  // strip fragment
  std::vector<String> parts;
  const String& start = h.startsWith("/") ? h : baseDir;  // absolute href ignores base
  if (!h.startsWith("/")) {
    int s = 0;
    while (s < (int)baseDir.length()) {
      int e = baseDir.indexOf('/', s); if (e < 0) e = baseDir.length();
      String seg = baseDir.substring(s, e);
      if (seg.length() && seg != ".") parts.push_back(seg);
      s = e + 1;
    }
  }
  (void)start;
  int s = 0;
  while (s < (int)h.length()) {
    int e = h.indexOf('/', s); if (e < 0) e = h.length();
    String seg = h.substring(s, e);
    if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (seg.length() && seg != ".") parts.push_back(seg);
    s = e + 1;
  }
  String out;
  for (size_t i = 0; i < parts.size(); i++) { if (i) out += "/"; out += parts[i]; }
  return out;
}

static String attrFrom(const String& tag, const char* name) {
  for (int qi = 0; qi < 2; qi++) {
    char q = qi ? '\'' : '"';
    String needle = String(name) + "=" + q;
    int i = tag.indexOf(needle);
    if (i >= 0) { int a = i + needle.length(); int e = tag.indexOf(q, a); if (e >= a) return tag.substring(a, e); }
  }
  return "";
}

static bool endsWithIgnoreCase(const String& s, const char* suf) {
  String a = s; a.toLowerCase();
  String b = suf; b.toLowerCase();
  return a.endsWith(b);
}

static void emitImage(const String& tagRaw, TextSink& sink, ImageCtx& ctx) {
  String href = attrFrom(tagRaw, "src");
  if (!href.length()) href = attrFrom(tagRaw, "xlink:href");
  if (!href.length()) href = attrFrom(tagRaw, "href");
  if (!href.length()) return;
  if (!(endsWithIgnoreCase(href, ".jpg") || endsWithIgnoreCase(href, ".jpeg"))) return;  // JPEG only

  String zipPath = resolveZipPath(ctx.chapterBase, href);
  size_t isz = 0;
  uint8_t* idata = ctx.zip->readFile(zipPath.c_str(), &isz);
  if (!idata || isz == 0) { if (idata) free(idata); return; }

  String outPath = ctx.imgDir + "/" + String(ctx.counter) + ".jpg";
  File of = ctx.fs->open(outPath, "w");
  if (of) {
    bool wrote = (of.write(idata, isz) == isz);
    of.close();
    if (wrote) {
      sink.flushPend();
      if (sink.last != '\n') sink.raw('\n');     // sentinel starts its own line
      sink.raw((char)EPUB_IMG_SENTINEL);
      sink.rawStr(outPath.c_str());
      sink.raw('\n');
      ctx.counter++;
    } else {
      ctx.fs->remove(outPath);
    }
  }
  free(idata);
}

// Stream-strip one XHTML chapter to the sink. Drops tags, decodes entities,
// turns block elements into newlines, skips <script>/<style>.
// <img>/<image> JPEGs are extracted and marked (see emitImage).
static void stripHtmlToSink(const char* html, size_t len, TextSink& sink) {
  bool inTag = false, inScript = false, inStyle = false, collecting = false;
  String tagName;
  String tagRaw;
  unsigned long lastYield = millis();

  for (size_t i = 0; i < len && html[i]; i++) {
    if ((i & 0x7FF) == 0) {
      unsigned long now = millis();
      if (now - lastYield >= 40) { sink.flush(); yield(); lastYield = now; }
    }
    char c = html[i];

    if (c == '<') { inTag = true; collecting = true; tagName = "";
      tagRaw = "";
      continue; }
    if (c == '>') {
      inTag = false; collecting = false;
      tagName.toLowerCase();
      if (tagName == "script")  inScript = true;
      if (tagName == "/script") inScript = false;
      if (tagName == "style")   inStyle = true;
      if (tagName == "/style")  inStyle = false;
      if (g_imgCtx && (tagName == "img" || tagName == "image")) emitImage(tagRaw, sink, *g_imgCtx);
      // <br> is an explicit single line break (poetry, addresses); every other
      // block element ends a paragraph and gets a blank line after it.
      if (tagName == "br" || tagName == "br/") sink.newline();
      else if (isBlockTag(tagName)) sink.parBreak();
      continue;
    }
    if (inTag) {
      tagRaw += c;   // keep the raw attributes so emitImage can read src/href
      if (collecting) {
        if (c == ' ' || c == '/' || c == '\n' || c == '\r' || c == '\t') collecting = false;
        else tagName += c;
      }
      continue;  // discard attributes (text output)
    }
    if (inScript || inStyle) continue;

    if (c == '\r') continue;
    if (c == '\n' || c == '\t') { sink.ch(' '); continue; }
    if (c == '&') {
      size_t adv = 1;
      String dec = decodeEntityAt(html, i, len, adv);
      if (adv > 1) { sink.str(dec); i += adv - 1; continue; }
      sink.ch('&');
      continue;
    }
    sink.ch(c);
  }
}

// ── orchestrator ─────────────────────────────────────────────────────────────
bool epubConvertToTxt(fs::FS& fs, const char* srcPath, const char* dstPath,
                      String* titleOut, String* authorOut, String* errOut) {
  auto fail = [&](const char* m) { if (errOut) *errOut = m; return false; };

  Serial.printf("[EPUB] convert src='%s' dst='%s'\n", srcPath, dstPath);

  ZipReader zip;
  if (!zip.open(fs, srcPath)) { Serial.println("[EPUB] zip.open failed"); return fail("Not a valid EPUB (zip)"); }

  size_t sz = 0;
  uint8_t* container = zip.readFile("META-INF/container.xml", &sz);
  if (!container) { zip.close(); return fail("EPUB missing container.xml"); }
  String opfPath = xmlAttr((const char*)container, "rootfile", "full-path");
  free(container);
  Serial.printf("[EPUB] opfPath='%s'\n", opfPath.c_str());
  if (!opfPath.length()) { zip.close(); return fail("EPUB has no OPF rootfile"); }

  uint8_t* opf = zip.readFile(opfPath.c_str(), &sz);
  if (!opf) { zip.close(); return fail("Cannot read OPF"); }
  const char* xml = (const char*)opf;

  String basePath = opfPath;
  int lastSlash = basePath.lastIndexOf('/');
  basePath = (lastSlash >= 0) ? basePath.substring(0, lastSlash + 1) : "";

  String title = xmlText(xml, "dc:title");  if (!title.length()) title = xmlText(xml, "title");
  String author = xmlText(xml, "dc:creator"); if (!author.length()) author = xmlText(xml, "creator");
  if (titleOut)  *titleOut  = title;
  if (authorOut) *authorOut = author;

  // manifest: id -> href (within zip)
  std::vector<String> ids, hrefs;
  const char* p = xml;
  while ((p = strstr(p, "<item ")) != nullptr) {
    const char* end = strchr(p, '>'); if (!end) break;
    String id, href;
    const char* a = strstr(p, "id=\"");   if (a && a < end) { a += 4; const char* e = strchr(a, '"'); if (e) id   = String(a, e - a); }
    const char* h = strstr(p, "href=\""); if (h && h < end) { h += 6; const char* e = strchr(h, '"'); if (e) href = String(h, e - h); }
    if (id.length() && href.length()) { ids.push_back(id); hrefs.push_back(basePath + href); }
    p = end + 1;
  }

  // spine order
  std::vector<String> spine;
  p = strstr(xml, "<spine");
  while (p && (p = strstr(p, "<itemref")) != nullptr) {
    String idref = xmlAttr(p, "itemref", "idref");
    if (idref.length()) {
      for (size_t k = 0; k < ids.size(); k++) if (ids[k] == idref) { spine.push_back(hrefs[k]); break; }
    }
    p += 8;
  }
  free(opf);

  Serial.printf("[EPUB] manifest items=%u  spine items=%u\n",
                (unsigned)hrefs.size(), (unsigned)spine.size());
  if (spine.empty()) { zip.close(); return fail("EPUB has no readable chapters"); }

  String tmp = String(dstPath) + ".tmp";
  if (fs.exists(tmp)) fs.remove(tmp);
  File out = fs.open(tmp, "w");
  if (!out) { zip.close(); return fail("Cannot create output file"); }

  TextSink sink(out);
  if (title.length())  { sink.str(title);  sink.newline(); }
  if (author.length()) { sink.str(author); sink.newline(); }
  sink.newline();

  // Extracted images live in a sibling dir, e.g. "/books/Foo.txt" -> "/books/Foo.imgs".
  String imgDir = String(dstPath);
  if (imgDir.endsWith(".txt")) imgDir = imgDir.substring(0, imgDir.length() - 4);
  imgDir += ".imgs";
  {                                    // clear any previous extraction so indices don't collide
    File d = fs.open(imgDir);
    if (d) {
      if (d.isDirectory()) {
        std::vector<String> old; File e;
        while ((e = d.openNextFile())) { String nm = e.name(); old.push_back(nm.startsWith("/") ? nm : (imgDir + "/" + nm)); e.close(); }
        for (auto& n : old) fs.remove(n);
      }
      d.close();
    }
  }
  if (!fs.exists(imgDir)) fs.mkdir(imgDir);
  ImageCtx imgCtx; imgCtx.zip = &zip; imgCtx.fs = &fs; imgCtx.imgDir = imgDir; imgCtx.counter = 0;
  g_imgCtx = &imgCtx;

  int written = 0;
  bool dumpedEntries = false;
  for (size_t s = 0; s < spine.size(); s++) {
    size_t csz = 0;
    uint8_t* chap = zip.readFile(spine[s].c_str(), &csz);
    if (!chap) {
      Serial.printf("[EPUB] chap[%u] '%s' -> NOT FOUND in zip\n",
                    (unsigned)s, spine[s].c_str());
      if (!dumpedEntries) { zip.logEntries(); dumpedEntries = true; }  // help diagnose a bad path
      continue;
    }
    { int sl = spine[s].lastIndexOf('/'); imgCtx.chapterBase = (sl >= 0) ? spine[s].substring(0, sl + 1) : ""; }
    // Notable chapter-break marker, before every chapter except the first.
    // raw() bypasses TextSink's whitespace collapse so these really land as
    // blank lines in the file (the reader renders an empty line as vertical
    // space); a plain ch('\n') here would collapse and disappear.
    if (written > 0) {
      if (sink.last != '\n') sink.raw('\n');  // close the previous chapter's line
      sink.raw('\n');                          // blank line above the marker
      sink.str("* * *");                       // ornament (left-aligned)
      sink.raw('\n');                          // end the marker line
      sink.raw('\n');                          // blank line below the marker
    }
    stripHtmlToSink((const char*)chap, csz, sink);
    free(chap);
    written++;
    yield();
  }
  g_imgCtx = nullptr;
  sink.flushPend();
  sink.flush();
  // NB: judge success by bytes the sink wrote, not out.size() — on SDFS the
  // file size isn't updated from buffered writes until flush/close, so reading
  // it here returns 0 even after a successful conversion.
  bool ok = sink.ok && sink.total > 0;
  out.flush();
  Serial.printf("[EPUB] done: chapters written=%d, text bytes=%u, file size=%u, sink.ok=%d\n",
                written, (unsigned)sink.total, (unsigned)out.size(), sink.ok ? 1 : 0);
  out.close();
  zip.close();

  if (!ok || written == 0) { if (fs.exists(tmp)) fs.remove(tmp); return fail("EPUB produced no text"); }
  if (fs.exists(dstPath)) fs.remove(dstPath);
  if (!fs.rename(tmp, dstPath)) { if (fs.exists(tmp)) fs.remove(tmp); return fail("Cannot finalize text file"); }
  return true;
}
