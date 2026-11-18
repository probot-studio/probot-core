#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <probot/core/lock.hpp>

namespace probot::autonomous {

  // Bounded storage keeps registration and dispatch deterministic on ESP32.
  constexpr size_t MAX_ROUTINES = 8;
  constexpr size_t MAX_ID_LENGTH = 15;
  constexpr size_t MAX_LABEL_LENGTH = 31;

  using Hook = void (*)();

  struct RoutineView {
    char id[MAX_ID_LENGTH + 1];
    char label[MAX_LABEL_LENGTH + 1];
    bool isDefault;
  };

  struct Snapshot {
    size_t count;
    int selected;
    int active;
    bool registrationError;
    RoutineView routines[MAX_ROUTINES];
  };

  class Registry {
  public:
    void beginRegistration() {
      probot::core::MuxGuard guard(_mux);
      _count = 0;
      _selected = -1;
      _active = -1;
      _defaultIndex = -1;
      _registrationError = false;
    }

    bool addRoutine(const char* id, const char* label, Hook init, Hook periodic,
                    bool isDefault = false) {
      if (!validId(id) || !validLabel(label) || !init || !periodic) {
        setRegistrationError();
        return false;
      }

      probot::core::MuxGuard guard(_mux);
      if (_count >= MAX_ROUTINES || (isDefault && _defaultIndex >= 0)) {
        _registrationError = true;
        return false;
      }
      for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_routines[i].view.id, id) == 0) {
          _registrationError = true;
          return false;
        }
      }

      Entry& entry = _routines[_count];
      copyString(entry.view.id, sizeof(entry.view.id), id);
      copyString(entry.view.label, sizeof(entry.view.label), label);
      entry.view.isDefault = isDefault;
      entry.init = init;
      entry.periodic = periodic;
      if (isDefault) _defaultIndex = static_cast<int>(_count);
      ++_count;
      return true;
    }

    // Convenient form: the routine's code name is also its DS label.
    bool addRoutine(const char* name, Hook init, Hook periodic, bool isDefault = false) {
      return addRoutine(name, name, init, periodic, isDefault);
    }

    void finishRegistration() {
      probot::core::MuxGuard guard(_mux);
      _selected = _defaultIndex;
      _active = -1;
    }

    bool hasRoutines() const {
      probot::core::MuxGuard guard(_mux);
      return _count > 0;
    }

    bool registrationError() const {
      probot::core::MuxGuard guard(_mux);
      return _registrationError;
    }

    Snapshot snapshot() const {
      probot::core::MuxGuard guard(_mux);
      Snapshot result{};
      result.count = _count;
      result.selected = _selected;
      result.active = _active;
      result.registrationError = _registrationError;
      for (size_t i = 0; i < _count; ++i) result.routines[i] = _routines[i].view;
      return result;
    }

    bool select(const char* id) {
      if (!validId(id)) return false;
      probot::core::MuxGuard guard(_mux);
      for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_routines[i].view.id, id) == 0) {
          _selected = static_cast<int>(i);
          return true;
        }
      }
      return false;
    }

    bool selectionMatches(const char* id) const {
      if (!validId(id)) return false;
      probot::core::MuxGuard guard(_mux);
      return _selected >= 0 && static_cast<size_t>(_selected) < _count &&
             strcmp(_routines[_selected].view.id, id) == 0;
    }

    // Called before the START status is published. The active routine is
    // latched for the complete run so a later UI request cannot swap callbacks.
    bool beginRun(const char* id) {
      probot::core::MuxGuard guard(_mux);
      if (_count == 0) {
        _active = -1;
        return id == nullptr || id[0] == '\0';
      }
      if (!id || _selected < 0 || static_cast<size_t>(_selected) >= _count ||
          strcmp(_routines[_selected].view.id, id) != 0) return false;
      _active = _selected;
      return true;
    }

    void dispatchInit(Hook legacy) const { dispatch(true, legacy); }
    void dispatchPeriodic(Hook legacy) const { dispatch(false, legacy); }

  private:
    struct Entry {
      RoutineView view{};
      Hook init = nullptr;
      Hook periodic = nullptr;
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

    static bool validLabel(const char* label) {
      return label && label[0] != '\0' && strnlen(label, MAX_LABEL_LENGTH + 1) <= MAX_LABEL_LENGTH;
    }

    static void copyString(char* destination, size_t capacity, const char* source) {
      size_t i = 0;
      while (i + 1 < capacity && source[i]) {
        destination[i] = source[i];
        ++i;
      }
      destination[i] = '\0';
    }

    void setRegistrationError() {
      probot::core::MuxGuard guard(_mux);
      _registrationError = true;
    }

    void dispatch(bool init, Hook legacy) const {
      Hook callback = nullptr;
      bool hasCatalog = false;
      {
        probot::core::MuxGuard guard(_mux);
        hasCatalog = _count > 0;
        const int index = (_active >= 0) ? _active : _selected;
        if (index >= 0 && static_cast<size_t>(index) < _count) {
          callback = init ? _routines[index].init : _routines[index].periodic;
        }
      }
      if (callback) callback();
      else if (!hasCatalog && legacy) legacy();
    }

    mutable probot::core::Mux _mux;
    Entry _routines[MAX_ROUTINES]{};
    size_t _count = 0;
    int _selected = -1;
    int _active = -1;
    int _defaultIndex = -1;
    bool _registrationError = false;
  };

  inline Registry& registry() {
    static Registry instance;
    return instance;
  }

  inline void clearRoutines() { registry().beginRegistration(); }
  inline bool addRoutine(const char* id, const char* label, Hook init, Hook periodic,
                         bool isDefault = false) {
    return registry().addRoutine(id, label, init, periodic, isDefault);
  }
  inline bool addRoutine(const char* name, Hook init, Hook periodic, bool isDefault = false) {
    return registry().addRoutine(name, init, periodic, isDefault);
  }
  inline bool select(const char* id) { return registry().select(id); }
  inline bool beginRun(const char* id) { return registry().beginRun(id); }
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
        case '\b': if (!append('\\') || !append('b')) return false; break;
        case '\f': if (!append('\\') || !append('f')) return false; break;
        case '\n': if (!append('\\') || !append('n')) return false; break;
        case '\r': if (!append('\\') || !append('r')) return false; break;
        case '\t': if (!append('\\') || !append('t')) return false; break;
        default:
          if (*p < 0x20) {
            if (!append('\\') || !append('u') || !append('0') || !append('0') ||
                !append(hexDigits[*p >> 4]) || !append(hexDigits[*p & 0x0f])) return false;
          } else if (!append(static_cast<char>(*p))) return false;
      }
    }
    return append('"');
  }

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
    if (!appendText(snap.registrationError
        ? "{\"supported\":true,\"registrationError\":true,\"selected\":"
        : "{\"supported\":true,\"registrationError\":false,\"selected\":")) return false;
    if (snap.selected >= 0 && static_cast<size_t>(snap.selected) < snap.count) {
      if (!appendJsonString(out, capacity, used, snap.routines[snap.selected].id)) return false;
    } else if (!appendText("null")) return false;
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
