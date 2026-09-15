// OomTrap.h -- records allocations that FAIL, including the closed SDK's own.
//
// The 2026-09-11 f661 crash was the SDK's ieee80211_setup_ratetable calling
// pvPortZalloc(212), getting NULL, and memcpy'ing into it. Nothing recorded that
// the allocation failed: the core's own "last failed alloc" bookkeeping is
// compiled out for pvPort* unless DEBUG_ESP_OOM is set (heap.cpp:207-217), and
// that switch costs 840 B of IRAM. Instead the linker routes every pvPort* call
// through a small IRAM shim (OomTrap.cpp, -Wl,--wrap= in platformio.ini) that
// notes the caller, size and free heap whenever the real allocator returns NULL.
// The crash handler copies the ring into the RTC crash record.
#pragma once
#include <Arduino.h>

struct OomEvent {
  uint32_t ra;     // return address of the failing call (addr2line it)
  uint32_t ms;     // millis() at the failure
  uint16_t size;   // bytes requested (clamped)
  uint16_t heap;   // total free heap at that instant (clamped)
};
#define OOM_RING_LEN 4

#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
static inline uint32_t        oomCount() { return 0; }
static inline const OomEvent* oomRing()  { return nullptr; }
#else
extern OomEvent          g_oomRing[OOM_RING_LEN];   // slot = failure number % OOM_RING_LEN
extern volatile uint32_t g_oomCount;                // failures since boot
static inline uint32_t        oomCount() { return g_oomCount; }
static inline const OomEvent* oomRing()  { return g_oomRing; }
#ifdef WITH_CRASHTEST
// Crashtest env only: record a synthetic failure through the real path, so the
// crash-record OOM ring (ordering, indexing, formatting) can be exercised on
// hardware without waiting for a storm to exhaust the heap.
void oomTestInject(uint32_t ra, size_t size);
#endif
#endif
