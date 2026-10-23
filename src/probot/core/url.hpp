#pragma once
#include <stddef.h>

namespace probot::core {

  // Decodes %XX escapes (encodeURIComponent output; '+' stays '+').
  // False on a malformed escape, an encoded NUL or when out is too small.
  inline bool urlDecode(const char* in, char* out, size_t cap) {
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    size_t n = 0;
    for (const char* p = in; *p; ++p) {
      char c = *p;
      if (c == '%') {
        int hi = hex(p[1]), lo = hi < 0 ? -1 : hex(p[2]);
        if (lo < 0) return false;
        c = (char)(hi * 16 + lo);
        if (c == '\0') return false;
        p += 2;
      }
      if (n + 1 >= cap) return false;
      out[n++] = c;
    }
    if (cap == 0) return false;
    out[n] = '\0';
    return true;
  }

} // namespace probot::core
