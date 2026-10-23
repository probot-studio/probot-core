#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <probot/core/lock.hpp>

// Dashboard topic store: named, typed values published by robot code
// (telemetry, tunables) and streamed to the Driver Station, plus a queue of
// values the Driver Station writes back. Config-free: safe to include from
// any translation unit. Limits are runtime values applied by
// runtime_setup() from the sketch's PROBOT_DASH_* macros.
//
// Threading: any task may publish/set; the Driver Station push task (core 0)
// serializes frames with detail::buildFrame(); the WebSocket handler
// (core 0) queues writes with detail::pushWrite(); robot code drains them
// with drainWrites() on its own task. Every critical section only copies:
// heap work happens before or after the lock.
//
// Values are opaque little-endian bytes (see ValueType); the core only
// compares them to drop duplicates. A topic's payload must not exceed
// Limits::maxValue.

// PERSISTENT topics keep Driver Station edits across reboots (only edits
// robot code accepted), with clearPersisted().
#define PROBOT_HAS_DASH_PERSIST 1

namespace probot::dashboard {

  using TopicId = uint16_t;
  constexpr TopicId kInvalid = 0xFFFF;

  enum class ValueType : uint8_t {
    Boolean = 1, Int = 2, Float = 3, Double = 4, String = 5, Raw = 6,
    BooleanArray = 7, IntArray = 8, FloatArray = 9, DoubleArray = 10, StringArray = 11
  };

  enum Flags : uint8_t {
    TELEMETRY = 1, TUNABLE = 2, MUTABLE = 4, ROBUST = 8, KEEP_DUPLICATES = 16, PERSISTENT = 32
  };

  // Status carried by a 'K' frame when a Driver Station write is refused.
  enum class WriteStatus : uint8_t {
    Ok = 0, UnknownId = 1, Immutable = 2, TypeMismatch = 3, DecodeError = 4, QueueFull = 5
  };

  // Reason carried by an 'E' frame: the robot's last set() of a topic was
  // refused, so the Driver Station still shows an older value.
  enum class SetRefusal : uint8_t { None = 0, TooLarge = 1, NoMemory = 2 };

  struct Write { TopicId id; ValueType vt; uint16_t len; const uint8_t* data; };

  struct Stats { uint16_t topics, maxTopics; uint32_t bytesUsed, bytesMax, dropped; };

  struct Limits {
    uint16_t maxTopics;        // at most 255
    uint16_t maxSchemas;
    uint32_t maxValue;         // bytes per value or property string
    uint32_t poolBytes;        // names, types, properties, values, schemas
    uint32_t writeQueueBytes;  // pending Driver Station writes
    uint32_t maxSchemaBytes;   // bytes per schema (protobuf descriptors exceed a value)
  };

  namespace detail {

    enum : uint8_t { SLOT_FREE = 0, SLOT_LIVE = 1, SLOT_REMOVING = 2 };

    struct Slot {
      uint8_t  state, gen, vt, flags;
      bool     announced, dirty, hasValue, writePending;
      uint32_t hash;
      char*    name;
      char*    type;
      char*    props;
      uint8_t* value;
      uint32_t len, cap;
      uint8_t* write;
      uint16_t writeLen;
      // Persistence (PERSISTENT topics). Only values the robot accepted
      // from a Driver Station write are stored: a drained write is the
      // candidate (cand) until rejectWrite() drops it, or a newer write is
      // drained or kPersistDebounceMs pass; it then becomes the value to
      // store (tuned). restorePending: the pending write is the stored
      // value. restoreDelivered / dsDelivered: the last drained write was the
      // stored value / a Driver Station write. eraseStored: the stored value
      // was refused. restoreFailed: it could not be read (reported once).
      // oversize: an accepted write over kMaxPersistValue, reported instead
      // of kept.
      bool     restorePending, restoreDelivered, dsDelivered, eraseStored, restoreFailed, failReported;
      bool     oversize;
      // The last set() was refused (SetRefusal) for a value of refusedLen
      // bytes (the latest one); refusedSent once the 'E' frame went out
      // after the value.
      uint8_t  refused;
      bool     refusedSent;
      uint32_t refusedLen;
      bool     candFresh;                    // persistTick() has not seen cand yet
      uint8_t* cand;
      uint8_t* tuned;
      uint16_t candLen, tunedLen;
      uint32_t candSince;                    // when persistTick() first saw cand
    };

    struct Schema { char* name; char* type; uint8_t* data; uint16_t len; bool sent; };

    struct Ack { TopicId id; uint8_t status; };
    constexpr size_t kAcks = 8;

    struct Store {
      core::Mux mux;
      Limits    lim{};
      bool      configured = false;
      Slot*     slots = nullptr;
      Schema*   schemas = nullptr;
      uint16_t* fifo = nullptr;          // slot indices with a pending write
      uint16_t  fifoHead = 0, fifoCount = 0;
      uint16_t  topics = 0;
      uint32_t  bytesUsed = 0, writeBytes = 0, dropped = 0;
      bool      resync = false;
      uint16_t  valueCursor = 0;         // round-robin start of the next 'V' scan
      uint16_t  freeCursor = 0;          // round-robin start of the free-slot search
      Ack       acks[kAcks]{};
      uint8_t   ackHead = 0, ackCount = 0;
      volatile uint32_t draining = 0;
      bool      persistUsed = false;     // a PERSISTENT topic was published
      uint32_t  persistEpoch = 0;        // bumped by clearPersisted()
      bool      persistClearing = false;  // clearPersisted() has not erased yet
    };

    inline Store g_store;

    // Flash storage for PERSISTENT topics. The runtime installs NVS on the
    // ESP32; it is null on the host unless a test installs one.
    struct PersistBackend {
      // Copies key's record into buf and returns its length; 0 when the key
      // is absent or longer than cap (not a record this firmware stored);
      // -1 when the storage could not be read.
      long   (*load)(const char* key, uint8_t* buf, size_t cap);
      bool   (*save)(const char* key, const uint8_t* data, size_t len);
      bool   (*erase)(const char* key);
      bool   (*eraseAll)();
    };
    inline const PersistBackend* g_persist = nullptr;

    // A drained Driver Station write not refused for this long is accepted.
    constexpr uint32_t kPersistDebounceMs = 2000;
    // Largest stored value: NVS is small and shared with WiFi.
    constexpr uint32_t kMaxPersistValue = 2048;
    constexpr uint8_t  kRecordVersion = 1;
    constexpr size_t   kKeyLen = 10;       // "d" + 8 hex digits + NUL (NVS keys: 15 chars)

    // Record layout: version, vt, name length, type length, name, type,
    // u16 value length, value. The name and type are checked on restore.
    inline size_t recordCapacity(uint32_t maxValue) {
      return 4 + 255 + 255 + 2 + (size_t)(maxValue < kMaxPersistValue ? maxValue : kMaxPersistValue);
    }

    // Topics whose paths share a hash share a key; the restore check then
    // refuses the other topic's record.
    inline void persistKey(uint32_t hash, char out[kKeyLen]) {
      static const char hex[] = "0123456789abcdef";
      out[0] = 'd';
      for (int i = 0; i < 8; ++i) out[1 + i] = hex[(hash >> (28 - 4 * i)) & 0xF];
      out[9] = '\0';
    }

    inline size_t encodeRecord(const char* name, uint8_t vt, const char* type,
                               const uint8_t* value, size_t len, uint8_t* out, size_t cap) {
      size_t nl = strlen(name), tl = strlen(type);
      size_t n = 4 + nl + tl + 2 + len;
      if (nl > 255 || tl > 255 || len > 0xFFFF || n > cap) return 0;
      size_t w = 0;
      out[w++] = kRecordVersion; out[w++] = vt; out[w++] = (uint8_t)nl; out[w++] = (uint8_t)tl;
      memcpy(out + w, name, nl); w += nl;
      memcpy(out + w, type, tl); w += tl;
      out[w++] = (uint8_t)len; out[w++] = (uint8_t)(len >> 8);
      if (len) memcpy(out + w, value, len);
      return n;
    }

    struct Record {
      uint8_t vt, nameLen, typeLen;
      const char* name;
      const char* type;
      const uint8_t* value;
      uint16_t len;
    };

    inline bool decodeRecord(const uint8_t* b, size_t n, Record& r) {
      if (n < 6 || b[0] != kRecordVersion) return false;
      r.vt = b[1]; r.nameLen = b[2]; r.typeLen = b[3];
      size_t p = 4 + (size_t)r.nameLen + r.typeLen;
      if (n < p + 2) return false;
      r.name = (const char*)b + 4;
      r.type = (const char*)b + 4 + r.nameLen;
      r.len = (uint16_t)(b[p] | (b[p + 1] << 8));
      r.value = b + p + 2;
      return n == p + 2 + r.len;
    }

    // Frame type bytes, robot to Driver Station and back.
    constexpr uint8_t FRAME_ANNOUNCE = 'A', FRAME_VALUES = 'V', FRAME_REMOVE = 'R',
                      FRAME_SCHEMA = 'C', FRAME_ACK = 'K', FRAME_WRITE = 'W',
                      FRAME_RESYNC = 'Q', FRAME_REFUSED = 'E';
    // Batched frames stop growing past this many bytes (one record may exceed it).
    constexpr size_t kFrameTarget = 1400;

    inline uint32_t hashOf(const char* s) {
      uint32_t h = 2166136261u;
      while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
      return h;
    }

    inline char* dupString(const char* s, size_t n) {
      char* p = (char*)malloc(n + 1);
      if (p) { memcpy(p, s, n); p[n] = '\0'; }
      return p;
    }

    inline TopicId makeId(uint16_t idx, uint8_t gen) { return (TopicId)(idx | ((uint16_t)gen << 8)); }

    // Caller holds the lock.
    inline Slot* liveSlot(Store& st, TopicId id) {
      uint16_t idx = id & 0xFF;
      if (!st.slots || idx >= st.lim.maxTopics) return nullptr;
      Slot& s = st.slots[idx];
      if (s.state != SLOT_LIVE || s.gen != (uint8_t)(id >> 8)) return nullptr;
      return &s;
    }

    // Caller holds the lock.
    inline bool reserve(Store& st, uint32_t n) {
      if (st.bytesUsed + n > st.lim.poolBytes) return false;
      st.bytesUsed += n;
      return true;
    }

    // Caller holds the lock.
    inline Slot* findLive(Store& st, const char* path, uint32_t hash) {
      if (!st.slots) return nullptr;
      for (uint16_t i = 0; i < st.lim.maxTopics; ++i) {
        Slot& s = st.slots[i];
        if (s.state == SLOT_LIVE && s.hash == hash && strcmp(s.name, path) == 0) return &s;
      }
      return nullptr;
    }

    // Caller holds the lock.
    inline void dropFromFifo(Store& st, uint16_t idx) {
      uint16_t n = st.fifoCount, kept = 0, cap = st.lim.maxTopics;
      for (uint16_t i = 0; i < n; ++i) {
        uint16_t v = st.fifo[(st.fifoHead + i) % cap];
        if (v != idx) st.fifo[(st.fifoHead + kept++) % cap] = v;
      }
      st.fifoCount = kept;
    }

    // A Driver Station payload has the exact layout of its value type, so
    // robot code can decode it without re-checking lengths.
    inline bool wellFormed(uint8_t vt, const uint8_t* data, size_t len) {
      switch ((ValueType)vt) {
        case ValueType::Boolean:     return len == 1;
        case ValueType::Int:
        case ValueType::Double:      return len == 8;
        case ValueType::Float:       return len == 4;
        case ValueType::IntArray:
        case ValueType::DoubleArray: return len % 8 == 0;
        case ValueType::FloatArray:  return len % 4 == 0;
        case ValueType::StringArray: {
          size_t p = 0;
          while (p < len) {
            if (len - p < 2) return false;
            size_t n = (size_t)(data[p] | (data[p + 1] << 8));
            p += 2;
            if (len - p < n) return false;
            p += n;
          }
          return true;
        }
        default:                     return true;
      }
    }

    // Bytes a slot's pending write counts against Limits::writeQueueBytes;
    // a restored value does not count. Caller holds the lock.
    inline uint32_t queuedBytes(const Slot& s) {
      return s.writePending && !s.restorePending ? s.writeLen : 0;
    }

    // Caller holds the lock. Only a new reason is sent at once: a value
    // that keeps growing would otherwise send a frame every tick. The
    // latest length goes out with the next resend.
    inline void markRefused(Slot& s, SetRefusal why, size_t len) {
      s.refusedLen = (uint32_t)len;
      if (s.refused == (uint8_t)why) return;
      s.refused = (uint8_t)why; s.refusedSent = false;
    }

    // Caller holds the lock. The value is sent even if it equals the old
    // one, since the Driver Station clears the refusal on the next 'V'.
    inline void clearRefused(Slot& s) {
      if (!s.refused) return;
      s.refused = 0;
      s.dirty = true;
    }

    // Caller holds the lock.
    inline void pushAck(Store& st, TopicId id, WriteStatus status) {
      if (st.ackCount == kAcks) { st.ackHead = (uint8_t)((st.ackHead + 1) % kAcks); --st.ackCount; }
      st.acks[(st.ackHead + st.ackCount) % kAcks] = Ack{ id, (uint8_t)status };
      ++st.ackCount;
    }

    // Tables are allocated on first use so sketches that never publish pay
    // nothing beyond this struct.
    inline bool ensureTables(Store& st) {
      {
        core::MuxGuard g(st.mux);
        if (!st.configured) return false;
        if (st.slots) return true;
      }
      Limits lim = st.lim;
      auto* slots   = (Slot*)calloc(lim.maxTopics, sizeof(Slot));
      auto* schemas = (Schema*)calloc(lim.maxSchemas ? lim.maxSchemas : 1, sizeof(Schema));
      auto* fifo    = (uint16_t*)calloc(lim.maxTopics, sizeof(uint16_t));
      bool installed = false;
      if (slots && schemas && fifo) {
        core::MuxGuard g(st.mux);
        if (!st.slots) {
          st.slots = slots; st.schemas = schemas; st.fifo = fifo;
          installed = true;
        }
      }
      if (!installed) { free(slots); free(schemas); free(fifo); }
      core::MuxGuard g(st.mux);
      return st.slots != nullptr;
    }

    // Applies the limits once; runtime_setup() calls it before any task runs.
    inline bool configure(const Limits& lim) {
      Store& st = g_store;
      core::MuxGuard g(st.mux);
      if (st.configured) return false;
      st.lim = lim;
      if (st.lim.maxTopics > 255) st.lim.maxTopics = 255;
      if (st.lim.maxValue > 16384) st.lim.maxValue = 16384;
      if (st.lim.maxSchemaBytes > 32768) st.lim.maxSchemaBytes = 32768;
      st.configured = true;
      return true;
    }

    inline uint32_t maxValue() {
      core::MuxGuard g(g_store.mux);
      return g_store.lim.maxValue;
    }

    // True once anything was published: the push task allocates its frame
    // buffer only then.
    inline bool active() {
      core::MuxGuard g(g_store.mux);
      return g_store.slots != nullptr;
    }

    // Largest frame buildFrame() can produce with the configured limits.
    inline size_t frameCapacity() {
      Store& st = g_store;
      core::MuxGuard g(st.mux);
      size_t named = 1 + 255 + 1 + 255;           // name and type with lengths
      size_t announce = 1 + 2 + 2 + named + 2 + st.lim.maxValue;
      size_t values = 1 + 4 + 4 + st.lim.maxValue;
      size_t schema = 1 + named + 2 + st.lim.maxSchemaBytes;
      size_t m = announce > values ? announce : values;
      if (schema > m) m = schema;
      return m > kFrameTarget ? m : kFrameTarget;
    }

    // Driver Station asked for everything again ('Q', or a new client).
    inline void requestResync() {
      core::MuxGuard g(g_store.mux);
      g_store.resync = true;
    }

    // Queues a Driver Station write (WebSocket handler, core 0). On refusal
    // the caller sends nothing: a 'K' frame is queued here.
    inline WriteStatus pushWrite(TopicId id, uint8_t vt, const uint8_t* data, size_t len) {
      Store& st = g_store;
      WriteStatus status = WriteStatus::Ok;
      {
        core::MuxGuard g(st.mux);
        Slot* s = liveSlot(st, id);
        if (!s) status = WriteStatus::UnknownId;
        else if ((s->flags & (TUNABLE | MUTABLE)) != (TUNABLE | MUTABLE)) status = WriteStatus::Immutable;
        else if (s->vt != vt) status = WriteStatus::TypeMismatch;
        else if (len > st.lim.maxValue || !wellFormed(vt, data, len)) status = WriteStatus::DecodeError;
        else if (st.writeBytes - (queuedBytes(*s)) + len > st.lim.writeQueueBytes)
          status = WriteStatus::QueueFull;
        if (status != WriteStatus::Ok) {
          if (status == WriteStatus::QueueFull) ++st.dropped;
          pushAck(st, id, status);
          return status;
        }
      }
      uint8_t* buf = (uint8_t*)malloc(len ? len : 1);
      if (!buf) {
        core::MuxGuard g(st.mux);
        ++st.dropped;
        pushAck(st, id, WriteStatus::QueueFull);
        return WriteStatus::QueueFull;
      }
      memcpy(buf, data, len);
      uint8_t* old = nullptr;
      {
        core::MuxGuard g(st.mux);
        Slot* s = liveSlot(st, id);
        if (!s) {
          pushAck(st, id, WriteStatus::UnknownId);
          status = WriteStatus::UnknownId;
          old = buf;
        } else {
          if (s->writePending) { old = s->write; st.writeBytes -= queuedBytes(*s); }
          else {
            st.fifo[(st.fifoHead + st.fifoCount) % st.lim.maxTopics] = id & 0xFF;
            ++st.fifoCount;
          }
          // Last writer wins: a newer edit replaces the pending one in place.
          s->write = buf; s->writeLen = (uint16_t)len; s->writePending = true;
          s->restorePending = false;
          st.writeBytes += len;
        }
      }
      free(old);
      return status;
    }

    // Queues the stored value of a newly published PERSISTENT topic as a
    // write, so robot code applies it with its first drainWrites() like a
    // Driver Station edit. Runs on the publishing task. Restores do not
    // count against Limits::writeQueueBytes (at most one per topic). A
    // storage read error is reported by persistTick().
    inline void restore(Store& st, TopicId id, const char* path, ValueType vt, const char* type) {
      const PersistBackend* be = __atomic_load_n(&g_persist, __ATOMIC_ACQUIRE);
      if (!be) return;
      uint32_t maxValue;
      {
        core::MuxGuard g(st.mux);
        maxValue = st.lim.maxValue;
      }
      const size_t cap = recordCapacity(maxValue);
      uint8_t* rec = (uint8_t*)malloc(cap);
      char key[kKeyLen];
      persistKey(hashOf(path), key);
      Record r{};
      uint8_t* buf = nullptr;
      long n = rec ? be->load(key, rec, cap) : -1;
      bool failed = n < 0;
      if (n > 0 && decodeRecord(rec, (size_t)n, r) && r.vt == (uint8_t)vt &&
          r.nameLen == strlen(path) && memcmp(r.name, path, r.nameLen) == 0 &&
          r.typeLen == strlen(type) && memcmp(r.type, type, r.typeLen) == 0 &&
          r.len <= maxValue && wellFormed(r.vt, r.value, r.len)) {
        buf = (uint8_t*)malloc(r.len ? r.len : 1);
        if (buf) memcpy(buf, r.value, r.len);
        else failed = true;
      }
      free(rec);
      if (failed) {
        core::MuxGuard g(st.mux);
        if (Slot* s = liveSlot(st, id)) s->restoreFailed = true;
        return;
      }
      if (!buf) return;
      {
        core::MuxGuard g(st.mux);
        Slot* s = liveSlot(st, id);
        if (s && !s->writePending) {
          st.fifo[(st.fifoHead + st.fifoCount) % st.lim.maxTopics] = id & 0xFF;
          ++st.fifoCount;
          s->write = buf; s->writeLen = r.len; s->writePending = true;
          s->restorePending = true;
          buf = nullptr;
        }
      }
      free(buf);
    }

    // Writes the next frame to send into buf and returns its length, or 0
    // when nothing is pending. Order: removes, schemas, announces, acks,
    // values, so a topic is always announced before its value.
    inline size_t buildFrame(uint8_t* buf, size_t cap, uint32_t nowMs) {
      Store& st = g_store;
      core::MuxGuard g(st.mux);
      if (!st.slots) return 0;
      const uint16_t n = st.lim.maxTopics;
      const size_t target = cap < kFrameTarget ? cap : kFrameTarget;

      if (st.resync) {
        st.resync = false;
        for (uint16_t i = 0; i < n; ++i) if (st.slots[i].state == SLOT_LIVE) st.slots[i].announced = false;
        for (uint16_t i = 0; i < st.lim.maxSchemas; ++i) st.schemas[i].sent = false;
      }

      size_t w = 1;
      buf[0] = FRAME_REMOVE;
      for (uint16_t i = 0; i < n && w + 2 <= target; ++i) {
        Slot& s = st.slots[i];
        if (s.state != SLOT_REMOVING) continue;
        TopicId id = makeId(i, s.gen);
        buf[w++] = (uint8_t)id; buf[w++] = (uint8_t)(id >> 8);
        s.state = SLOT_FREE;
      }
      if (w > 1) return w;

      for (uint16_t i = 0; i < st.lim.maxSchemas; ++i) {
        Schema& c = st.schemas[i];
        if (!c.name || c.sent) continue;
        size_t nl = strlen(c.name), tl = strlen(c.type);
        if (1 + 1 + nl + 1 + tl + 2 + (size_t)c.len > cap) { c.sent = true; continue; }
        w = 0;
        buf[w++] = FRAME_SCHEMA;
        buf[w++] = (uint8_t)nl; memcpy(buf + w, c.name, nl); w += nl;
        buf[w++] = (uint8_t)tl; memcpy(buf + w, c.type, tl); w += tl;
        buf[w++] = (uint8_t)c.len; buf[w++] = (uint8_t)(c.len >> 8);
        memcpy(buf + w, c.data, c.len); w += c.len;
        c.sent = true;
        return w;
      }

      w = 1;
      buf[0] = FRAME_ANNOUNCE;
      for (uint16_t i = 0; i < n; ++i) {
        Slot& s = st.slots[i];
        if (s.state != SLOT_LIVE || s.announced) continue;
        size_t nl = strlen(s.name), tl = strlen(s.type), pl = strlen(s.props);
        size_t rec = 2 + 1 + 1 + 1 + nl + 1 + tl + 2 + pl;
        if (w + rec > cap || (w > 1 && w + rec > target)) break;
        TopicId id = makeId(i, s.gen);
        buf[w++] = (uint8_t)id; buf[w++] = (uint8_t)(id >> 8);
        buf[w++] = s.vt; buf[w++] = s.flags;
        buf[w++] = (uint8_t)nl; memcpy(buf + w, s.name, nl); w += nl;
        buf[w++] = (uint8_t)tl; memcpy(buf + w, s.type, tl); w += tl;
        buf[w++] = (uint8_t)pl; buf[w++] = (uint8_t)(pl >> 8);
        memcpy(buf + w, s.props, pl); w += pl;
        s.announced = true;
        if (s.hasValue) s.dirty = true;
        s.refusedSent = false;
      }
      if (w > 1) return w;

      if (st.ackCount) {
        w = 0;
        buf[w++] = FRAME_ACK;
        while (st.ackCount && w + 3 <= target) {
          const Ack& a = st.acks[st.ackHead];
          buf[w++] = (uint8_t)a.id; buf[w++] = (uint8_t)(a.id >> 8); buf[w++] = a.status;
          st.ackHead = (uint8_t)((st.ackHead + 1) % kAcks); --st.ackCount;
        }
        return w;
      }

      // Refusals, each after the value it leaves on the Driver Station.
      w = 1;
      buf[0] = FRAME_REFUSED;
      for (uint16_t i = 0; i < n && w + 7 <= target; ++i) {
        Slot& s = st.slots[i];
        if (s.state != SLOT_LIVE || !s.announced || !s.refused || s.refusedSent || s.dirty) continue;
        TopicId id = makeId(i, s.gen);
        buf[w++] = (uint8_t)id; buf[w++] = (uint8_t)(id >> 8);
        buf[w++] = s.refused;
        for (int b = 0; b < 4; ++b) buf[w++] = (uint8_t)(s.refusedLen >> (8 * b));
        s.refusedSent = true;
      }
      if (w > 1) return w;

      w = 0;
      buf[w++] = FRAME_VALUES;
      buf[w++] = (uint8_t)nowMs; buf[w++] = (uint8_t)(nowMs >> 8);
      buf[w++] = (uint8_t)(nowMs >> 16); buf[w++] = (uint8_t)(nowMs >> 24);
      const size_t head = w;
      // Resume where the previous batch stopped, so with more dirty bytes
      // than one push tick can send every topic still gets its turn.
      const uint16_t start = st.valueCursor;
      for (uint16_t k = 0; k < n; ++k) {
        uint16_t i = (uint16_t)((start + k) % n);
        Slot& s = st.slots[i];
        if (s.state != SLOT_LIVE || !s.announced || !s.dirty || !s.hasValue) continue;
        size_t rec = 4 + s.len;
        if (w + rec > cap || (w > head && w + rec > target)) { st.valueCursor = i; break; }
        TopicId id = makeId(i, s.gen);
        buf[w++] = (uint8_t)id; buf[w++] = (uint8_t)(id >> 8);
        buf[w++] = (uint8_t)s.len; buf[w++] = (uint8_t)(s.len >> 8);
        if (s.len) memcpy(buf + w, s.value, s.len);
        w += s.len;
        s.dirty = false;
        s.refusedSent = false;             // the refusal follows this value again
        st.valueCursor = (uint16_t)((i + 1) % n);
      }
      return w > head ? w : 0;
    }

    // Frees every table and value and forgets the limits (host tests only).
    inline void resetForTest() {
      Store& st = g_store;
      if (st.slots) {
        for (uint16_t i = 0; i < st.lim.maxTopics; ++i) {
          Slot& s = st.slots[i];
          free(s.name); free(s.type); free(s.props); free(s.value); free(s.write);
          free(s.cand); free(s.tuned);
        }
        for (uint16_t i = 0; i < st.lim.maxSchemas; ++i) {
          free(st.schemas[i].name); free(st.schemas[i].type); free(st.schemas[i].data);
        }
      }
      free(st.slots); free(st.schemas); free(st.fifo);
      st.slots = nullptr; st.schemas = nullptr; st.fifo = nullptr;
      st.lim = Limits{}; st.configured = false;
      st.fifoHead = st.fifoCount = 0; st.topics = 0;
      st.bytesUsed = st.writeBytes = st.dropped = 0;
      st.resync = false; st.ackHead = st.ackCount = 0; st.draining = 0;
      st.persistUsed = false;
      st.persistClearing = false;
      g_persist = nullptr;
    }

  } // namespace detail

  // Publishes a topic, or returns the existing id when the path is already
  // published with the same value type and type string. Returns kInvalid on
  // a type conflict, an over-long name/type/properties string, a full table
  // or an exhausted pool. Before the runtime starts it also returns kInvalid.
  inline TopicId publish(const char* path, ValueType vt, const char* typeString,
                         const char* propertiesJson, uint8_t flags) {
    using namespace detail;
    Store& st = g_store;
    if (!path || !*path || !ensureTables(st)) return kInvalid;
    if (!typeString) typeString = "";
    if (!propertiesJson) propertiesJson = "";
    size_t nl = strlen(path), tl = strlen(typeString), pl = strlen(propertiesJson);
    if (nl > 255 || tl > 255) return kInvalid;
    const uint32_t hash = hashOf(path);
    const uint32_t bytes = (uint32_t)(nl + tl + pl + 3);

    auto existing = [&](Slot* s) -> TopicId {
      if (s->vt != (uint8_t)vt || strcmp(s->type, typeString) != 0) return kInvalid;
      return makeId((uint16_t)(s - st.slots), s->gen);
    };
    {
      core::MuxGuard g(st.mux);
      if (pl > st.lim.maxValue) return kInvalid;
      if (Slot* s = findLive(st, path, hash)) return existing(s);
      if (!reserve(st, bytes)) return kInvalid;
    }
    char* name  = dupString(path, nl);
    char* type  = dupString(typeString, tl);
    char* props = dupString(propertiesJson, pl);
    TopicId id = kInvalid;
    bool keep = false;
    {
      core::MuxGuard g(st.mux);
      if (Slot* s = findLive(st, path, hash)) {
        id = existing(s);              // published concurrently
      } else if (name && type && props) {
        // Round-robin, so one topic republished in a loop does not cycle a
        // single slot's 8-bit generation back to a stale id quickly.
        const uint16_t start = st.freeCursor;
        for (uint16_t k = 0; k < st.lim.maxTopics; ++k) {
          uint16_t i = (uint16_t)((start + k) % st.lim.maxTopics);
          Slot& s = st.slots[i];
          if (s.state != SLOT_FREE) continue;
          st.freeCursor = (uint16_t)((i + 1) % st.lim.maxTopics);
          uint8_t gen = (uint8_t)(s.gen + 1);
          s = Slot{};
          s.state = SLOT_LIVE; s.gen = gen; s.vt = (uint8_t)vt; s.flags = flags;
          s.hash = hash; s.name = name; s.type = type; s.props = props;
          ++st.topics;
          id = makeId(i, gen);
          keep = true;
          break;
        }
      }
      if (!keep) st.bytesUsed -= bytes;
      else if (flags & PERSISTENT) st.persistUsed = true;
    }
    if (!keep) { free(name); free(type); free(props); }
    else if (flags & PERSISTENT) restore(st, id, path, vt, typeString);
    return id;
  }

  // Removes a topic. The Driver Station is told with an 'R' frame; the id
  // is not reused before that.
  inline void unpublish(TopicId id) {
    using namespace detail;
    Store& st = g_store;
    char *name = nullptr, *type = nullptr, *props = nullptr;
    uint8_t *value = nullptr, *write = nullptr, *cand = nullptr, *tuned = nullptr;
    {
      core::MuxGuard g(st.mux);
      Slot* s = liveSlot(st, id);
      if (!s) return;
      name = s->name; type = s->type; props = s->props; value = s->value; write = s->write;
      cand = s->cand; tuned = s->tuned;
      st.bytesUsed -= (uint32_t)(strlen(name) + strlen(type) + strlen(props) + 3) + s->cap;
      if (s->writePending) { st.writeBytes -= queuedBytes(*s); dropFromFifo(st, id & 0xFF); }
      uint8_t gen = s->gen;
      *s = Slot{};
      s->gen = gen;
      s->state = SLOT_REMOVING;
      --st.topics;
    }
    free(name); free(type); free(props); free(value); free(write); free(cand); free(tuned);
  }

  // Replaces the properties JSON and re-announces the topic.
  inline bool setProperties(TopicId id, const char* propertiesJson) {
    using namespace detail;
    Store& st = g_store;
    if (!propertiesJson) propertiesJson = "";
    size_t pl = strlen(propertiesJson);
    char* props = dupString(propertiesJson, pl);
    if (!props) return false;
    char* old = props;
    bool ok = false;
    {
      core::MuxGuard g(st.mux);
      Slot* s = liveSlot(st, id);
      if (s && pl <= st.lim.maxValue) {
        size_t oldLen = strlen(s->props);
        if (pl <= oldLen || reserve(st, (uint32_t)(pl - oldLen))) {
          if (pl < oldLen) st.bytesUsed -= (uint32_t)(oldLen - pl);
          old = s->props; s->props = props;
          s->announced = false;
          ok = true;
        }
      }
    }
    free(old);
    return ok;
  }

  // Replaces the flags and re-announces the topic.
  inline bool setFlags(TopicId id, uint8_t flags) {
    using namespace detail;
    uint8_t *cand = nullptr, *tuned = nullptr;
    {
      core::MuxGuard g(g_store.mux);
      Slot* s = liveSlot(g_store, id);
      if (!s) return false;
      s->flags = flags;
      s->announced = false;
      if (flags & PERSISTENT) {
        g_store.persistUsed = true;
      } else {                           // values not stored yet are dropped
        cand = s->cand; tuned = s->tuned;
        s->cand = s->tuned = nullptr; s->candLen = s->tunedLen = 0;
        s->oversize = false;
      }
    }
    free(cand); free(tuned);
    return true;
  }

  // Copies a new value. An identical value is dropped unless the topic has
  // KEEP_DUPLICATES. Buffers only grow, so steady-state sets do not allocate.
  // False when the value is longer than Limits::maxValue or does not fit in
  // the pool; the Driver Station then marks the topic's older value as not
  // current ('E' frame) until a later set() is stored.
  inline bool set(TopicId id, const void* data, size_t len) {
    using namespace detail;
    Store& st = g_store;
    if (len && !data) return false;
    uint32_t grow = 0, reserved = 0;
    {
      core::MuxGuard g(st.mux);
      Slot* s = liveSlot(st, id);
      if (!s) return false;
      if (len > st.lim.maxValue) { markRefused(*s, SetRefusal::TooLarge, len); return false; }
      if (len <= s->cap) {
        bool same = s->hasValue && s->len == len && (len == 0 || memcmp(s->value, data, len) == 0);
        clearRefused(*s);
        if (same && !(s->flags & KEEP_DUPLICATES)) return true;
        if (len) memcpy(s->value, data, len);
        s->len = (uint32_t)len; s->hasValue = true; s->dirty = true;
        return true;
      }
      grow = 16;
      while (grow < len) grow <<= 1;
      if (grow > st.lim.maxValue) grow = st.lim.maxValue;
      reserved = grow - s->cap;
      if (!reserve(st, reserved)) { ++st.dropped; markRefused(*s, SetRefusal::NoMemory, len); return false; }
    }
    uint8_t* buf = (uint8_t*)malloc(grow);
    uint8_t* old = buf;
    bool ok = false;
    {
      core::MuxGuard g(st.mux);
      st.bytesUsed -= reserved;        // re-accounted below from the live cap
      Slot* s = liveSlot(st, id);
      if (buf && s) {
        if (s->cap < grow) {
          st.bytesUsed += grow - s->cap;
          old = s->value;
          s->value = buf; s->cap = grow;
        }                              // else grown concurrently: drop ours
        memcpy(s->value, data, len);
        s->len = (uint32_t)len; s->hasValue = true; s->dirty = true;
        s->refused = 0;
        ok = true;
      } else if (s) {
        ++st.dropped;
        markRefused(*s, SetRefusal::NoMemory, len);
      }
    }
    free(old);
    return ok;
  }

  inline bool hasSchema(const char* name) {
    using namespace detail;
    Store& st = g_store;
    if (!name) return false;
    core::MuxGuard g(st.mux);
    if (!st.schemas) return false;
    for (uint16_t i = 0; i < st.lim.maxSchemas; ++i)
      if (st.schemas[i].name && strcmp(st.schemas[i].name, name) == 0) return true;
    return false;
  }

  // Registers a struct/proto schema (e.g. name "struct:Pose2d", type
  // "structschema"). Idempotent per name. Returns false when it cannot be
  // stored: longer than Limits::maxSchemaBytes, no free schema slot, or the
  // pool is full.
  inline bool addSchema(const char* name, const char* type, const void* data, size_t len) {
    using namespace detail;
    Store& st = g_store;
    if (!name || !type || (len && !data) || !ensureTables(st)) return false;
    size_t nl = strlen(name), tl = strlen(type);
    if (nl > 255 || tl > 255) return false;
    const uint32_t bytes = (uint32_t)(nl + tl + 2 + len);
    {
      core::MuxGuard g(st.mux);
      if (len > st.lim.maxSchemaBytes) return false;
      for (uint16_t i = 0; i < st.lim.maxSchemas; ++i)
        if (st.schemas[i].name && strcmp(st.schemas[i].name, name) == 0) return true;
      if (!reserve(st, bytes)) return false;
    }
    char* n = dupString(name, nl);
    char* t = dupString(type, tl);
    uint8_t* d = (uint8_t*)malloc(len ? len : 1);
    if (d && len) memcpy(d, data, len);
    bool stored = false, exists = false;
    {
      core::MuxGuard g(st.mux);
      for (uint16_t i = 0; i < st.lim.maxSchemas; ++i)
        if (st.schemas[i].name && strcmp(st.schemas[i].name, name) == 0) exists = true;
      if (!exists && n && t && d) {
        for (uint16_t i = 0; i < st.lim.maxSchemas; ++i) {
          if (st.schemas[i].name) continue;
          st.schemas[i] = Schema{ n, t, d, (uint16_t)len, false };
          stored = true;
          break;
        }
      }
      if (!stored) st.bytesUsed -= bytes;
    }
    if (!stored) { free(n); free(t); free(d); }
    return stored || exists;
  }

  // Calls fn for each queued Driver Station write, oldest first, and returns
  // how many it delivered. Write::data is valid only during fn. Each
  // delivered topic's current value is re-sent afterwards, so the Driver
  // Station sees the value the robot kept. Call it from one task; a
  // concurrent call returns 0.
  inline size_t drainWrites(void (*fn)(const Write&, void* ctx), void* ctx) {
    using namespace detail;
    Store& st = g_store;
    if (!fn || __atomic_exchange_n(&st.draining, 1u, __ATOMIC_ACQUIRE)) return 0;
    size_t delivered = 0;
    uint16_t budget;
    {
      core::MuxGuard g(st.mux);
      budget = st.fifoCount;
    }
    while (budget--) {
      Write w{};
      uint8_t *data = nullptr, *accepted = nullptr;
      {
        core::MuxGuard g(st.mux);
        if (!st.fifoCount) break;
        uint16_t idx = st.fifo[st.fifoHead];
        st.fifoHead = (uint16_t)((st.fifoHead + 1) % st.lim.maxTopics);
        --st.fifoCount;
        Slot& s = st.slots[idx];
        if (s.state != SLOT_LIVE || !s.writePending) continue;
        data = s.write;
        w = Write{ makeId(idx, s.gen), (ValueType)s.vt, s.writeLen, data };
        st.writeBytes -= queuedBytes(s);
        s.write = nullptr; s.writePending = false;
        s.restoreDelivered = s.restorePending;
        s.dsDelivered = !s.restorePending;
        s.restorePending = false;
        s.writeLen = 0;
        if (s.cand) {                    // a newer write is drained: it was not refused
          accepted = s.tuned;
          s.tuned = s.cand; s.tunedLen = s.candLen;
          s.cand = nullptr; s.candLen = 0;
        }
      }
      free(accepted);
      fn(w, ctx);
      ++delivered;
      {
        core::MuxGuard g(st.mux);
        if (Slot* s = liveSlot(st, w.id)) {
          if (s->hasValue) s->dirty = true;
          // Not refused during fn: the candidate to store.
          if ((s->flags & PERSISTENT) && s->dsDelivered) {
            if (w.len <= kMaxPersistValue) {
              s->cand = data; s->candLen = w.len; s->candFresh = true;
              data = nullptr;
            } else {
              s->oversize = true;          // cannot be stored: reported, not kept
            }
          }
        }
      }
      free(data);
    }
    __atomic_store_n(&st.draining, 0u, __ATOMIC_RELEASE);
    return delivered;
  }

  // Tells the Driver Station a drained write was not applied ('K' frame).
  // Call it before another write for the topic is drained. For a restored
  // PERSISTENT value no frame is sent, since the Driver Station did not
  // write it: the stored value is erased instead. A refused Driver Station
  // write to a PERSISTENT topic is not stored.
  inline void rejectWrite(TopicId id, uint8_t reason) {
    using namespace detail;
    uint8_t* drop = nullptr;
    {
      core::MuxGuard g(g_store.mux);
      Slot* s = liveSlot(g_store, id);
      if (s && s->restoreDelivered) {
        s->restoreDelivered = false;
        s->eraseStored = true;
        return;
      }
      if (s && s->dsDelivered) {
        s->dsDelivered = false;
        s->oversize = false;
        drop = s->cand; s->cand = nullptr; s->candLen = 0;
      }
      pushAck(g_store, id, (WriteStatus)reason);
    }
    free(drop);
  }

  // Erases every stored PERSISTENT value, and drops stored values still
  // waiting for drainWrites() and Driver Station values not stored yet.
  // Published topics keep their values; the next Driver Station write is
  // stored again. Writes flash, so call it while the robot is stopped.
  // False without storage or when the erase failed.
  inline bool clearPersisted() {
    using namespace detail;
    const PersistBackend* be = __atomic_load_n(&g_persist, __ATOMIC_ACQUIRE);
    if (!be) return false;
    Store& st = g_store;
    for (uint16_t i = 0;; ++i) {
      uint8_t *drop = nullptr, *cand = nullptr, *tuned = nullptr;
      {
        core::MuxGuard g(st.mux);
        if (!st.slots || i >= st.lim.maxTopics) break;
        if (i == 0) { ++st.persistEpoch; st.persistClearing = true; }
        Slot& s = st.slots[i];
        // restoreDelivered stays: refusing that restore must not send 'K'.
        s.eraseStored = s.dsDelivered = s.restoreFailed = s.oversize = false;
        cand = s.cand; tuned = s.tuned;
        s.cand = s.tuned = nullptr; s.candLen = s.tunedLen = 0;
        if (s.restorePending) {
          dropFromFifo(st, i);
          drop = s.write;
          s.write = nullptr; s.writeLen = 0; s.writePending = s.restorePending = false;
        }
      }
      free(drop); free(cand); free(tuned);
    }
    // Values accepted while the walk ran are stored only after the erase.
    bool ok = be->eraseAll();
    {
      core::MuxGuard g(st.mux);
      st.persistClearing = false;
    }
    return ok;
  }

  inline Stats stats() {
    const auto& st = detail::g_store;
    core::MuxGuard g(st.mux);
    return Stats{ st.topics, st.lim.maxTopics, st.bytesUsed, st.lim.poolBytes, st.dropped };
  }

  // The limits in effect (PROBOT_DASH_* after clamping); all zero before
  // runtime_setup() configures the store.
  inline Limits limits() {
    const auto& st = detail::g_store;
    core::MuxGuard g(st.mux);
    return st.lim;
  }

  namespace detail {

    inline bool persistUsed() {
      core::MuxGuard g(g_store.mux);
      return g_store.persistUsed;
    }

    enum class PersistFailure : uint8_t { Store, Erase, Read };

    // One pass of the persistence task: stores each accepted Driver Station
    // value of a PERSISTENT topic and erases refused stored ones. A flash
    // write stalls both cores, so nothing is written unless idle (the robot
    // is not running an OpMode); edits made while running are stored after
    // it stops. A candidate still open to refusal and a write still waiting
    // for drainWrites() are not stored. rec and cur hold recordCapacity()
    // bytes each. failed gets the path of a value that could not be stored
    // or erased (not retried until the next Driver Station write) or whose
    // stored value could not be read (reported once). Returns the flash
    // operations attempted (an unchanged record is not rewritten).
    inline size_t persistTick(uint32_t nowMs, bool idle, uint8_t* rec, uint8_t* cur, size_t cap,
                              void (*failed)(const char* path, PersistFailure why)) {
      Store& st = g_store;
      const PersistBackend* be = __atomic_load_n(&g_persist, __ATOMIC_ACQUIRE);
      if (!be) return 0;
      enum Op { None, Report, TooLarge, Store, Erase };
      size_t ops = 0;
      for (uint16_t i = 0;; ++i) {
        char key[kKeyLen];
        char path[256];
        size_t n = 0;
        Op op = None;
        uint8_t *drop = nullptr, *stored = nullptr;
        uint32_t epoch;
        {
          core::MuxGuard g(st.mux);
          if (!st.slots || i >= st.lim.maxTopics) break;
          Slot& s = st.slots[i];
          if (s.state == SLOT_LIVE && (s.flags & PERSISTENT)) {
            if (s.candFresh) { s.candFresh = false; s.candSince = nowMs; }
            if (s.cand && (uint32_t)(nowMs - s.candSince) >= kPersistDebounceMs) {
              drop = s.tuned;                  // not refused in time: accepted
              s.tuned = s.cand; s.tunedLen = s.candLen;
              s.cand = nullptr; s.candLen = 0;
            }
            if (s.restoreFailed && !s.failReported) {
              s.failReported = true;
              op = Report;
            } else if (s.oversize) {
              s.oversize = false;
              op = TooLarge;
            } else if (idle && !st.persistClearing && !s.cand && !s.writePending) {
              if (s.tuned) {
                n = encodeRecord(s.name, s.vt, s.type, s.tuned, s.tunedLen, rec, cap);
                stored = s.tuned;              // freed below; rec holds the copy
                s.tuned = nullptr; s.tunedLen = 0;
                s.eraseStored = false;         // the new value replaces the refused one
                op = Store;
              } else if (s.eraseStored) {
                s.eraseStored = false;
                op = Erase;
              }
            }
            if (op != None) {
              epoch = st.persistEpoch;
              persistKey(s.hash, key);
              memcpy(path, s.name, strlen(s.name) + 1);
            }
          }
        }
        free(drop); free(stored);
        if (op == None) continue;
        if (op == Report || op == TooLarge) {
          if (failed) failed(path, op == Report ? PersistFailure::Read : PersistFailure::Store);
          continue;
        }
        bool ok, wrote = false;
        if (op == Erase) {
          ok = be->erase(key);
          ++ops;
        } else if (n == 0) {
          ok = false;                          // does not fit the record
        } else if (be->load(key, cur, cap) == (long)n && memcmp(cur, rec, n) == 0) {
          ok = true;
        } else {
          ok = wrote = be->save(key, rec, n);
          ++ops;
          bool cleared;
          {
            core::MuxGuard g(st.mux);
            cleared = st.persistEpoch != epoch;
          }
          if (cleared && wrote) be->erase(key);   // clearPersisted() ran meanwhile
        }
        if (!ok && failed) failed(path, op == Erase ? PersistFailure::Erase : PersistFailure::Store);
      }
      return ops;
    }

  } // namespace detail

} // namespace probot::dashboard
