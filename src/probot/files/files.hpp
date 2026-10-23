#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <probot/core/lifecycle.hpp>
#include <probot/core/lock.hpp>
#include <probot/core/url.hpp>
#ifdef ESP32
#include <Arduino.h>
#include <FS.h>
#endif

// Folders the Driver Station may list and download (Logs page, "Dosyalar"),
// for example the .wpilog files a program writes to LittleFS or an SD card.
// Only the files directly in a shared folder are offered, never its
// subfolders, and only while the robot is stopped: reading flash or an SD
// card competes with the robot program and the control link. Not after an
// e-stop either: the killed user task may have left a file system or SPI
// lock taken.

// share() with an Arduino FS or a VFS path, unshare().
#define PROBOT_HAS_FILES_SHARE 1

namespace probot::files {

  constexpr size_t kMaxShares = 4;
  constexpr size_t kMaxShareName = 15;     // [A-Za-z0-9_-]
  constexpr size_t kMaxDir = 95;
  constexpr size_t kMaxFileName = 127;
  constexpr size_t kMaxPath = kMaxDir + 1 + kMaxFileName;

  namespace detail {
    struct Share {
      char name[kMaxShareName + 1];
      char dir[kMaxDir + 1];               // absolute VFS path when fs is null
      void* fs;                            // fs::FS*, null for a VFS path
      uint32_t gen;                        // changes when the entry is replaced
    };

    inline Share g_shares[kMaxShares] = {};
    inline size_t g_count = 0;
    inline uint32_t g_gen = 0;
    inline probot::core::Mux g_mux;
    inline volatile uint32_t g_active = 0;  // gen of the share the file task reads, 0 when none
    constexpr uint32_t kReleaseWaitMs = 1000;

    // Waits until the file task no longer reads the share with this gen.
    // The task stops a replaced or removed share at its next file call or
    // chunk, but a stuck card or a browser that stops reading (a chunk send
    // blocks up to the server's send timeout) can hold it longer, so the
    // wait ends after kReleaseWaitMs; busy() tells whether it was released.
    // No file task on the host.
    inline void waitReleased(uint32_t gen) {
#ifdef ESP32
      uint32_t start = millis();
      while (gen && __atomic_load_n(&g_active, __ATOMIC_SEQ_CST) == gen &&
             millis() - start < kReleaseWaitMs) {
        delay(1);
      }
#else
      (void)gen;
#endif
    }

    inline bool validShareName(const char* s) {
      if (!s || !*s) return false;
      size_t n = 0;
      for (; s[n]; ++n) {
        char c = s[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok || n >= kMaxShareName) return false;
      }
      return true;
    }

    // An absolute folder path without "." or ".." segments.
    inline bool validDir(const char* d) {
      if (!d || d[0] != '/' || strlen(d) > kMaxDir) return false;
      for (const char* seg = d + 1; *seg;) {
        const char* end = strchr(seg, '/');
        size_t len = end ? (size_t)(end - seg) : strlen(seg);
        if ((len == 1 && seg[0] == '.') || (len == 2 && seg[0] == '.' && seg[1] == '.')) return false;
        if (!end) break;
        seg = end + 1;
      }
      return true;
    }

    // A plain file name: no path separators, not "." or "..", no control
    // characters. Names come from the network, so this is the guard
    // against reading outside the shared folder.
    inline bool validFileName(const char* s) {
      if (!s || !*s || strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return false;
      size_t n = 0;
      for (const unsigned char* p = (const unsigned char*)s; *p; ++p, ++n) {
        if (n >= kMaxFileName || *p < 0x20 || *p == 0x7F || *p == '/' || *p == '\\') return false;
      }
      return true;
    }

    // Whether the Driver Station may list or download files now.
    inline bool transferAllowed(robot::Phase phase, robot::Status status, bool estopped) {
      return !estopped && core::canAcceptModeChange(phase, status);
    }

    inline bool joinPath(const char* dir, const char* name, char* out, size_t cap) {
      size_t dl = strlen(dir);
      while (dl > 1 && dir[dl - 1] == '/') --dl;   // "/logs/" -> "/logs"
      bool root = dl == 1;
      int n = snprintf(out, cap, "%.*s%s%s", root ? 0 : (int)dl, dir, "/", name);
      return n > 0 && (size_t)n < cap;
    }

    using core::urlDecode;

    // Writes s as a JSON string literal; returns the length, or 0 when it
    // does not fit in cap (including the terminating NUL).
    inline size_t jsonString(const char* s, char* out, size_t cap) {
      size_t n = 0;
      auto put = [&](char c) { if (n + 1 < cap) out[n] = c; ++n; };
      put('"');
      for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { put('\\'); put((char)*p); }
        else if (*p < 0x20 || *p == 0x7F) {
          char esc[7]; snprintf(esc, sizeof esc, "\\u%04x", *p);
          for (int k = 0; k < 6; ++k) put(esc[k]);
        } else put((char)*p);
      }
      put('"');
      if (n + 1 > cap) return 0;
      out[n] = '\0';
      return n;
    }

    // One listing entry {"name":..,"size":N,"mtime":T}, with a leading
    // comma unless first. mtime is Unix seconds, 0 when unknown.
    inline size_t jsonFile(const char* name, uint64_t size, int64_t mtime, bool first,
                           char* out, size_t cap) {
      int h = snprintf(out, cap, "%s{\"name\":", first ? "" : ",");
      if (h < 0 || (size_t)h >= cap) return 0;
      size_t s = jsonString(name, out + h, cap - h);
      if (!s) return 0;
      size_t n = (size_t)h + s;
      int t = snprintf(out + n, cap - n, ",\"size\":%llu,\"mtime\":%lld}",
                       (unsigned long long)size, (long long)(mtime > 0 ? mtime : 0));
      if (t < 0 || (size_t)t >= cap - n) return 0;
      return n + (size_t)t;
    }

    // Adds a share, or replaces the one with the same name. fs null: dir
    // is a VFS path.
    inline bool addShare(const char* name, void* fs, const char* dir) {
      if (!validShareName(name) || !validDir(dir)) return false;
      uint32_t old = 0;
      {
        probot::core::MuxGuard g(g_mux);
        size_t i = 0;
        while (i < g_count && strcmp(g_shares[i].name, name) != 0) ++i;
        if (i == g_count) {
          if (g_count >= kMaxShares) return false;
          ++g_count;
        } else {
          old = g_shares[i].gen;
        }
        Share& s = g_shares[i];
        strcpy(s.name, name);
        strcpy(s.dir, dir);
        size_t dl = strlen(s.dir);
        while (dl > 1 && s.dir[dl - 1] == '/') s.dir[--dl] = '\0';   // "/logs/" -> "/logs"
        s.fs = fs;
        s.gen = ++g_gen;
      }
      waitReleased(old);
      return true;
    }

    inline bool removeShare(const char* name) {
      if (!name) return false;
      uint32_t old = 0;
      {
        probot::core::MuxGuard g(g_mux);
        for (size_t i = 0; i < g_count; ++i) {
          if (strcmp(g_shares[i].name, name) != 0) continue;
          old = g_shares[i].gen;
          for (size_t k = i + 1; k < g_count; ++k) g_shares[k - 1] = g_shares[k];
          --g_count;
          break;
        }
      }
      if (!old) return false;
      waitReleased(old);
      return true;
    }

    // Whether a share copied earlier is still offered unchanged.
    inline bool shareCurrent(const Share& sh) {
      probot::core::MuxGuard g(g_mux);
      for (size_t i = 0; i < g_count; ++i) {
        if (g_shares[i].gen == sh.gen) return true;
      }
      return false;
    }

    // File task: marks a share as being read before its first file call.
    // False, and nothing marked, when it was replaced or removed meanwhile.
    // Marking first and checking second means a remover either sees the
    // mark and waits, or the check here fails.
    inline bool enterShare(const Share& sh) {
      __atomic_store_n(&g_active, sh.gen, __ATOMIC_SEQ_CST);
      if (shareCurrent(sh)) return true;
      __atomic_store_n(&g_active, 0u, __ATOMIC_SEQ_CST);
      return false;
    }

    // After the share's directory or file handle is closed.
    inline void leaveShare() {
      __atomic_store_n(&g_active, 0u, __ATOMIC_SEQ_CST);
    }

    inline size_t count() {
      probot::core::MuxGuard g(g_mux);
      return g_count;
    }

    inline bool shareAt(size_t i, Share& out) {
      probot::core::MuxGuard g(g_mux);
      if (i >= g_count) return false;
      out = g_shares[i];
      return true;
    }

    inline bool findShare(const char* name, Share& out) {
      probot::core::MuxGuard g(g_mux);
      for (size_t i = 0; i < g_count; ++i) {
        if (strcmp(g_shares[i].name, name) == 0) { out = g_shares[i]; return true; }
      }
      return false;
    }

    // Test-only.
    inline void resetForTest() {
      probot::core::MuxGuard g(g_mux);
      g_count = 0;
      g_active = 0;
    }
  } // namespace detail

#ifdef ESP32
  // Offers the files directly in `dir` of `fs` (LittleFS, SD, SD_MMC, ...)
  // on the Driver Station under `name` (1-15 of A-Z a-z 0-9 _ -). The file
  // system must be mounted by the sketch and stay mounted while shared. A
  // share with the same name is replaced; like unshare() this waits for a
  // running transfer of the old one to stop. At most 4 shares; false for
  // an invalid name, a relative path, a path with "." or "..", or a fifth
  // name.
  inline bool share(const char* name, fs::FS& fs, const char* dir = "/") {
    return detail::addShare(name, &fs, dir);
  }
#endif

  // The same for a folder of an ESP-IDF VFS mount, by its absolute path
  // (for example "/littlefs/logs" or "/sd/logs"), read with POSIX calls.
  inline bool share(const char* name, const char* vfsDir) {
    return detail::addShare(name, nullptr, vfsDir);
  }

  // Stops offering a share; false when no share has that name. A transfer
  // from it that is still running stops at its next file call, and
  // unshare() waits for that for at most 1 s. Unmount once busy() is false.
  inline bool unshare(const char* name) {
    return detail::removeShare(name);
  }

  // Whether a Driver Station listing or download still has a folder or file
  // of any share open. A stuck card or a stalled browser can keep one open
  // a few seconds after unshare() returns.
  inline bool busy() {
    return __atomic_load_n(&detail::g_active, __ATOMIC_SEQ_CST) != 0;
  }

} // namespace probot::files
