#!/usr/bin/env python3
"""Compile real motion.cpp against a simulated BHI260AP; no hardware claims."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
'Arduino.h': r'''#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <cstring>
using std::min; using std::max;
template<class T> T constrain(T v, T lo, T hi) { return std::max(lo,std::min(hi,v)); }
inline uint32_t fakeNow=1000;
inline int imuLevel=0;
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow+=ms; }
constexpr int INPUT=0, HIGH=1, LOW=0;
inline void pinMode(int,int) {}
inline int digitalRead(int) { return imuLevel; }
struct SerialMock { template<class... T> void printf(const char*,T...) {} void println(const char*) {} };
inline SerialMock Serial;
''',
'Wire.h': r'''#pragma once
#include <cstdint>
class TwoWire { public: void beginTransmission(uint8_t) {} int endTransmission() { return 0; }
uint32_t getClock() { return 400000; } void setClock(uint32_t) {} };
inline TwoWire Wire;
''',
'Preferences.h': r'''#pragma once
#include <cstdint>
class Preferences { public: bool begin(const char*,bool) { return true; }
bool getBool(const char*,bool value) { return value; }
uint8_t getUChar(const char*,uint8_t value) { return value; }
void putBool(const char*,bool) {} void putUChar(const char*,uint8_t) {} void end() {} };
''',
'logstore.h': r'''#pragma once
constexpr int LOG_INFO=0,LOG_WARN=1,LOG_ERROR=2;
class LogStore { public: template<class... T> void add(int,const char*,T...) {} };
''',
'bosch/BoschSensorID.hpp': r'''#pragma once
namespace BoschSensorID { constexpr uint8_t ACCEL_CORRECTED=4,ACCEL_PASSTHROUGH=1,TILT_DETECTOR=48,SIGNIFICANT_MOTION=55; }
''',
'BoschFirmware.h': r'''#pragma once
inline const uint8_t bosch_firmware_image[]={0};
inline const size_t bosch_firmware_size=1;
inline const bool bosch_firmware_type=false;
''',
'SensorBHI260AP.hpp': r'''#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <cassert>
constexpr int BHY2_OK=0;
struct bhy2_dev {};
struct bhy2_virt_sensor_conf { float sample_rate; };
struct bhy2_data_xyz { int16_t x,y,z; };
inline bhy2_data_xyz pose{0,0,4096};
inline void bhy2_parse_xyz(const uint8_t* p,bhy2_data_xyz* v) { std::memcpy(v,p,sizeof(*v)); }
inline bool tiltSupported=true,sigSupported=true,rejectEvents=false,rejectStop=false,rejectResume=false,stuckIrq=false;
inline bool deliverSamples=true;
inline bool restartArtifacts=false;
inline bool staleBurst=false;
inline bhy2_data_xyz stalePose{0,0,4096};
inline unsigned starts=0, rawUpdates=0;
inline uint32_t rawStarted=0;
inline float rates[256]={};
using Callback=void(*)(uint8_t,const uint8_t*,uint32_t,uint64_t*,void*);
inline Callback callbacks[256]={};
inline int pendingEvent=0;
inline uint8_t interruptMask=8;
inline bool bhy2_is_sensor_available(uint8_t id,const bhy2_dev*) {
return id==48?tiltSupported:id==55?sigSupported:true;
}
inline int bhy2_get_virt_sensor_cfg(uint8_t id,bhy2_virt_sensor_conf* c,bhy2_dev*) {
c->sample_rate=rejectStop && id==4 && rates[id]==0 ? 25 : rates[id]; return 0;
}
struct InterruptConfig {
  bool wakeUpFIFOEnabled=true,nonWakeUpFIFOEnabled=true,statusFIFOEnabled=true,debuggingEnabled=false,faultEnabled=true;
  enum class Polarity { ACTIVE_HIGH,ACTIVE_LOW }; enum class TriggerMode { LEVEL,EDGE };
  Polarity polarity=Polarity::ACTIVE_HIGH; TriggerMode triggerMode=TriggerMode::LEVEL;
  uint8_t getRegisterValue() const { return (!wakeUpFIFOEnabled)|(!nonWakeUpFIFOEnabled<<1)|(!statusFIFOEnabled<<2)|(!debuggingEnabled<<3)|(!faultEnabled<<4); }
};
enum class SensorRemap { BOTTOM_LAYER_TOP_LEFT_CORNER };
class SensorBHI260AP {
  bhy2_dev dev; uint32_t lastRaw=0;
public:
  void setPins(int) {} void setFirmware(const uint8_t*,size_t,bool) {} void setBootFromFlash(bool) {}
  bool begin(TwoWire&,uint8_t) { return true; }
  const char* getError() { return "simulated"; }
  void setRemapAxes(SensorRemap) {} bhy2_dev* getDev() { return &dev; }
  bool onResultEvent(uint8_t id,Callback cb) {
    if (!bhy2_is_sensor_available(id,&dev) || callbacks[id]) return false;
    callbacks[id]=cb; return true;
  }
  void removeResultEvent(uint8_t id,Callback) { callbacks[id]=nullptr; }
  bool configure(uint8_t id,float rate,uint32_t) {
    if (rejectResume && (id==4||id==1) && rate>0) return false;
    if (rejectEvents && (id==48||id==55) && rate>0) return false;
    rates[id]=rate;
    if (id==4||id==1) {
      lastRaw=millis();
      if (rate>0) { ++starts; rawUpdates=0; rawStarted=millis(); }
    }
    return true;
  }
  uint8_t getInterruptRegisterValue() { return interruptMask; }
  bool setInterruptRegisterValue(uint8_t v) { interruptMask=v; return true; }
  bool configureInterrupt(const InterruptConfig& c) { interruptMask=c.getRegisterValue(); return true; }
  void update() {
    if (pendingEvent && rates[pendingEvent]>0 && callbacks[pendingEvent]) {
      uint8_t id=pendingEvent; pendingEvent=0;
      if (id==55) rates[id]=0; // Bosch one-shot
      callbacks[id](id,nullptr,0,nullptr,nullptr);
    }
    imuLevel=stuckIrq ? 1 : 0;
    // The stopped/resumed FIFO can still deliver a burst of old orientation.
    if (staleBurst && rates[4]>0 && callbacks[4]) {
      staleBurst=false;
      for(int i=0;i<3;++i)
        callbacks[4](4,reinterpret_cast<const uint8_t*>(&stalePose),sizeof(stalePose),nullptr,nullptr);
    }
    if (deliverSamples && rates[4]>0 && callbacks[4] && millis()-lastRaw>=40) {
      // A restart may need >180 ms and produce invalid initial readings.
      if (restartArtifacts && starts>=4 && millis()-rawStarted<240) return;
      lastRaw=millis();
      bhy2_data_xyz value=pose;
      if (restartArtifacts && starts>=4 && rawUpdates++<2) value={0,0,0};
      callbacks[4](4,reinterpret_cast<const uint8_t*>(&value),sizeof(value),nullptr,nullptr);
    }
  }
};
''',
'motion_test.cpp': r'''#include <cassert>
#include <cstring>
#include <string>
#include <SensorBHI260AP.hpp>
#include <logstore.h>
#include "motion.h"
LogStore logs;
namespace power {
  bool light=true,cpu=true;
  bool saver() { return false; }
  bool lightSleep() { return light; }
  bool cpuSaving() { return cpu; }
}
void frames(int count,bool off=true) {
  for (int i=0;i<count;++i) { fakeNow+=40; motion::tick(off); }
}
int main(int argc,char** argv) {
  std::string scenario=argc>1?argv[1]:"normal";
  if (scenario=="stop") rejectStop=true;
  motion::begin();
  assert(rates[4]==25);
  motion::tick(true);
  if (scenario=="stop") {
    assert(motion::needsFastPolling() && rates[4]==25);
    assert(std::strstr(motion::sleepState(),"polling"));
    assert(!motion::irqWatch());
    return 0;
  }
  assert(!motion::needsFastPolling() && rates[4]==0 && !motion::irqWatch());
  if(scenario=="turn-after-sleep") {
    // Observe face up while lit, press middle to sleep, then turn face down.
    motion::setQuietFaceDown(true);
    pose={0,0,4096}; frames(60,false);
    motion::tick(true);
    assert(!motion::needsFastPolling());
    pose={0,0,-4096}; stalePose={0,0,4096}; staleBurst=true;
    motion::refreshForAlert(); assert(motion::faceDown());
    // Also reject a stale face-down FIFO burst after turning face up.
    pose={0,0,4096}; stalePose={0,0,-4096}; staleBurst=true;
    motion::refreshForAlert(); assert(!motion::faceDown());
    // Message races the middle-button sleep, before the first dark tick.
    pose={0,0,4096}; frames(30,false);
    pose={0,0,-4096}; stalePose={0,0,4096}; staleBurst=true;
    motion::refreshForAlert(true);
    assert(motion::faceDown() && rates[4]==0 && !motion::takeRaise());
    return 0;
  }
  // Walking for ten minutes, including a permanently asserted sensor IRQ,
  // must neither resume sampling nor raise the screen.
  for(int i=0;i<15000;++i) {
    pose={int16_t(i%2?1800:-1800),0,int16_t(i%2?3600:-3600)};
    pendingEvent=i%2?48:55; imuLevel=1;
    frames(1);
    assert(!motion::needsFastPolling() && !motion::takeRaise());
    assert(rates[4]==0 && rates[48]==0 && rates[55]==0);
    assert(callbacks[48]==nullptr && callbacks[55]==nullptr);
  }
  motion::setQuietFaceDown(true); motion::tick(true);
  // Repeated messages with no screen wake: first warm read then slow cold
  // restarts with invalid initial samples. Never cache the previous orientation.
  restartArtifacts=true; pose={0,0,-4096};
  for(int i=0;i<12;++i) {
    fakeNow+=30000; motion::tick(true);
    motion::refreshForAlert();
    assert(motion::faceDown());
    assert(!motion::needsFastPolling() && !motion::irqWatch() && rates[4]==0);
  }
  pose={0,0,4096}; motion::refreshForAlert(); assert(!motion::faceDown());
  restartArtifacts=false;
  // Fresh pose, including one never observed before sleeping, controls quiet.
  for(int i=0;i<6;++i) {
    pose={0,0,int16_t(i%2?4096:-4096)};
    const uint32_t start=millis();
    motion::refreshForAlert();
    assert(millis()-start<=500 && motion::faceDown()==(i%2==0));
    assert(!motion::needsFastPolling() && rates[4]==0 && !motion::takeRaise());
  }
  // Large acceleration while face down is not treated as lying still.
  pose={0,0,-6000}; motion::refreshForAlert(); assert(!motion::faceDown());
  deliverSamples=false;
  const uint32_t timeoutStart=millis(); motion::refreshForAlert();
  assert(millis()-timeoutStart==500 && !motion::faceDown() && rates[4]==0);
  deliverSamples=true;
  // A failed stop after an alert must prevent unsafe long sleep.
  rejectStop=true; motion::refreshForAlert();
  assert(motion::needsFastPolling() && rates[4]==25);
  rejectStop=false; motion::setQuietFaceDown(true); motion::tick(true);
  assert(!motion::needsFastPolling() && rates[4]==0);
  // Failed read must not retain stale silence or a stale screen raise.
  pose={0,0,-4096}; motion::refreshForAlert(); assert(motion::faceDown());
  rejectResume=true; motion::refreshForAlert();
  assert(!motion::faceDown() && !motion::takeRaise() && !motion::irqWatch());
  // The next message must recover without toggling a setting or rebooting.
  rejectResume=false; motion::refreshForAlert();
  assert(motion::faceDown() && rates[4]==0 && !motion::needsFastPolling());
  // Screen wake and disabling either CPU option restore normal sampling.
  motion::tick(false); assert(rates[4]==25 && motion::needsFastPolling());
  frames(1); assert(rates[4]==0);
  power::light=false; motion::tick(true); assert(rates[4]==25);
  power::light=true; motion::tick(true); assert(rates[4]==0);
  power::cpu=false; motion::tick(true); assert(rates[4]==25);
  power::cpu=true; motion::tick(true); assert(rates[4]==0);
  // Preserve the existing man-down exception, but suppress raise while saving.
  motion::setManDownMin(10); frames(150);
  assert(motion::needsFastPolling() && rates[4]==25 && !motion::takeRaise());
  motion::setManDownMin(0); motion::tick(true); assert(rates[4]==0);
  motion::setRaiseToWake(false); motion::setStayOnHeld(false); motion::setQuietFaceDown(false);
  assert(!motion::needsFastPolling() && rates[4]==0);

}
''',
}
with tempfile.TemporaryDirectory(prefix='motion-sleep-test-') as directory:
    temp = Path(directory)
    for name, content in FILES.items():
        path = temp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    # Quoted includes search next to motion.cpp first; use an exact copy of the
    # production translation unit so hardware-only local headers can be faked.
    (temp / 'motion.cpp').write_text((ROOT / 'src/motion.cpp').read_text())
    binary = temp / 'check'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',
                    f'-I{temp}',f'-I{ROOT / "src"}',str(temp/'motion.cpp'),
                    str(temp/'motion_test.cpp'),'-o',str(binary)],check=True)
    for scenario in ('normal','stop','turn-after-sleep'):
        subprocess.run([str(binary),scenario],check=True)
        print(f'Motion sleep {scenario}: passed')
