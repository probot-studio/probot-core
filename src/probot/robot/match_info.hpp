#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <probot/core/lock.hpp>
#include <probot/core/url.hpp>

// Match information the Driver Station sets while the robot is stopped:
// alliance station, game data, event name, match type, match and replay
// number (the WPILib DriverStation match fields). Kept in RAM only: a
// reboot clears it and the Driver Station page sends it again.
// Config-free: safe to include from any translation unit.

// probot::ds::allianceStation(), gameData(), matchInfo(), matchInfoSeq().
#define PROBOT_HAS_DS_MATCH_INFO 1

namespace probot::robot {

  // Numbered like WPILib HAL_AllianceStationID.
  enum class AllianceStation : uint8_t {
    Unknown = 0, Red1 = 1, Red2 = 2, Red3 = 3, Blue1 = 4, Blue2 = 5, Blue3 = 6
  };

  // Numbered like WPILib HAL_MatchType.
  enum class MatchType : uint8_t { None = 0, Practice = 1, Qualification = 2, Elimination = 3 };

  struct MatchInfo {
    // Text fields: UTF-8, at most this many bytes, no control characters.
    static constexpr size_t kMaxText = 64;

    AllianceStation station = AllianceStation::Unknown;
    MatchType type = MatchType::None;
    uint16_t matchNumber = 0;
    uint8_t replayNumber = 0;
    char eventName[kMaxText + 1] = {};
    char gameData[kMaxText + 1] = {};
  };

  inline bool operator==(const MatchInfo& a, const MatchInfo& b) {
    return a.station == b.station && a.type == b.type && a.matchNumber == b.matchNumber &&
           a.replayNumber == b.replayNumber && strcmp(a.eventName, b.eventName) == 0 &&
           strcmp(a.gameData, b.gameData) == 0;
  }
  inline bool operator!=(const MatchInfo& a, const MatchInfo& b) { return !(a == b); }

  namespace match_detail {
    // Well-formed UTF-8 without control characters (C0, DEL, C1).
    inline bool validText(const char* s) {
      const unsigned char* p = (const unsigned char*)s;
      while (*p) {
        unsigned c = *p;
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) { ++p; continue; }
        int extra;
        unsigned cp;
        if (c >= 0xC2 && c <= 0xDF)      { extra = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; cp = c & 0x07; }
        else return false;
        for (int i = 1; i <= extra; ++i) {
          if ((p[i] & 0xC0) != 0x80) return false;
          cp = (cp << 6) | (p[i] & 0x3F);
        }
        if ((extra == 1 && cp < 0xA0) ||
            (extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
            (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return false;
        p += extra + 1;
      }
      return true;
    }

    // Plain decimal, no sign or spaces, at most max.
    inline bool parseNumber(const char* s, uint32_t max, uint32_t* out) {
      if (!*s) return false;
      uint32_t v = 0;
      for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint32_t)(*s - '0');
        if (v > max) return false;
      }
      *out = v;
      return true;
    }

    inline bool keyIs(const char* key, size_t len, const char* name) {
      return strlen(name) == len && memcmp(key, name, len) == 0;
    }

    // JSON string of text that validText() accepted: only '"' and '\\'
    // need escaping.
    inline size_t jsonText(const char* s, char* out, size_t cap) {
      size_t n = 0;
      auto put = [&](char c) { if (n < cap) out[n] = c; ++n; };
      put('"');
      for (; *s; ++s) {
        if (*s == '"' || *s == '\\') put('\\');
        put(*s);
      }
      put('"');
      return n;
    }
  } // namespace match_detail

  // Applies a URL-encoded form (station=4&type=2&match=12&replay=0&event=..&gd=..,
  // values encoded with encodeURIComponent) to info. Missing keys keep their
  // value; unknown keys and segments without '=' are ignored. Returns nullptr on success, or the
  // reason the form is invalid; info is then unchanged.
  inline const char* applyMatchForm(const char* form, MatchInfo& info) {
    using namespace match_detail;
    MatchInfo next = info;
    const char* p = form ? form : "";
    while (*p) {
      const char* end = strchr(p, '&');
      size_t len = end ? (size_t)(end - p) : strlen(p);
      const char* eq = (const char*)memchr(p, '=', len);
      if (eq) {
        size_t keyLen = (size_t)(eq - p);
        char raw[MatchInfo::kMaxText * 3 + 1];
        size_t rawLen = len - keyLen - 1;
        char value[MatchInfo::kMaxText + 1];
        bool fits = rawLen < sizeof raw;
        if (fits) { memcpy(raw, eq + 1, rawLen); raw[rawLen] = '\0'; }
        uint32_t v = 0;
        if (keyIs(p, keyLen, "station")) {
          if (!fits || !parseNumber(raw, 6, &v)) return "station must be 0-6";
          next.station = (AllianceStation)v;
        } else if (keyIs(p, keyLen, "type")) {
          if (!fits || !parseNumber(raw, 3, &v)) return "type must be 0-3";
          next.type = (MatchType)v;
        } else if (keyIs(p, keyLen, "match")) {
          if (!fits || !parseNumber(raw, 65535, &v)) return "match must be 0-65535";
          next.matchNumber = (uint16_t)v;
        } else if (keyIs(p, keyLen, "replay")) {
          if (!fits || !parseNumber(raw, 255, &v)) return "replay must be 0-255";
          next.replayNumber = (uint8_t)v;
        } else if (keyIs(p, keyLen, "event") || keyIs(p, keyLen, "gd")) {
          if (!fits || !core::urlDecode(raw, value, sizeof value) || !validText(value))
            return "event and gd must be UTF-8 text of at most 64 bytes, no control characters";
          char* dst = keyIs(p, keyLen, "gd") ? next.gameData : next.eventName;
          memcpy(dst, value, strlen(value) + 1);
        }
      }
      if (!end) break;
      p = end + 1;
    }
    info = next;
    return nullptr;
  }

  // {"seq":N,"station":0-6,"type":0-3,"match":N,"replay":N,"event":"..","gd":".."}
  // Returns the length, or 0 when it does not fit in cap (with its NUL).
  inline size_t matchInfoJson(const MatchInfo& info, uint32_t seq, char* out, size_t cap) {
    int h = snprintf(out, cap, "{\"seq\":%lu,\"station\":%u,\"type\":%u,\"match\":%u,\"replay\":%u,\"event\":",
                     (unsigned long)seq, (unsigned)info.station, (unsigned)info.type,
                     (unsigned)info.matchNumber, (unsigned)info.replayNumber);
    if (h < 0 || (size_t)h >= cap) return 0;
    size_t n = (size_t)h;
    n += match_detail::jsonText(info.eventName, out + n, cap - n);
    if (n + 6 >= cap) return 0;
    memcpy(out + n, ",\"gd\":", 6);
    n += 6;
    n += match_detail::jsonText(info.gameData, out + n, n < cap ? cap - n : 0);
    if (n + 2 > cap) return 0;
    out[n++] = '}';
    out[n] = '\0';
    return n;
  }

  // Worst case of matchInfoJson(): both texts all quotes or backslashes.
  constexpr size_t kMatchInfoJsonMax = 112 + 2 * (2 * MatchInfo::kMaxText + 2);

  class MatchInfoStore {
  public:
    // Stores info; the change counter moves only when the content changes.
    void set(const MatchInfo& info) {
      probot::core::MuxGuard g(_mux);
      if (info == _info) return;
      _info = info;
      ++_seq;
      if (_seq == 0) _seq = 1;   // 0 means never set since boot
    }

    MatchInfo read(uint32_t* seq = nullptr) {
      probot::core::MuxGuard g(_mux);
      if (seq) *seq = _seq;
      return _info;
    }

    uint32_t seq() {
      probot::core::MuxGuard g(_mux);
      return _seq;
    }

    // Test-only: back to the boot state.
    void resetForTest(uint32_t seq = 0) { probot::core::MuxGuard g(_mux); _info = MatchInfo{}; _seq = seq; }

  private:
    MatchInfo _info;
    uint32_t _seq = 0;
    probot::core::Mux _mux;
  };

  inline MatchInfoStore& matchStore() {
    static MatchInfoStore instance;
    return instance;
  }

} // namespace probot::robot
