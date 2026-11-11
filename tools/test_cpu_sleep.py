#!/usr/bin/env python3
"""Exercise actual manual sleep policy with fake IDF calls (not hardware QA)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'esp_err.h': r'''#pragma once
inline const char* esp_err_to_name(int code) { return code ? "TEST_ERROR" : "ESP_OK"; }
''',
    'Arduino.h': r'''#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t now=0, yields=0;
inline uint32_t millis() { return now; }
inline void delay(uint32_t) { ++yields; }
constexpr int HIGH=1, LOW=0;
inline int radioLevel=0, buttonLevel=1, wheelPress=1, wheelA=1, wheelB=1, imuLevel=0;
inline int digitalRead(int pin) { return pin==14 ? radioLevel : pin==0 ? buttonLevel : pin==7 ? wheelPress : pin==8 ? imuLevel : pin==40 ? wheelA : wheelB; }
struct SerialMock { template<class... T> void printf(const char*, T...) {} };
inline SerialMock Serial;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (++*(x))
#define portEXIT_CRITICAL(x) (--*(x))
''',
    'Preferences.h': r'''#pragma once
#include <cstddef>
inline bool stored=false, writable=true, shortWrite=false;
class Preferences { public:
bool begin(const char*, bool ro) { return ro || writable; }
bool getBool(const char*, bool fallback) { return stored ? true : fallback; }
size_t putBool(const char*, bool value) { if(shortWrite) return 0; stored=value; return 1; }
void end() {}
};
''',
    'esp_sleep.h': r'''#pragma once
#include <cassert>
#include <cstdint>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_SLEEP_WAKEUP_GPIO=7, ESP_SLEEP_WAKEUP_TIMER=4;
inline int calls=0, result=0, configResult=0, enabledSources=0;
inline int gpioResult=0, badPin=-1, pinResult=0;
inline bool radioOnEntry=false;
inline uint64_t timerUs=0;
inline int64_t clockUs=0;
inline int wakeCause=ESP_SLEEP_WAKEUP_TIMER;
inline bool injectedRadio=false, expectImu=false;
inline int esp_sleep_enable_timer_wakeup(uint64_t us) { timerUs=us; enabledSources|=1; return configResult; }
inline int esp_sleep_enable_gpio_wakeup() { enabledSources|=2; return gpioResult; }
int esp_light_sleep_start();
inline int esp_sleep_get_wakeup_cause() { return wakeCause; }
inline void esp_sleep_disable_wakeup_source(int s) { enabledSources &= s==ESP_SLEEP_WAKEUP_TIMER ? ~1 : ~2; }
''',
    'esp_timer.h': r'''#pragma once
#include <esp_sleep.h>
inline int64_t esp_timer_get_time() { return clockUs; }
''',
    'driver/gpio.h': r'''#pragma once
#include <esp_sleep.h>
#include <Arduino.h>
using gpio_num_t=int;
constexpr int GPIO_INTR_DISABLE=0, GPIO_INTR_POSEDGE=1, GPIO_INTR_HIGH_LEVEL=4, GPIO_INTR_LOW_LEVEL=5;
inline int intr[49]={};
inline bool wakes[49]={};
inline int gpio_wakeup_enable(int pin, int trigger) { wakes[pin]=true; intr[pin]=trigger; return pin==badPin ? pinResult : 0; }
inline void gpio_wakeup_disable(int pin) { wakes[pin]=false; }
inline void gpio_set_intr_type(int pin, int trigger) { intr[pin]=trigger; }
inline int esp_light_sleep_start() {
  ++calls;
  if (radioOnEntry) radioLevel=1;
  assert(wakes[14] && wakes[0] && wakes[8]==expectImu);
  assert(!wakes[7] && !wakes[40] && !wakes[41] && !wakes[6]);
  clockUs+=injectedRadio ? 7000 : timerUs;
  return result;
}

''',
    'test.cpp': r'''#include <cassert>
#include <cstring>
#include <initializer_list>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "power.h"
#include "cpu_sleep.h"
#include "sleep_deadline.h"
#include "battery_schedule.h"
namespace power {
  bool saving=true;
  uint32_t mhz=80;
  bool cpuSaving() { return saving; }
  void updateCpu(bool) {}
  uint32_t cpuClockMhz() { return mhz; }
}
int main() {
  using namespace power;
  SleepInputs idle{true,false,false,false,false,false,false,true,false,false,false,false,150000};
  assert(!lightSleep()); assert(!idleLightSleep(idle) && calls==0);
  writable=false; assert(!setLightSleep(true) && !lightSleep());
  writable=true; shortWrite=true; assert(!setLightSleep(true) && !lightSleep());
  shortWrite=false; assert(setLightSleep(true) && lightSleep());
  assert(!idleLightSleep(idle) && calls==0); // grace period
  now=120000;
  saving=false; assert(!idleLightSleep(idle)); saving=true;
  // Each unsafe input independently blocks sleep.
  for(int i=0;i<11;++i) {
    auto blocked=idle;
    switch(i) {
      case 0: blocked.screenOff=false; break;
      case 1: blocked.audio=true; break;
      case 2: blocked.usb=true; break;
      case 3: blocked.bluetooth=true; break;
      case 4: blocked.wifi=true; break;
      case 5: blocked.gps=true; break;
      case 6: blocked.meshBusy=true; break;
      case 7: blocked.radioReady=false; break;
      case 8: blocked.motion=true; break;
      case 9: blocked.background=true; break;
      case 10: blocked.nfc=true; break;
    }
    assert(!idleLightSleep(blocked) && calls==0);
  }
  radioLevel=1; assert(!idleLightSleep(idle)); radioLevel=0;
  buttonLevel=0; assert(!idleLightSleep(idle)); buttonLevel=1;
  wheelPress=0; wheelA=0; wheelB=0; // ignored pocket input must not block sleep
  mhz=240; assert(!idleLightSleep(idle)); mhz=80;
  assert(idleLightSleep(idle));
  assert(calls==1 && yields==1 && timerUs==30000000);
  assert(lightSleepCount()==1 && lightSleepMicros()==30000000);
  assert(lightSleepWakeCause()==ESP_SLEEP_WAKEUP_TIMER);
  assert(!wakes[14] && !wakes[0] && intr[14]==GPIO_INTR_POSEDGE && intr[0]==GPIO_INTR_DISABLE);
  assert(!wakes[7] && !wakes[40] && !wakes[41]);
  assert(lightSleepLastMs()==30000 && lightSleepMaxMs()==30000);
  assert(enabledSources==0);
  // Even a high IMU line cannot block sleep or add a motion wake source.
  idle.imuIrq=true; imuLevel=1;
  assert(idleLightSleep(idle) && calls==2 && !wakes[8]);
  idle.imuIrq=false; imuLevel=0;
  calls=1; // remaining checks count from this baseline
  // Playback temporarily blocks sleep, then allows it again without a reset.
  auto sound=idle; sound.audio=true;
  assert(!idleLightSleep(sound) && calls==1);
  assert(idleLightSleep(idle) && calls==2);
  result=99; assert(!idleLightSleep(idle));
  assert(calls==3 && lightSleepCount()==3 && enabledSources==0);
  assert(!idleLightSleep(idle) && calls==3); // no failure storm
  result=0; assert(setLightSleep(true));
  configResult=99; assert(!idleLightSleep(idle) && calls==3 && enabledSources==0);
  configResult=0; assert(setLightSleep(true));
  assert(idleLightSleep(idle) && calls==4);
  // An IRQ ends a long sleep early without changing the CPU clock.
  injectedRadio=true; wakeCause=ESP_SLEEP_WAKEUP_GPIO;
  idle.deadline=125000;
  assert(idleLightSleep(idle) && calls==5 && timerUs==5000000);
  assert(lightSleepLastMs()==7 && lightSleepMaxMs()==30000 && mhz==80);
  assert(lightSleepWakeCause()==ESP_SLEEP_WAKEUP_GPIO);
  injectedRadio=false;
  // Deadline already due, or due during preparation: do not oversleep.
  idle.deadline=now; assert(!idleLightSleep(idle) && calls==5);
  idle.deadline=now+1; assert(!idleLightSleep(idle) && calls==5);
  idle.deadline=now+250; assert(idleLightSleep(idle) && calls==6 && timerUs==250000);
  idle.deadline=now+60000; assert(idleLightSleep(idle) && timerUs==30000000);
  assert(setLightSleep(false) && !idleLightSleep(idle) && calls==7);
  // Battery sampling/checkpoint share a 30-second deadline in the saving mode.
  assert(batteryIntervalMs(true,true)==30000);
  assert(batteryIntervalMs(false,true)==5000);
  assert(batteryIntervalMs(true,false)==5000);
  assert(batteryIntervalMs(false,false)==5000);
  SleepDeadline batteryJob(120000);
  batteryJob.after(120000,batteryIntervalMs(true,true));
  assert(batteryJob.ms()==30000);
  // A low-voltage minute is a minute with either 5 s or 30 s samples.
  for (uint32_t interval : {5000u,30000u}) {
    LowBatteryWindow low;
    assert(!low.update(true,0));
    for (uint32_t t=interval;t<60000;t+=interval) assert(!low.update(true,t));
    assert(low.update(true,60000));
    assert(!low.update(false,65000));
    assert(!low.update(true,90000));
    assert(!low.update(true,120000));
    assert(low.update(true,150000));
  }
  LowBatteryWindow lowWrap;
  assert(!lowWrap.update(true,0xfffffff0));
  assert(!lowWrap.update(true,(uint32_t)(0xfffffff0u+30000u)));
  assert(lowWrap.update(true,(uint32_t)(0xfffffff0u+60000u)));
  // Multiple real scheduler deadlines, due/past deadlines and millis wrap.
  SleepDeadline jobs(1000); jobs.after(900,5000); jobs.at(2000); assert(jobs.ms()==1000);
  jobs.at(999); assert(jobs.ms()==0);
  SleepDeadline wrap(0xfffffff0); wrap.after(0xfffffff0,100); assert(wrap.ms()==100);
  wrap.at(0xffffffe0); assert(wrap.ms()==0);
  // Diagnostic accounting measures elapsed time, not loop counts. Separate
  // real work, the first blocking guard, actual sleep, and screen-on time.
  now=200000; idle.deadline=now+30000; assert(setLightSleep(true));
  resetSleepDiagnostics(true);
  clockUs+=1000;
  sleepDiagnosticPhase(SleepReason::Draw,true);
  clockUs+=2000;
  auto overlay=idle; overlay.background=true;
  sleepDiagnosticContext(SleepReason::Overlay);
  assert(!idleLightSleep(overlay));
  clockUs+=30000;
  sleepDiagnosticPhase(SleepReason::Input,true);
  assert(sleepDiagnosticMicros(SleepReason::Input)==1000);
  assert(sleepDiagnosticMicros(SleepReason::Draw)==2000);
  assert(sleepDiagnosticMicros(SleepReason::Overlay)==30000);
  // USB takes precedence over an overlay and is not charged to that overlay.
  overlay.usb=true; assert(!idleLightSleep(overlay)); clockUs+=7000;
  sleepDiagnosticPhase(SleepReason::Input,false); clockUs+=1000000;
  assert(sleepDiagnosticMicros(SleepReason::Usb)==7000);
  assert(sleepDiagnosticMicros(SleepReason::Input)==1000);
  sleepDiagnosticPhase(SleepReason::Scheduling,true);
  injectedRadio=false; assert(idleLightSleep(idle));
  assert(sleepDiagnosticMicros(SleepReason::Sleeping)==30000000);
  assert(lightSleepCount()==1 && lightSleepMicros()==30000000);
  printSleepDiagnostics();
  result=99; assert(!idleLightSleep(idle));
  assert(sleepDiagnosticMicros(SleepReason::Sleeping)==30000000);
  assert(sleepDiagnosticMicros(SleepReason::Error)==30000000);
  resetSleepDiagnostics(true);
  assert(lightSleepCount()==0 && lightSleepMicros()==0);
  assert(sleepDiagnosticMicros(SleepReason::Sleeping)==0);
  const int before=calls; assert(!idleLightSleep(idle) && calls==before);
  assert(setLightSleep(true)); result=0;
  assert(idleLightSleep(idle)); // Manual retry still works and retains evidence.
  SleepFailure last{};
  assert(lastSleepFailure(last) && last.code==99);
  assert(sleepFailureCount()==3 && sleepRecoveryCount()==2);
  // Each preparation stage is retained and restores the temporary wake setup.
  for (int stage=0; stage<4; ++stage) {
    assert(setLightSleep(true));
    configResult=stage==0 ? 71 : 0;
    badPin=stage==1 ? 14 : stage==2 ? 0 : -1; pinResult=72;
    gpioResult=stage==3 ? 73 : 0;
    const int entered=calls;
    assert(!idleLightSleep(idle) && calls==entered);
    assert(enabledSources==0 && !wakes[14] && !wakes[0]);
    assert(intr[14]==GPIO_INTR_POSEDGE && intr[0]==GPIO_INTR_DISABLE);
    assert(lastSleepFailure(last));
    const char* stages[]={"timer wake","radio wake","button wake","GPIO wake"};
    assert(!std::strcmp(last.stage,stages[stage]));
  }
  configResult=gpioResult=pinResult=0; badPin=-1;
  assert(setLightSleep(true) && idleLightSleep(idle));
  // A radio edge arriving after the initial gate can reject entry: retain it.
  radioOnEntry=true; result=99;
  const auto failuresBefore=sleepFailureCount();
  assert(!idleLightSleep(idle));
  assert(lastSleepFailure(last) && last.radioBefore==0 && last.radioAfter==1);
  assert(last.budgetMs==30000 && last.clockMhz==80 && last.uptimeMs==now);
  assert(!std::strcmp(last.stage,"enter sleep"));
  radioOnEntry=false; radioLevel=0; result=0;
  const int entered=calls;
  assert(sleepRetryMs()==1000);
  now+=999; idle.deadline=now+30000;
  assert(!idleLightSleep(idle) && calls==entered && sleepRetryMs()==1);
  now+=1; idle.deadline=now+30000;
  // Even once cooldown ends, every ordinary service guard remains in force.
  auto busy=idle; busy.usb=true;
  assert(!idleLightSleep(busy) && calls==entered);
  radioLevel=1; assert(!idleLightSleep(idle) && calls==entered); radioLevel=0;
  assert(idleLightSleep(idle) && calls==entered+1 && sleepRetryMs()==0);
  assert(sleepFailureCount()==failuresBefore+1);
  assert(lastSleepFailure(last) && last.code==99); // success does not erase evidence
  // Persistent errors back off without a tight retry storm or permanent latch.
  result=99;
  for (uint32_t wait : {1000u,2000u,4000u,8000u,16000u,30000u,30000u,30000u,30000u}) {
    assert(!idleLightSleep(idle));
    assert(sleepRetryMs()==wait);
    const int n=calls;
    for (int i=0;i<100;++i) assert(!idleLightSleep(idle) && calls==n);
    now+=wait; idle.deadline=now+30000;
  }
  const auto retainedCount=sleepFailureCount();
  resetSleepDiagnostics(true);
  assert(sleepFailureCount()==retainedCount && lastSleepFailure(last));
  printSleepDiagnostics(); // also exercise ring traversal after more than 8 failures
  result=0; assert(idleLightSleep(idle));
  // Cooldown arithmetic survives the millis rollover without opening new wakes.
  now=0xffffff00u; idle.deadline=now+30000; result=99;
  assert(!idleLightSleep(idle) && sleepRetryMs()==1000);
  now+=999; idle.deadline=now+30000;
  assert(!idleLightSleep(idle) && sleepRetryMs()==1);
  now+=1; idle.deadline=now+30000;
  assert(sleepRetryMs()==0); // existing boot-grace gate may still block after wrap
  assert(setLightSleep(false) && setLightSleep(true));
  assert(sleepFailureCount()==retainedCount+1);

}
''',
}
with tempfile.TemporaryDirectory(prefix='cpu-sleep-test-') as directory:
    temp = Path(directory)
    for name, content in FILES.items():
        path = temp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    binary = temp / 'check'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    f'-I{temp}', f'-I{ROOT / "src"}', str(ROOT / 'src/cpu_sleep.cpp'),
                    str(temp / 'test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('Light sleep gates, wake cleanup, stats and error fallback: passed')
