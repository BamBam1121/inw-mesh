#include "multiradio.h"
#include <math.h>
#include <target.h>
#include "homelink.h"
#include "logstore.h"

extern LogStore logs;

MultiRadio g_radio;

RadioLibWrapper& MultiRadio::lora() { return radio_driver; }

// The link has to have been up this long before we switch to it, so a connection
// that comes and goes doesn't bounce the LoRa chip in and out of sleep.
static constexpr uint32_t LINK_SETTLE_MS = 3000;
// A companion answers a raw send in well under a second, even with our Wi-Fi in its
// deep sleep. Nothing by then: it's going out on LoRa as well.
static constexpr uint32_t RESEND_AFTER_MS = 5000;
static_assert(homelink::MAX_TX_RAW <= 174, "Unconfirmed::buf holds one link send");

// The chip itself, behind the wrapper's protected _radio: reached without changing MeshCore.
struct LoraChip : RadioLibWrapper {
  static PhysicalLayer& of(RadioLibWrapper& w) { return *(w.*(&LoraChip::_radio)); }
};

void MultiRadio::sleepLora() {
  if (_loraAsleep) return;
  // Warm sleep: the chip keeps its settings. Not the wrapper's powerOff(): on the
  // SX1262 that's a cold sleep, which forgets frequency, SF, sync word, TCXO and the
  // interrupt mask. Woken from that, LoRa sent on the wrong settings, never said a
  // send was done, and sat out BUSY timeouts - so falling back to LoRa after the
  // link dropped looked fine and was heard by nobody.
  LoraChip::of(lora()).sleep();
  _loraAsleep = true;
}

void MultiRadio::wakeLora() {
  if (!_loraAsleep) return;
  _loraAsleep = false;
  // Standby first: it wakes the chip the way it wants (a bare NOP). Anything else
  // waits on BUSY before talking, which an asleep chip holds high, and each call
  // sat out RadioLib's 1 s timeout. Then resetAGC() for a fresh receive.
  LoraChip::of(lora()).standby();
  lora().resetAGC();
}

// ---- which radio ----------------------------------------------------------------------------------
void MultiRadio::loop() {
  // Only switch between packets, never in the middle of one.
  if (_tx == TX_NONE) {
    if (homelink::up()) {
      if (!_linkUpSince) _linkUpSince = millis() | 1;
      if (!_viaLink && millis() - _linkUpSince >= LINK_SETTLE_MS) {
        _viaLink = true;
        sleepLora();
        logs.add(LOG_INFO, "radio: using home link");
      }
    } else {
      _linkUpSince = 0;
      if (_viaLink) {
        _viaLink = false;
        wakeLora();
        logs.add(LOG_INFO, "radio: back on lora");
      }
    }
  }
  checkUnconfirmed();
  if (_tx == TX_RESEND && lora().isSendComplete()) {
    lora().onSendFinished();
    _tx = TX_NONE;
    if (_viaLink) sleepLora();
  }
  if (!_loraAsleep) lora().loop();
}

void MultiRadio::remember(uint32_t seq, const uint8_t* bytes, int len) {
  Unconfirmed* slot = &_unc[0];
  for (auto& u : _unc) {
    if (!u.seq) { slot = &u; break; }
    if ((int32_t)(u.at - slot->at) < 0) slot = &u;   // full: the oldest makes room
  }
  slot->seq = seq;
  slot->at = millis();
  slot->len = (uint8_t)len;
  slot->resend = false;
  memcpy(slot->buf, bytes, len);
}

void MultiRadio::checkUnconfirmed() {
  for (auto& u : _unc) {
    if (!u.seq || u.resend) continue;
    const int8_t r = homelink::result(u.seq);
    if (r > 0) { u.seq = 0; continue; }
    if (r < 0 || millis() - u.at >= RESEND_AFTER_MS) u.resend = true;
  }
  if (_tx != TX_NONE) return;                         // only between packets
  for (auto& u : _unc) {
    if (!u.seq || !u.resend) continue;
    wakeLora();
    const bool ok = lora().startSendRaw(u.buf, u.len);
    u.seq = 0;
    if (!ok) { if (_viaLink) sleepLora(); continue; }
    _tx = TX_RESEND;
    _n.resent++;
    logs.add(LOG_WARN, "radio: home link didn't confirm a packet, sent it on lora");
    return;                                           // one at a time
  }
}

// ---- receive --------------------------------------------------------------------------------------
int MultiRadio::recvRaw(uint8_t* bytes, int sz) {
  // Our own resend on LoRa: its done flag is the radio's interrupt too, and reading
  // a packet now would take it for one and leave the send waiting forever.
  if (_tx == TX_RESEND) return 0;
  if (!_viaLink) return lora().recvRaw(bytes, sz);
  int len = 0;
  if (!homelink::recv(bytes, len, _snr, _rssi)) return 0;
  _n.linkRx++;
  return min(len, sz);
}

// ---- transmit -------------------------------------------------------------------------------------
// LoRa time on air (Semtech AN1200.13) for the companion's settings. Preamble as
// MeshCore sets it per SF.
uint32_t MultiRadio::linkAirtime(int len) const {
  const homelink::Remote& r = homelink::remote();
  if (!r.sf || r.bw <= 0) return 0;
  const float tsym = (float)(1UL << r.sf) / (r.bw * 1000.0f) * 1000.0f;   // ms
  const int de = tsym > 16.0f ? 1 : 0;
  const int cr = r.cr >= 5 && r.cr <= 8 ? r.cr : 5;
  const float num = 8.0f * len - 4.0f * r.sf + 28 + 16;                   // CRC on, explicit header
  const float den = 4.0f * (r.sf - 2 * de);
  const float payloadSym = 8 + fmaxf(ceilf(num / den) * cr, 0);
  const float pre = RadioLibWrapper::preambleLengthForSF(r.sf) + 4.25f;
  return (uint32_t)ceilf((pre + payloadSym) * tsym);
}

uint32_t MultiRadio::getEstAirtimeFor(int len_bytes) {
  if (_viaLink) {
    const uint32_t t = linkAirtime(len_bytes);
    if (t) return t;
  }
  return lora().getEstAirtimeFor(len_bytes);
}

bool MultiRadio::startSendRaw(const uint8_t* bytes, int len) {
  if (_viaLink) {
    if (len <= homelink::MAX_TX_RAW) {
      uint32_t seq;
      if (homelink::send(bytes, len, seq)) {
        remember(seq, bytes, len);
        _tx = TX_LINK;
        _txSeq = seq;
        _txStart = millis();
        _txEst = getEstAirtimeFor(len);
        _n.linkTx++;
        return true;
      }
      // The link went down under us and we haven't switched yet: our own radio.
      wakeLora();
    } else {
      _n.tooBig++;
      wakeLora();                   // too big for the link: this one goes out on our own radio
    }
  }
  if (!lora().startSendRaw(bytes, len)) return false;
  _tx = TX_LORA;
  return true;
}

bool MultiRadio::isSendComplete() {
  if (_tx == TX_LORA) return lora().isSendComplete();
  if (_tx != TX_LINK) return true;

  // The companion answers as soon as it has queued the packet, long before it is
  // on air. Hold the dispatcher for the packet's airtime anyway: that keeps its
  // duty-cycle budget honest and paces us to what the companion can send. The
  // dispatcher gives up at 1.5x the estimate, so finish by 1.25x whether or not
  // the answer is back (it is counted when it comes).
  const int8_t r = homelink::result(_txSeq);
  if (r < 0) { _n.linkRefused++; return true; }
  const uint32_t el = millis() - _txStart;
  if (el < _txEst) return false;
  return r > 0 || el >= _txEst + _txEst / 4;
}

void MultiRadio::onSendFinished() {
  if (_tx == TX_LORA) {
    lora().onSendFinished();
    if (_viaLink) sleepLora();      // that was an oversized one: back to sleep
  }
  _tx = TX_NONE;
}
