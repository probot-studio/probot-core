#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <probot/core/lock.hpp>

// Autonomous routine chooser: a fixed catalog of autonomous routines compiled
// into the firmware, registered once at boot from autonomousConfigure(), one
// of which the Driver Station selects before START. Pure logic (no FreeRTOS,
// no NVS, no HTTP) so the host tests exercise it; the Driver Station owns the
// wire protocol and the persisted selection.
namespace probot::autonomous {

  // Bounded storage keeps registration and dispatch deterministic on ESP32.
  constexpr size_t MAX_ROUTINES = 8;
  constexpr size_t MAX_ID_LENGTH = 15;
  constexpr size_t MAX_LABEL_LENGTH = 31;
  // Large enough for the worst-case /getAutoModes body (every label made of
  // characters that need escaping); tests/test_autonomous.cpp checks it.
  constexpr size_t MODES_JSON_CAPACITY = 1280;

  using Hook = void (*)();

  // First registration problem, kept so the Driver Station can name it.
  enum class RegistrationError : uint8_t {
    NONE = 0,
    INVALID_ID,        // empty, longer than MAX_ID_LENGTH or not [A-Za-z0-9_-]
    INVALID_LABEL,     // empty, longer than MAX_LABEL_LENGTH bytes or a control character
    MISSING_CALLBACK,  // start or loop is null
    DUPLICATE_ID,
    MULTIPLE_DEFAULTS,
    TOO_MANY_ROUTINES, // more than MAX_ROUTINES
  };

  inline const char* errorCode(RegistrationError e) {
    switch (e) {
      case RegistrationError::NONE: return "none";
      case RegistrationError::INVALID_ID: return "invalid_id";
      case RegistrationError::INVALID_LABEL: return "invalid_label";
      case RegistrationError::MISSING_CALLBACK: return "missing_callback";
      case RegistrationError::DUPLICATE_ID: return "duplicate_id";
      case RegistrationError::MULTIPLE_DEFAULTS: return "multiple_defaults";
      case RegistrationError::TOO_MANY_ROUTINES: return "too_many_routines";
    }
    return "unknown";
  }

  // Why START was refused for the selected autonomous routine.
  enum class RunCheck : uint8_t { OK = 0, CATALOG_ERROR, NO_SELECTION, MISMATCH };

  struct RoutineView {
    char id[MAX_ID_LENGTH + 1];
    char label[MAX_LABEL_LENGTH + 1];
    bool isDefault;
  };

  struct Snapshot {
    size_t count;
    int selected;
    int active;
    RegistrationError error;
    // The id passed to the rejected addRoutine() call, sanitized: control
    // characters become '?', cut at a UTF-8 boundary. Empty when unknown.
    char errorId[MAX_LABEL_LENGTH + 1];
    RoutineView routines[MAX_ROUTINES];

    bool inUse() const { return count > 0 || error != RegistrationError::NONE; }
    const char* selectedId() const {
      return (selected >= 0 && static_cast<size_t>(selected) < count) ? routines[selected].id : nullptr;
    }
    const char* activeId() const {
      return (active >= 0 && static_cast<size_t>(active) < count) ? routines[active].id : nullptr;
    }
  };

  class Registry {
  public:
    void beginRegistration() {
      probot::core::MuxGuard guard(_mux);
      _count = 0;
      _selected = -1;
      _active = -1;
      _defaultIndex = -1;
      _error = RegistrationError::NONE;
      _errorId[0] = '\0';
    }

    // `start` runs once when the autonomous run STARTs (in place of
    // autonomousStart()); `loop` runs every RUN pass (in place of
    // autonomousLoop()). Returns false and records the first error otherwise.
    bool addRoutine(const char* id, const char* label, Hook start, Hook loop,
                    bool isDefault = false) {
      if (!validId(id)) return reject(RegistrationError::INVALID_ID, id);
      if (!validLabel(label)) return reject(RegistrationError::INVALID_LABEL, id);
      if (!start || !loop) return reject(RegistrationError::MISSING_CALLBACK, id);

      probot::core::MuxGuard guard(_mux);
      for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_routines[i].view.id, id) == 0) return rejectLocked(RegistrationError::DUPLICATE_ID, id);
      }
      if (isDefault && _defaultIndex >= 0) return rejectLocked(RegistrationError::MULTIPLE_DEFAULTS, id);
      if (_count >= MAX_ROUTINES) return rejectLocked(RegistrationError::TOO_MANY_ROUTINES, id);

      Entry& entry = _routines[_count];
      copyString(entry.view.id, sizeof(entry.view.id), id);
      copyString(entry.view.label, sizeof(entry.view.label), label);
      entry.view.isDefault = isDefault;
      entry.start = start;
      entry.loop = loop;
      if (isDefault) _defaultIndex = static_cast<int>(_count);
      ++_count;
      return true;
    }

    // Short form: the name is both the id and the DS label, so it follows the
    // id rules (at most 15 ASCII letters, digits, '-' or '_'; no spaces).
    bool addRoutine(const char* name, Hook start, Hook loop, bool isDefault = false) {
      return addRoutine(name, name, start, loop, isDefault);
    }

    void finishRegistration() {
      probot::core::MuxGuard guard(_mux);
      _selected = _defaultIndex;
      _active = -1;
    }

    bool inUse() const {
      probot::core::MuxGuard guard(_mux);
      return _count > 0 || _error != RegistrationError::NONE;
    }

    bool registrationError() const {
      probot::core::MuxGuard guard(_mux);
      return _error != RegistrationError::NONE;
    }

    Snapshot snapshot() const {
      probot::core::MuxGuard guard(_mux);
      Snapshot result{};
      result.count = _count;
      result.selected = _selected;
      result.active = _active;
      result.error = _error;
      memcpy(result.errorId, _errorId, sizeof(result.errorId));
      for (size_t i = 0; i < _count; ++i) result.routines[i] = _routines[i].view;
      return result;
    }

    // Selects a registered routine. The caller (Driver Station) decides when
    // a selection is allowed; a catalog with a registration error accepts none.
    bool select(const char* id) {
      if (!validId(id)) return false;
      probot::core::MuxGuard guard(_mux);
      if (_error != RegistrationError::NONE) return false;
      const int index = findLocked(id);
      if (index < 0) return false;
      _selected = index;
      return true;
    }

    // Boot-time restore of a persisted selection. An id that is no longer in
    // the catalog (a different sketch was uploaded) keeps the default.
    bool restoreSelection(const char* id) { return select(id); }

    // START gate, called by the Driver Station before START is published. On
    // OK the selected routine is latched as active for the whole run, so a
    // later selection cannot swap callbacks mid-run. `id` is the selection
    // the DS page believes is current; it must match the robot's.
    RunCheck beginRun(const char* id) {
      probot::core::MuxGuard guard(_mux);
      _active = -1;
      if (_error != RegistrationError::NONE) return RunCheck::CATALOG_ERROR;
      if (_selected < 0 || static_cast<size_t>(_selected) >= _count) return RunCheck::NO_SELECTION;
      if (!id || strcmp(_routines[_selected].view.id, id) != 0) return RunCheck::MISMATCH;
      _active = _selected;
      return RunCheck::OK;
    }

    // Clears the latched routine; called on INIT and after the autonomous
    // stop hook, so no later dispatch can reach a finished run's callbacks.
    void endRun() {
      probot::core::MuxGuard guard(_mux);
      _active = -1;
    }

    // With a catalog only the latched routine is dispatched (never the bare
    // selection); without one the sketch's own hook runs unchanged.
    void dispatchStart(Hook legacy) const { dispatch(true, legacy); }
    void dispatchLoop(Hook legacy) const { dispatch(false, legacy); }

  private:
    struct Entry {
      RoutineView view{};
      Hook start = nullptr;
      Hook loop = nullptr;
    };

    static bool validId(const char* id) {
      if (!id || id[0] == '\0' || strnlen(id, MAX_ID_LENGTH + 1) > MAX_ID_LENGTH) return false;
      for (const char* p = id; *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
      }
      return true;
    }

    // UTF-8 is allowed; control characters are not (they would need \u
    // escapes and have no place in a drop-down label).
    static bool validLabel(const char* label) {
      if (!label || label[0] == '\0' || strnlen(label, MAX_LABEL_LENGTH + 1) > MAX_LABEL_LENGTH) return false;
      for (const unsigned char* p = reinterpret_cast<const unsigned char*>(label); *p; ++p) {
        if (*p < 0x20 || *p == 0x7f) return false;
      }
      return true;
    }

    static void copyString(char* destination, size_t capacity, const char* source) {
      size_t i = 0;
      while (i + 1 < capacity && source[i]) {
        destination[i] = source[i];
        ++i;
      }
      destination[i] = '\0';
    }

    static void copyErrorId(char* destination, size_t capacity, const char* source) {
      size_t i = 0;
      if (source) {
        while (i + 1 < capacity && source[i]) {
          const unsigned char c = static_cast<unsigned char>(source[i]);
          destination[i] = (c < 0x20 || c == 0x7f) ? '?' : source[i];
          ++i;
        }
        // Drop a multi-byte character that the cut left incomplete.
        if (source[i] != '\0') {
          size_t lead = i;
          while (lead > 0 && (static_cast<unsigned char>(destination[lead - 1]) & 0xC0) == 0x80) --lead;
          if (lead > 0) {
            const unsigned char b = static_cast<unsigned char>(destination[lead - 1]);
            const size_t need = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : (b >= 0xC0) ? 2 : 1;
            if (i - (lead - 1) < need) i = lead - 1;
          }
        }
      }
      destination[i] = '\0';
    }

    int findLocked(const char* id) const {
      for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_routines[i].view.id, id) == 0) return static_cast<int>(i);
      }
      return -1;
    }

    bool reject(RegistrationError e, const char* id) {
      probot::core::MuxGuard guard(_mux);
      return rejectLocked(e, id);
    }

    bool rejectLocked(RegistrationError e, const char* id) {
      if (_error == RegistrationError::NONE) {
        _error = e;
        copyErrorId(_errorId, sizeof(_errorId), id);
      }
      return false;
    }

    void dispatch(bool start, Hook legacy) const {
      Hook callback = nullptr;
      bool inUse = false;
      {
        probot::core::MuxGuard guard(_mux);
        inUse = _count > 0 || _error != RegistrationError::NONE;
        if (_active >= 0 && static_cast<size_t>(_active) < _count) {
          callback = start ? _routines[_active].start : _routines[_active].loop;
        }
      }
      if (callback) callback();
      else if (!inUse && legacy) legacy();
    }

    mutable probot::core::Mux _mux;
    Entry _routines[MAX_ROUTINES]{};
    size_t _count = 0;
    int _selected = -1;
    int _active = -1;
    int _defaultIndex = -1;
    RegistrationError _error = RegistrationError::NONE;
    char _errorId[MAX_LABEL_LENGTH + 1] = {};
  };

  inline Registry& registry() {
    static Registry instance;
    return instance;
  }

  inline void clearRoutines() { registry().beginRegistration(); }
  inline bool addRoutine(const char* id, const char* label, Hook start, Hook loop,
                         bool isDefault = false) {
    return registry().addRoutine(id, label, start, loop, isDefault);
  }
  inline bool addRoutine(const char* name, Hook start, Hook loop, bool isDefault = false) {
    return registry().addRoutine(name, start, loop, isDefault);
  }
  inline Snapshot snapshot() { return registry().snapshot(); }

  inline bool appendJsonString(char* out, size_t capacity, size_t& used, const char* value) {
    if (!out || !value || used >= capacity) return false;
    auto append = [&](char c) {
      if (used + 1 >= capacity) return false;
      out[used++] = c;
      return true;
    };
    if (!append('"')) return false;
    static const char hexDigits[] = "0123456789abcdef";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
      switch (*p) {
        case '"': if (!append('\\') || !append('"')) return false; break;
        case '\\': if (!append('\\') || !append('\\')) return false; break;
        default:
          if (*p < 0x20) {
            if (!append('\\') || !append('u') || !append('0') || !append('0') ||
                !append(hexDigits[*p >> 4]) || !append(hexDigits[*p & 0x0f])) return false;
          } else if (!append(static_cast<char>(*p))) return false;
      }
    }
    return append('"');
  }

  // Body of GET /getAutoModes:
  // {"supported":b,"registrationError":b,"error":null|{"code":s,"id":s},
  //  "selected":s|null,"active":s|null,"modes":[{"id":s,"label":s,"default":b},...]}
  inline bool buildModesJson(const Snapshot& snap, char* out, size_t capacity) {
    if (!out || capacity == 0) return false;
    size_t used = 0;
    auto appendText = [&](const char* value) {
      const size_t len = strlen(value);
      if (used + len >= capacity) return false;
      memcpy(out + used, value, len);
      used += len;
      return true;
    };
    auto appendIdOrNull = [&](const char* id) {
      return id ? appendJsonString(out, capacity, used, id) : appendText("null");
    };
    const bool failed = snap.error != RegistrationError::NONE;
    if (!appendText(snap.inUse() ? "{\"supported\":true" : "{\"supported\":false")) return false;
    if (!appendText(failed ? ",\"registrationError\":true,\"error\":{\"code\":"
                           : ",\"registrationError\":false,\"error\":null")) return false;
    if (failed) {
      if (!appendJsonString(out, capacity, used, errorCode(snap.error))) return false;
      if (!appendText(",\"id\":")) return false;
      if (!appendJsonString(out, capacity, used, snap.errorId)) return false;
      if (!appendText("}")) return false;
    }
    if (!appendText(",\"selected\":") || !appendIdOrNull(snap.selectedId())) return false;
    if (!appendText(",\"active\":") || !appendIdOrNull(snap.activeId())) return false;
    if (!appendText(",\"modes\":[")) return false;
    for (size_t i = 0; i < snap.count; ++i) {
      if (i && !appendText(",")) return false;
      if (!appendText("{\"id\":")) return false;
      if (!appendJsonString(out, capacity, used, snap.routines[i].id)) return false;
      if (!appendText(",\"label\":")) return false;
      if (!appendJsonString(out, capacity, used, snap.routines[i].label)) return false;
      if (!appendText(snap.routines[i].isDefault ? ",\"default\":true}" : ",\"default\":false}")) return false;
    }
    if (!appendText("]}")) return false;
    out[used] = '\0';
    return true;
  }

} // namespace probot::autonomous
