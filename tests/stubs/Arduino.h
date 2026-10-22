#pragma once
#include <stdint.h>
#include <math.h>
#include <vector>
#include <cstddef>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

#ifdef __cplusplus
extern "C" {
#endif
void yield(void);
#ifdef __cplusplus
}
#endif

inline void yield(void) {}

extern unsigned long _test_millis_now;

inline unsigned long millis(){ return _test_millis_now; }
inline void delay(unsigned long ms){ _test_millis_now += ms; }
inline void delayMicroseconds(unsigned int us){ _test_millis_now += (us + 999u) / 1000u; }

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int){ return 0; }
inline void analogWrite(int, int) {}
inline void analogWriteResolution(int, int) {}
inline void analogWriteFrequency(int, int) {}

struct __SerialStub {
  std::vector<uint8_t> buffer;
  void begin(unsigned long) {}
  void println(const char*) {}
  void println(int) {}
  void print(const char*) {}
  void printf(const char*, ...) {}
  size_t write(uint8_t value){
    buffer.push_back(value);
    return 1;
  }
  size_t write(const uint8_t* data, size_t len){
    if (data && len){
      buffer.insert(buffer.end(), data, data + len);
    }
    return len;
  }
  void clear(){ buffer.clear(); }
} __attribute__((unused));

inline __SerialStub Serial;

inline void __serialStubClear(){ Serial.clear(); }
inline const std::vector<uint8_t>& __serialStubBuffer(){ return Serial.buffer; }

inline std::vector<uint8_t>& __wifiStubBuffer(){ static std::vector<uint8_t> buf; return buf; }
inline void __wifiStubClear(){ __wifiStubBuffer().clear(); }
inline bool __wifiStubSend(const uint8_t* data, size_t len){
  auto& buf = __wifiStubBuffer();
  buf.assign(data, data + len);
  return true;
}
inline bool __wifiStubSendFail(const uint8_t*, size_t){
  return false;
}

inline uint32_t micros(){ return millis() * 1000u; }
inline uint32_t esp_random(){ static uint32_t x = 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
