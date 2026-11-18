#!/usr/bin/env python3
"""Host smoke tests for CPU clock policy; ESP32 HAL/NVS are replaced by fakes.

Run: python tools/test_cpu_power.py (requires g++). These tests do not establish
radio, peripheral or power compatibility; use docs/cpu-power-saving.md on hardware.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "Arduino.h": """#pragma once
#include <cstdint>
#include <cstddef>
struct SerialMock { template<class... T> void printf(const char*, T...) {} };
inline SerialMock Serial;
inline uint32_t fakeMillis=0;
inline uint32_t millis() { return fakeMillis; }
constexpr uint8_t HIGH=1, LOW=0, OUTPUT=1;
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
""",
    "Preferences.h": """#pragma once
#include <cstddef>
inline bool stored=false, writable=true;
class Preferences { public:
bool begin(const char*, bool ro) { return ro || writable; }
bool getBool(const char*, bool) { return stored; }
size_t putBool(const char*, bool v) { stored=v; return 1; }
void end() {}
};
""",
    "esp32-hal-cpu.h": """#pragma once
#include <cstdint>
inline uint32_t clockMhz=240;
inline int changes=0;
inline bool reject=false;
inline uint32_t getCpuFrequencyMhz() { return clockMhz; }
inline bool setCpuFrequencyMhz(uint32_t v) { ++changes; if(reject) return false; clockMhz=v; return true; }
""",
    "test.cpp": """#include <cassert>
#include <cstdlib>
#include <Preferences.h>
#include <esp32-hal-cpu.h>
#include "power.h"
#include "backlight.h"
int main(int argc, char** argv) {
clockMhz=argc>1 ? std::atoi(argv[1]) : 240;
const auto original=clockMhz;
assert(!power::cpuSaving());
power::updateCpu(true); assert(clockMhz==original && changes==0);
assert(power::setCpuSaving(true) && stored);
power::updateCpu(true); assert(clockMhz==80);
power::updateCpu(true); assert(changes==(original>80 ? 1 : 0));
power::updateCpu(false); assert(clockMhz==original);
if(original>80) {
reject=true; const int before=changes;
power::updateCpu(true); power::updateCpu(true);
assert(changes==before+1 && clockMhz==original);
reject=false;
assert(power::setCpuSaving(true));
power::updateCpu(true); assert(clockMhz==80);
}
writable=false;
assert(!power::setCpuSaving(false) && power::cpuSaving());
writable=true;
assert(power::setCpuSaving(false) && !stored && clockMhz==original);
power::updateCpu(true); assert(clockMhz==original);
// Test real screen-state transitions, including timers and message-style wake.
Backlight light; light.begin(42);
IdleDimmer dimmer; dimmer.begin(&light, 12, 3, 100, 200);
dimmer.onScreenState(power::updateCpu);
assert(power::setCpuSaving(true));
dimmer.sleepInstant(); assert(clockMhz==80);
// Repeated background passes do not change frequency while logically asleep.
for (int i=0; i<20; ++i) { power::updateCpu(dimmer.asleep()); assert(clockMhz==80); }
dimmer.wake(); assert(clockMhz==original && !dimmer.asleep());
dimmer.sleepNow(); assert(clockMhz==80);
dimmer.wakeInstant(); assert(clockMhz==original);
fakeMillis=101; dimmer.tick(); assert(dimmer.dimmed() && clockMhz==original);
fakeMillis=201; dimmer.tick(); assert(dimmer.asleep() && clockMhz==80);
assert(power::setCpuSaving(false)); assert(clockMhz==original);
dimmer.wakeInstant(); dimmer.sleepInstant(); assert(clockMhz==original);
}
""",
}

with tempfile.TemporaryDirectory(prefix="cpu-power-test-") as directory:
    temp = Path(directory)
    for name, content in FILES.items():
        (temp / name).write_text(content)
    binary = temp / "check"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{temp}", f"-I{ROOT / 'src'}", str(ROOT / "src/cpu_power.cpp"),
        str(temp / "test.cpp"), "-o", str(binary),
    ], check=True)
    for mhz in (240, 160, 80):
        subprocess.run([str(binary), str(mhz)], check=True)
        print(f"CPU policy at {mhz} MHz: passed")
