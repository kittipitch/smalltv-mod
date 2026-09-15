// OomTrap.cpp -- see OomTrap.h. ESP8266 only; the ESP32 heaps are not umm.
#if !defined(SMALLTV_ESP32C2) && !defined(SMALLTV_ESP32)
#include "OomTrap.h"

static_assert(sizeof(OomEvent) == 12, "OomEvent is copied word-for-word into RTC");

extern "C" {
size_t umm_free_heap_size_lw(void);   // O(1), no heap walk -- safe in any context

// The core's real allocators. Signatures must match heap.cpp exactly: the SDK
// passes file/line in a3/a4 (visible in the ratetable disassembly), so a
// one-argument shim would silently shift the ABI.
void* __real_pvPortZalloc(size_t size, const char* file, int line);
void* __real_pvPortMalloc(size_t size, const char* file, int line);
void* __real_pvPortCalloc(size_t count, size_t size, const char* file, int line);
void* __real_pvPortRealloc(void* ptr, size_t size, const char* file, int line);
}

OomEvent          g_oomRing[OOM_RING_LEN];
volatile uint32_t g_oomCount = 0;

// Runs on the failure path only, possibly from SDK/timer context with the flash
// cache unavailable: IRAM, no flash-resident callee, no heap walk, no RTC write.
// millis() is IRAM_ATTR in core 3.1.2; umm_free_heap_size_lw() lives in the
// IRAM-linked umm_malloc object. Both are checked with nm after every build.
static inline void IRAM_ATTR oomNote(uint32_t ra, size_t size) {
  size_t heap = umm_free_heap_size_lw();
  uint32_t ps = xt_rsil(15);
  OomEvent& e = g_oomRing[g_oomCount % OOM_RING_LEN];
  e.ra   = ra;
  e.ms   = millis();
  e.size = size > 0xFFFF ? 0xFFFF : (uint16_t)size;
  e.heap = heap > 0xFFFF ? 0xFFFF : (uint16_t)heap;
  g_oomCount = g_oomCount + 1;
  xt_wsr_ps(ps);
}

#ifdef WITH_CRASHTEST
void oomTestInject(uint32_t ra, size_t size) { oomNote(ra, size); }
#endif

extern "C" {
// Return address first, before any other call can disturb a0.
void* IRAM_ATTR __wrap_pvPortZalloc(size_t size, const char* file, int line) {
  uint32_t ra = (uint32_t)__builtin_return_address(0);
  void* p = __real_pvPortZalloc(size, file, line);
  if (!p && size) oomNote(ra, size);
  return p;
}
void* IRAM_ATTR __wrap_pvPortMalloc(size_t size, const char* file, int line) {
  uint32_t ra = (uint32_t)__builtin_return_address(0);
  void* p = __real_pvPortMalloc(size, file, line);
  if (!p && size) oomNote(ra, size);
  return p;
}
void* IRAM_ATTR __wrap_pvPortCalloc(size_t count, size_t size, const char* file, int line) {
  uint32_t ra = (uint32_t)__builtin_return_address(0);
  void* p = __real_pvPortCalloc(count, size, file, line);
  if (!p && count && size) oomNote(ra, count * size);
  return p;
}
void* IRAM_ATTR __wrap_pvPortRealloc(void* ptr, size_t size, const char* file, int line) {
  uint32_t ra = (uint32_t)__builtin_return_address(0);
  void* p = __real_pvPortRealloc(ptr, size, file, line);
  if (!p && size) oomNote(ra, size);
  return p;
}
}
#endif
