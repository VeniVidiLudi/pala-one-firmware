#pragma once
#include <Arduino.h>
#include <FS.h>

// ── Image preservation (shared by the converter and the reader) ──────────────
// Both this header's consumers (the reader and epub_import.cpp's converter)
// are separate translation units, so the flag and the page-break sentinel MUST
// be defined here so the two sides agree.
// Both halves of the feature are wired:
//   • converter (epub_import.cpp) — extracts <img>/<image> JPEGs and writes an
//     image line <0x0C><sd-path>\n at the image's reading-order position;
//   • reader — pure/paginator.cpp treats EPUB_IMG_SENTINEL as a hard page
//     boundary (so an image owns its page) and ui/epub_image.cpp decodes the
//     JPEG with JPEGDEC + ordered dithering, full-screen, no status bar.
// REQUIRES the JPEGDEC library (bitbank2) installed in the Arduino IDE.
// 0x0C (form-feed) never appears in book text, so it is a safe in-stream marker
// for "this is an image page". The converter writes one line: <0x0C><sd-path>\n
// and the reader treats that as a hard, deterministic page boundary.
#define EPUB_IMG_SENTINEL 0x0C

// Import-time EPUB support for Pala One.
//
// EPUB rendering is intentionally NOT done live. Instead, an uploaded .epub is
// flattened once to a plain UTF-8 .txt that the existing byte-offset reader
// paginates unchanged. This gives reflowed text + reading order; formatting,
// fonts and (with EPUB_KEEP_IMAGES off, as here) images are dropped — see
// epub_import.cpp.
//
// The ZIP/inflate and HTML-stripping logic is adapted from yetisoldier's
// t5-ereader-firmware (MIT) — see LICENSE.yetisoldier alongside this file.

// Flatten the EPUB at srcPath into a plain-text file at dstPath. On success
// returns true; titleOut/authorOut (optional) receive the book's metadata.
// Both source and destination live on the same fs::FS (this build: SD, see
// state.h's `#define FS SD`).
//
// state.h does `#define FS SD` (a bare-token rewrite). When a translation unit
// includes state.h before this header — web/upload.cpp does — that macro would
// mangle the `fs::FS&` parameter type below into the non-existent `fs::SD&`.
// Neutralise the macro across just this declaration; callers still pass the
// `FS` global (SD) as the argument, which binds fine to an `fs::FS&`.
#pragma push_macro("FS")
#undef FS
bool epubConvertToTxt(fs::FS& fs, const char* srcPath, const char* dstPath,
                      String* titleOut, String* authorOut, String* errOut);
#pragma pop_macro("FS")
