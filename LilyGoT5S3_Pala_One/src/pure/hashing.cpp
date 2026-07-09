#include "hashing.h"

#include <cstdio>

uint32_t fnv1a32(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

uint32_t hashPath32(const String& path) {
  return fnv1a32(path.c_str());
}

String prefKeyForBook(const String& path) {
  uint32_t h = fnv1a32(path.c_str());
  char buf[16];
  std::snprintf(buf, sizeof(buf), "b_%08lx", (unsigned long)h);
  return String(buf);
}

String bmKeyFor(const String& bookKey) {
  return bookKey + "_bm";
}
