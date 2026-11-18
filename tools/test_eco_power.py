#!/usr/bin/env python3
"""Exercise the real ECO coordinator with failing preference setters."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    for name in ('eco_power.cpp', 'eco_power.h'):
        (p / name).write_text((ROOT / 'src' / name).read_text())
    (p / 'power.h').write_text('''#pragma once
namespace power { bool cpuSaving(); bool setCpuSaving(bool); }
''')
    (p / 'cpu_sleep.h').write_text('''#pragma once
namespace power { bool lightSleep(); bool setLightSleep(bool); }
''')
    (p / 'nfcapp.h').write_text('''#pragma once
namespace app { bool nfcPowerSaving(); bool setNfcPowerSaving(bool); }
''')
    (p / 'test.cpp').write_text(r'''
#include <cassert>
#include "eco_power.h"
bool cpu=false, sleep=false, nfc=false;
int failure=0, writes=0;
bool put(bool& target, bool value, int key) {
  ++writes;
  if (failure==key) { failure=0; return false; }
  target=value; return true;
}
namespace power {
bool cpuSaving() { return cpu; }
bool lightSleep() { return sleep; }
bool setCpuSaving(bool v) { return put(cpu,v,1); }
bool setLightSleep(bool v) { return put(sleep,v,2); }
}
namespace app {
bool nfcPowerSaving() { return nfc; }
bool setNfcPowerSaving(bool v) { return put(nfc,v,3); }
}
int main() {
  // All previous CPU/sleep/NFC combinations migrate to one coherent policy.
  for (int bits=0; bits<8; ++bits) {
    cpu=bits&1; sleep=bits&2; nfc=bits&4;
    const bool selected=cpu&&sleep;
    assert(power::initEco());
    assert(cpu==selected && sleep==selected && power::eco()==selected);
#if INW_NFC
    assert(nfc==selected);
#endif
    writes=0; assert(power::initEco()); assert(writes==0);
    assert(power::setEco(true) && power::eco());
#if INW_NFC
    assert(nfc);
#endif
    assert(power::setEco(false) && !power::eco() && !cpu && !sleep);
#if INW_NFC
    assert(!nfc);
#endif
  }
  // A single failed write must restore the previous selection in both directions.
  for (bool old : {false,true}) {
    for (int key=1; key<=
#if INW_NFC
      3
#else
      2
#endif
      ; ++key) {
      cpu=sleep=nfc=old; failure=key;
      assert(!power::setEco(!old));
      assert(cpu==old && sleep==old && power::eco()==old);
#if INW_NFC
      assert(nfc==old);
#endif
    }
  }
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>'))
    for nfc in (0, 1):
        binary = p / f'test-{nfc}'
        subprocess.run(['g++', '-std=c++17', f'-DINW_NFC={nfc}', '-I', str(p),
                        str(p / 'eco_power.cpp'), str(p / 'test.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f'ECO migration, toggles, failed writes and idle boots (NFC={nfc}): passed')
