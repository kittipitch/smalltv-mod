#!/usr/bin/env python3
"""Host check: the real Mascot.cpp always plays the one kept animation, whatever the burn rate.

Compiles src/features/usage/Mascot.cpp with tiny Arduino stubs and drives mascotSample() through every
mood (flat, slow, fast, very fast usage growth). Usage: scripts/test_mascot.py [FRAMES_HEADER]
(default: the committed src/features/usage/mascot_frames.h).
"""
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
frames = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "src/features/usage/mascot_frames.h"
with tempfile.TemporaryDirectory(prefix="mascot-") as tmp:
    t = Path(tmp)
    (t / "pgmspace.h").write_text("#pragma once\n#define PROGMEM\n#define pgm_read_byte(p) (*(const unsigned char*)(p))\n")
    (t / "Arduino.h").write_text("#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include \"pgmspace.h\"\n"
                                 "extern uint32_t g_now;\ninline uint32_t millis() { return g_now; }\n")
    (t / "mascot_frames.h").write_text(frames.read_text())
    (t / "Mascot.h").write_text((root / "src/features/usage/Mascot.h").read_text())
    src = (root / "src/features/usage/Mascot.cpp").read_text()
    (t / "Mascot.cpp").write_text(src)
    (t / "main.cpp").write_text(r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "Arduino.h"
#include "Mascot.h"
#include "mascot_frames.h"
uint32_t g_now = 0;
int main() {
  assert(MASCOT_ANIM_COUNT == 1);
  mascotInit();
  const float rates[] = {0.0f, 0.15f, 0.25f, 1.0f};          // idle, normal, active, heavy (%/min)
  for (float r : rates) {
    mascotReset();
    float pct = 0;
    for (int m = 0; m < 60; m++) {                            // an hour of samples, one a minute
      g_now = (uint32_t)m * 60000u; pct += r; mascotSample(pct);
      for (int k = 0; k < 600; k++) { g_now += 100; mascotTick(); }
      assert(std::strcmp(mascotName(), "idle breathe") == 0);
    }
  }
  std::puts("test_mascot: OK");
}
''')
    subprocess.run(["c++", "-std=c++14", "-I", str(t), str(t / "Mascot.cpp"), str(t / "main.cpp"),
                    "-o", str(t / "test")], check=True)
    subprocess.run([str(t / "test")], check=True)
