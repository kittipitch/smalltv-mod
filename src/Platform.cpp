// Platform.cpp — the few things that need a real definition (not header-inline)
// but are still core-specific. Keeps the per-chip SNTP callback wiring out of the
// feature code, same as the inline shims in Platform.h.
#include "Platform.h"

#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
#include <esp_sntp.h>

static void (*s_syncCb)() = nullptr;
static void sntpSyncNotify(struct timeval*) { if (s_syncCb) s_syncCb(); }

void platformOnTimeSync(void (*cb)()) {
  s_syncCb = cb;
  sntp_set_time_sync_notification_cb(sntpSyncNotify);
}

#else  // ESP8266
#include <coredecls.h>

// ---- crash log in RTC user memory ------------------------------------------
// This panel has no UART broken out, so the core's postmortem stack dump goes
// nowhere and every crash we have ever seen is reduced to whatever fits on the
// crash screen: epc + fault address. That was not enough to identify the caller
// for the 2026-09-10 f661 crash (epc 0x4000df64 = ROM memcpy+0x1c, addr 0
// -- 490 possible call sites once ets_memcpy's trampoline is counted), so this
// keeps the exception cause AND the top of the crashed stack, where the return
// addresses live, in RTC memory. RTC survives an exception reset (which is the
// case we care about) but NOT a power cycle -- so read it before power-cycling.
//
// Written from the crash handler, so: no heap, no String, no yield, no Serial,
// and no heap WALK (getMaxFreeBlockSize() traverses block headers and can fault
// on the very heap corruption being recorded).
#include "OomTrap.h"
#include "Net.h"
extern "C" size_t umm_free_heap_size_lw(void);

// RTC user memory map -- 512 B; slot = 4-byte block index into the user area.
//   slot  0  bytes   0..  3  GFX1 display-init guard          (main.cpp)
//   slot  8  bytes  32..287  CRL2 crash record, 256 B         (below)
//   slot 72  bytes 288..499  HSN1 health ring, 212 B          (below)
//            bytes 500..511  spare
// eboot writes its 128-byte OTA command over user bytes 0..127 on every OTA:
// esp8266_peri.h RTC_USER_MEM and eboot's RTC_MEM are both 0x60001200, and user
// block 0 sits at raw block 64 (RTC_SYS 0x60001100 + 256). Anything down there
// must be consumed before an OTA can happen, or be rejected by magic/checksum
// after one. The crash record qualifies -- it is consumed on the very boot after
// the crash, before any OTA can run. State meant to outlive an OTA goes at 128+.
#define CRASHLOG_SLOT   8
#define CRASHLOG_MAGIC  0x43524C32   // "CRL2" (CRL1 lacked heap/OOM/link context)
#define CRASHLOG_WORDS  40   // 40 words = 160 B; SP is the CALLER frame on a leaf fault, so 24 was thin
struct CrashLog {
  uint32_t magic, reason, exccause, epc1, excvaddr, sp;       // 24
  uint32_t stackEnd;      // SYS (0x3fffffb0) vs cont -- recorded, not inferred
  uint32_t uptimeS;
  uint32_t heapLw;        // total free heap at the fault, O(1) read
  uint32_t oomCount;      // allocations that returned NULL since boot (OomTrap)
  uint32_t discCount;     // Wi-Fi link drops since boot (netLoop)
  OomEvent oom[OOM_RING_LEN];                                  // 48
  uint32_t stack[CRASHLOG_WORDS];                              // 160
  uint32_t csum;          // FNV-1a over everything between magic and csum
};
static_assert(sizeof(OomEvent) * OOM_RING_LEN == 48, "CRL2 layout assumes a 48 B OOM ring");
static_assert(sizeof(CrashLog) == 256, "CRL2 layout changed -- bump the magic and the slot map");
static_assert(sizeof(CrashLog) % 4 == 0, "RTC user memory is written in 4-byte blocks");

#define HEALTH_SLOT     72
#define HEALTH_MAGIC    0x48534E31   // "HSN1"
#define HEALTH_LEN      17
#define HEALTH_TICK_MS  30000UL
struct HealthHdr { uint32_t magic, seq; };                    // seq = entries written, ever
struct HealthEnt {
  uint32_t upS;
  uint16_t heap, maxblk;   // KB would lose the resolution a 212 B failure needs
  int8_t   rssi;
  uint8_t  wifiSt;         // wl_status_t
  uint16_t disc;           // netDisconnectCount(), clamped
};
static_assert(sizeof(HealthHdr) == 8 && sizeof(HealthEnt) == 12, "health ring is written word-for-word");
static_assert(CRASHLOG_SLOT * 4 + sizeof(CrashLog) <= HEALTH_SLOT * 4, "crash record overruns the health ring");
static_assert(HEALTH_SLOT * 4 + sizeof(HealthHdr) + HEALTH_LEN * sizeof(HealthEnt) <= 500,
              "health ring overruns RTC user memory (bytes 500..511 are reserved)");

// One static buffer, not a handler-stack local: the handler runs ON the crashed
// stack, and a 256 B frame on a stack that has just overflowed is a second fault
// inside the postmortem -- a WDT loop with no record. The boot-time reader reuses
// it; nothing else touches it.
static CrashLog s_crash;

static uint32_t crashCsum(const CrashLog& c) {
  const uint32_t* w = (const uint32_t*)&c;
  uint32_t h = 0x811C9DC5UL;
  for (size_t i = 1; i < sizeof(CrashLog) / 4 - 1; i++) { h ^= w[i]; h *= 0x01000193UL; }
  return h;
}

// The core calls this weak hook from its postmortem handler, AFTER the dump and
// BEFORE the restart. ESP.rtcUserMemoryWrite() is a thin wrapper over
// system_rtc_mem_write() with the +64-block user-area offset already applied;
// use the SAME family for read and write so the offsets cannot drift apart.
extern "C" void custom_crash_callback(struct rst_info* ri, uint32_t stack, uint32_t stack_end) {
  CrashLog& c = s_crash;
  c.magic     = 0;              // written last, see below
  c.reason    = ri->reason;
  c.exccause  = ri->exccause;
  c.epc1      = ri->epc1;
  c.excvaddr  = ri->excvaddr;
  c.sp        = stack;
  c.stackEnd  = stack_end;
  c.uptimeS   = millis() / 1000;
  c.heapLw    = umm_free_heap_size_lw();
  c.oomCount  = oomCount();
  c.discCount = netDisconnectCount();
  const OomEvent* ring = oomRing();
  for (uint32_t i = 0; i < OOM_RING_LEN; i++) c.oom[i] = ring[i];
  // Refuse a wild SP. A stack overflow or a corrupted SP is exactly the class of
  // crash this log exists to catch, and dereferencing one HERE would fault inside
  // the postmortem handler -- turning a clean exception reset (crash screen + this
  // record) into a WDT reboot loop with no log at all.
  //
  // The upper bound is 0x40000000 (the end of the DRAM window), NOT dram0_0_seg's
  // 0x3FFFC000. That linker segment describes only where the SKETCH links
  // .data/.bss/heap; the SDK's SYS stack lives ABOVE it, and
  // core_esp8266_postmortem.cpp:246 passes stack_end = 0x3fffffb0 for any crash
  // that is not on the cont stack. Bounding at 0x3FFFC000 would have failed spOk
  // for every SYS-context crash -- including a fault in the SDK WiFi stack during
  // reassociation, which is the crash this whole feature was built for -- and
  // stored a valid-looking record with 40 zero words. Caught in the pre-flash
  // audit, before it shipped.
  const uint32_t kDramLo = 0x3FFE8000UL, kDramHi = 0x40000000UL;
  bool spOk = (stack & 3) == 0 && stack >= kDramLo && stack < kDramHi &&
              stack_end > stack && stack_end <= kDramHi;
  for (uint32_t i = 0; i < CRASHLOG_WORDS; i++) {
    uint32_t a = stack + 4 * i;
    c.stack[i] = (spOk && a + 4 <= stack_end) ? *(volatile uint32_t*)a : 0;
  }
  c.csum = crashCsum(c);
  // Body first with magic still 0, then the magic word on its own. A brownout
  // mid-write then leaves no valid record rather than a hybrid: a repeat crash at
  // the SAME pc passes the epc/excvaddr freshness check, so a torn record mixing
  // this crash's header with the previous crash's stack words would be read as
  // fresh and name the wrong callers with full confidence.
  ESP.rtcUserMemoryWrite(CRASHLOG_SLOT, (uint32_t*)&c, sizeof(c));
  uint32_t magic = CRASHLOG_MAGIC;
  ESP.rtcUserMemoryWrite(CRASHLOG_SLOT, &magic, sizeof(magic));
}

// Appends the health ring, oldest entry first, as "upS:heap/maxblk/rssi/st/disc".
// Called only while consuming a crash record at boot -- i.e. before the first
// platformHealthTick() of this boot can overwrite the run-up we want.
static void healthAppend(String& out) {
  HealthHdr h;
  if (!ESP.rtcUserMemoryRead(HEALTH_SLOT, (uint32_t*)&h, sizeof(h))) return;
  if (h.magic != HEALTH_MAGIC || h.seq == 0) return;
  uint32_t n = h.seq < HEALTH_LEN ? h.seq : HEALTH_LEN;
  out += " | hs";
  char b[40];
  for (uint32_t k = 0; k < n; k++) {
    uint32_t idx = (h.seq - n + k) % HEALTH_LEN;
    HealthEnt e;
    if (!ESP.rtcUserMemoryRead(HEALTH_SLOT + 2 + idx * 3, (uint32_t*)&e, sizeof(e))) return;
    snprintf(b, sizeof(b), " %u:%u/%u/%d/%u/%u", (unsigned)e.upS, (unsigned)e.heap,
             (unsigned)e.maxblk, (int)e.rssi, (unsigned)e.wifiSt, (unsigned)e.disc);
    out += b;
  }
}

bool platformCrashLogRead(String& out) {
  CrashLog& c = s_crash;
  if (!ESP.rtcUserMemoryRead(CRASHLOG_SLOT, (uint32_t*)&c, sizeof(c))) return false;
  if (c.magic != CRASHLOG_MAGIC) return false;
  // A record whose epc1/excvaddr do not match this boot's rst_info is probably
  // from an EARLIER crash (a fault inside the handler, or a brownout mid-write,
  // leaves the old one in place while the ROM still reports a crash). Label it
  // rather than discard it: the core does not populate epc1 identically for
  // every reset flavour, and a trace marked possibly-stale beats no trace. That
  // is deliberate -- do not "fix" it into a discard.
  struct rst_info* ri = ESP.getResetInfoPtr();
  bool fresh  = ri && c.epc1 == ri->epc1 && c.excvaddr == ri->excvaddr;
  bool intact = c.csum == crashCsum(c);
  char b[72];   // "exc N epc 0x... addr 0x... sp 0x..." needs 60; -Wformat-truncation caught 48
  snprintf(b, sizeof(b), "exc %u epc 0x%08x addr 0x%08x sp 0x%08x",
           (unsigned)c.exccause, (unsigned)c.epc1, (unsigned)c.excvaddr, (unsigned)c.sp);
  out = fresh ? "" : "stale? ";
  if (!intact) out += "torn? ";
  out += b;
  // Keep only words that look like code addresses -- IRAM .text 0x4010xxxx,
  // flash .irom0.text 0x402xxxxx, boot ROM 0x4000xxxx. Everything else on the
  // stack is data and only makes the field harder to read. Feed what survives
  // to addr2line against the matching firmware.elf.
  for (uint32_t i = 0; i < CRASHLOG_WORDS; i++) {
    uint32_t w = c.stack[i];
    if ((w & 0xFFF00000) == 0x40100000 ||
        (w & 0xFFF00000) == 0x40200000 ||
        (w & 0xFFFF0000) == 0x40000000) {
      snprintf(b, sizeof(b), " %08x", (unsigned)w);
      out += b;
    }
  }
  snprintf(b, sizeof(b), " | end 0x%08x up %us heap %u oom %u disc %u",
           (unsigned)c.stackEnd, (unsigned)c.uptimeS, (unsigned)c.heapLw,
           (unsigned)c.oomCount, (unsigned)c.discCount);
  out += b;
  // Newest failure first. ra is the SDK/lwIP call site: addr2line it. On the
  // f661 signature, ra inside ieee80211_setup_ratetable with size 212 is the
  // one-read proof that the allocation failed; oom 0 with this exc 29 falsifies it.
  // k < shown <= oomCount, so oomCount - 1 - k never underflows.
  uint32_t shown = c.oomCount < OOM_RING_LEN ? c.oomCount : OOM_RING_LEN;
  for (uint32_t k = 0; k < shown; k++) {
    const OomEvent& e = c.oom[(c.oomCount - 1 - k) % OOM_RING_LEN];
    snprintf(b, sizeof(b), " oom %08x(%u)@%u,%us", (unsigned)e.ra, (unsigned)e.size,
             (unsigned)e.heap, (unsigned)(e.ms / 1000));
    out += b;
  }
  healthAppend(out);
  return true;
}

void platformCrashLogClear() {
  uint32_t zero = 0;   // clearing the magic is enough to invalidate the record
  ESP.rtcUserMemoryWrite(CRASHLOG_SLOT, &zero, sizeof(zero));
}

// ---- health ring ------------------------------------------------------------
// Written from loop() (cont context), never from the crash handler, so there is
// no writer race and the heap walk below is safe. RTC is SRAM: rewrites are free.
// Periodic rather than crash-time on purpose: a hardware WDT reset runs no
// postmortem at all, and this ring is then the only trace of what led up to it.
static uint32_t s_healthSeq    = 0;
static uint32_t s_healthNextMs = 0;
static bool     s_healthInit   = false;
static HealthEnt s_healthLast;                 // last entry actually written
static uint32_t s_healthLastWrMs = 0;
// Sample every 30 s, but only WRITE when something moved or the heartbeat is due.
// Each write is two system_rtc_mem_write() calls into the closed SDK; on a
// healthy unit the samples are flat for hours, and 2,880 identical writes a day
// on every unit buy nothing. A flat stretch still shows up as a gap in upS.
#define HEALTH_HEARTBEAT_MS  300000UL
#define HEALTH_HEAP_DELTA    512

void platformHealthTick() {
  uint32_t now = millis();
  if (s_healthInit && (int32_t)(now - s_healthNextMs) < 0) return;
  if (!s_healthInit) {
    // Continue an existing ring across warm reboots; start over after a power
    // cycle (RTC comes up as noise, which the magic rejects).
    HealthHdr h;
    s_healthSeq = (ESP.rtcUserMemoryRead(HEALTH_SLOT, (uint32_t*)&h, sizeof(h)) &&
                   h.magic == HEALTH_MAGIC) ? h.seq : 0;
    s_healthInit = true;
  }
  s_healthNextMs = now + HEALTH_TICK_MS;

  uint32_t heap = ESP.getFreeHeap(), blk = platformMaxFreeBlock(), disc = netDisconnectCount();
  HealthEnt e;
  e.upS    = now / 1000;
  e.heap   = heap > 0xFFFF ? 0xFFFF : (uint16_t)heap;
  e.maxblk = blk  > 0xFFFF ? 0xFFFF : (uint16_t)blk;
  e.rssi   = (int8_t)netRSSI();
  e.wifiSt = (uint8_t)WiFi.status();
  e.disc   = disc > 0xFFFF ? 0xFFFF : (uint16_t)disc;

  auto moved = [](uint16_t a, uint16_t b) { return (a > b ? a - b : b - a) >= HEALTH_HEAP_DELTA; };
  bool changed = s_healthSeq == 0 ||
                 moved(e.heap, s_healthLast.heap) || moved(e.maxblk, s_healthLast.maxblk) ||
                 e.wifiSt != s_healthLast.wifiSt || e.disc != s_healthLast.disc ||
                 (e.rssi > s_healthLast.rssi ? e.rssi - s_healthLast.rssi
                                             : s_healthLast.rssi - e.rssi) >= 6;
  if (!changed && (uint32_t)(now - s_healthLastWrMs) < HEALTH_HEARTBEAT_MS) return;
  s_healthLast = e;
  s_healthLastWrMs = now;
  ESP.rtcUserMemoryWrite(HEALTH_SLOT + 2 + (s_healthSeq % HEALTH_LEN) * 3, (uint32_t*)&e, sizeof(e));
  s_healthSeq++;
  HealthHdr hdr = { HEALTH_MAGIC, s_healthSeq };
  ESP.rtcUserMemoryWrite(HEALTH_SLOT, (uint32_t*)&hdr, sizeof(hdr));
}

static void (*s_syncCb)() = nullptr;
// settimeofday_cb fires whenever the clock is set; from_sntp distinguishes an
// SNTP update (what we care about) from a manual settimeofday (which we never do).
static void sntpSyncNotify(bool from_sntp) { if (from_sntp && s_syncCb) s_syncCb(); }

void platformOnTimeSync(void (*cb)()) {
  s_syncCb = cb;
  settimeofday_cb(sntpSyncNotify);
}
#endif
