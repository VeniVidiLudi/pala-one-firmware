#ifndef PALA_PURE_HASHING_H
#define PALA_PURE_HASHING_H

#include "arduino_compat.h"

// FNV-1a 32-bit hash + per-book preference key derivation. Pure.

uint32_t fnv1a32(const char* s);
uint32_t hashPath32(const String& path);

// Preference-key namespace for a given book path: "b_<hash8hex>".
String prefKeyForBook(const String& path);

// Preference-key suffix for a book's bookmark blob: "<bookKey>_bm".
String bmKeyFor(const String& bookKey);

#endif  // PALA_PURE_HASHING_H
