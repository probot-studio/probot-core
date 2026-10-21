#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <probot/core/lock.hpp>
#include <probot/robot/state.hpp>

// Named OpMode options a framework offers for the Driver Station to pick
// from (WPILib HAL_SetOpModeOptions). The registry keeps its own copy of the
// strings. Config-free: safe to include from any translation unit.
//
// Threading: set() may run on any task. It builds the new tables and the
// JSON served at /opmodes on the heap, swaps them in under the lock and
// frees the old ones afterwards, so every reader does its (bounded) work
// inside the critical section.

namespace probot::robot {

  struct OpModeOption {
    uint64_t    id;           // non-zero, unique within one list
    OpMode      mode;         // AUTO, TELEOP or UTILITY
    const char* name;         // non-empty
    const char* group;        // optional, groups options in the selector
    const char* description;  // optional
  };

  class OpModeRegistry {
  public:
    static constexpr size_t kMaxOptions = 64;
    static constexpr size_t kMaxName = 63, kMaxGroup = 63, kMaxDescription = 255;

    ~OpModeRegistry() { free(_entries); free(_json); }

    // Replaces the list; nullptr/0 clears it. Returns false and keeps the
    // old list when an option is invalid (zero or duplicate id, unknown
    // mode, empty or over-long string) or memory is short.
    bool set(const OpModeOption* opts, size_t count) {
      if (count > kMaxOptions || (count && !opts)) return false;
      for (size_t i = 0; i < count; ++i) {
        const OpModeOption& o = opts[i];
        if (o.id == 0 || !validMode(o.mode) || !o.name || !*o.name) return false;
        if (strlen(o.name) > kMaxName) return false;
        if (o.group && strlen(o.group) > kMaxGroup) return false;
        if (o.description && strlen(o.description) > kMaxDescription) return false;
        for (size_t j = 0; j < i; ++j) if (opts[j].id == o.id) return false;
      }

      Entry* entries = nullptr;
      if (count) {
        entries = (Entry*)malloc(count * sizeof(Entry));
        if (!entries) return false;
        for (size_t i = 0; i < count; ++i) entries[i] = { opts[i].id, opts[i].mode };
      }
      size_t len = jsonOptions(opts, count, nullptr);
      char* json = (char*)malloc(len + 1);
      if (!json) { free(entries); return false; }
      jsonOptions(opts, count, json);

      Entry* oldEntries; char* oldJson;
      {
        probot::core::MuxGuard g(_mux);
        oldEntries = _entries; oldJson = _json;
        _entries = entries; _count = count; _json = json; _jsonLen = len;
        _version = _version + 1 ? _version + 1 : 1;
      }
      free(oldEntries); free(oldJson);
      return true;
    }

    // True when `id` is listed; its mode goes to *mode.
    bool find(uint64_t id, OpMode* mode) const {
      probot::core::MuxGuard g(_mux);
      for (size_t i = 0; i < _count; ++i) {
        if (_entries[i].id == id) { if (mode) *mode = _entries[i].mode; return true; }
      }
      return false;
    }

    bool hasMode(OpMode mode) const {
      probot::core::MuxGuard g(_mux);
      for (size_t i = 0; i < _count; ++i) if (_entries[i].mode == mode) return true;
      return false;
    }

    // Option to select for `mode`: the one last picked for it if still
    // listed, else the first listed for it, else 0.
    uint64_t preferred(OpMode mode) const {
      if (!validMode(mode)) return 0;
      probot::core::MuxGuard g(_mux);
      uint64_t first = 0, last = _last[(int)mode];
      for (size_t i = 0; i < _count; ++i) {
        if (_entries[i].mode != mode) continue;
        if (_entries[i].id == last) return last;
        if (!first) first = _entries[i].id;
      }
      return first;
    }

    void remember(OpMode mode, uint64_t id) {
      if (!validMode(mode)) return;
      probot::core::MuxGuard g(_mux);
      _last[(int)mode] = id;
    }

    // Changes on every successful set(); 0 before the first one.
    uint32_t version() const {
      probot::core::MuxGuard g(_mux);
      return _version;
    }

    // The /opmodes JSON body in pieces: copies up to `cap` bytes starting at
    // `offset` of the list with version `ver` and returns the count (0 at
    // the end). Sets *stale and returns 0 when the list changed since.
    // Before the first set() the body is the empty list.
    size_t readJson(uint32_t ver, size_t offset, char* out, size_t cap, bool* stale) const {
      static const char kEmpty[] = "{\"options\":[]}";
      probot::core::MuxGuard g(_mux);
      *stale = ver != _version;
      const char* json = _json ? _json : kEmpty;
      size_t len = _json ? _jsonLen : sizeof kEmpty - 1;
      if (*stale || offset >= len) return 0;
      size_t n = len - offset < cap ? len - offset : cap;
      memcpy(out, json + offset, n);
      return n;
    }

    // Test-only: back to the empty, never-set registry.
    void resetForTest() { set(nullptr, 0); probot::core::MuxGuard g(_mux); _version = 0; _last[0] = _last[1] = _last[2] = 0; }

    static const char* modeName(OpMode m) {
      return m == OpMode::AUTO ? "auto" : m == OpMode::UTILITY ? "utility" : "teleop";
    }

  private:
    struct Entry { uint64_t id; OpMode mode; };

    static bool validMode(OpMode m) {
      return m == OpMode::AUTO || m == OpMode::TELEOP || m == OpMode::UTILITY;
    }

    // Writes (or, with out == nullptr, measures) a JSON string literal.
    static size_t jsonString(const char* s, char* out) {
      size_t n = 0;
      auto put = [&](char c) { if (out) out[n] = c; ++n; };
      put('"');
      for (const unsigned char* p = (const unsigned char*)(s ? s : ""); *p; ++p) {
        if (*p == '"' || *p == '\\') { put('\\'); put((char)*p); }
        else if (*p < 0x20) {
          char esc[7]; snprintf(esc, sizeof esc, "\\u%04x", *p);
          for (int k = 0; k < 6; ++k) put(esc[k]);
        } else put((char)*p);
      }
      put('"');
      return n;
    }

    // {"options":[{"id":"<decimal>","mode":"auto","name":..,"group":..,"description":..},..]}
    // Ids are strings: JavaScript numbers lose integers above 2^53.
    static size_t jsonOptions(const OpModeOption* opts, size_t count, char* out) {
      size_t n = 0;
      auto raw = [&](const char* s) { size_t l = strlen(s); if (out) memcpy(out + n, s, l); n += l; };
      raw("{\"options\":[");
      for (size_t i = 0; i < count; ++i) {
        char id[64];
        snprintf(id, sizeof id, "%s{\"id\":\"%llu\",\"mode\":\"", i ? "," : "",
                 (unsigned long long)opts[i].id);
        raw(id); raw(modeName(opts[i].mode)); raw("\",\"name\":");
        n += jsonString(opts[i].name, out ? out + n : nullptr);
        raw(",\"group\":");
        n += jsonString(opts[i].group, out ? out + n : nullptr);
        raw(",\"description\":");
        n += jsonString(opts[i].description, out ? out + n : nullptr);
        raw("}");
      }
      raw("]}");
      if (out) out[n] = '\0';
      return n;
    }

    Entry*   _entries = nullptr;
    size_t   _count = 0;
    char*    _json = nullptr;
    size_t   _jsonLen = 0;
    uint32_t _version = 0;
    uint64_t _last[3] = {0, 0, 0};
    probot::core::Mux _mux;
  };

  inline OpModeRegistry& opModes() {
    static OpModeRegistry instance;
    return instance;
  }

} // namespace probot::robot
