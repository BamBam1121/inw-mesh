// MultiRadio: the mesh::Radio the node talks to. One radio at a time:
//
//   home link up   everything goes through the Wi-Fi companion; the LoRa chip sleeps.
//   home link down the pager's own LoRa radio, exactly as before.
//
// The dispatcher already hands over finished on-air packets and takes raw bytes
// back, so this only decides which of the two carries them.
//
// A packet the companion doesn't confirm (refused, or no answer in RESEND_AFTER_MS,
// or the link went down with it) goes out again on our own radio, so a link that
// is dying quietly can't swallow it. The mesh drops the copy if both got out.
//
// One exception: a packet too big for a companion frame (over 174 bytes) can't go
// over the link, so the LoRa chip is woken just to send it, then sleeps again.
//
// MeshCore's own code still reaches radio_driver directly for radio settings and
// stats; those stay about the LoRa chip.

#pragma once
#include <Arduino.h>
#include <Dispatcher.h>
#include <helpers/radiolib/RadioLibWrappers.h>

class MultiRadio : public mesh::Radio {
public:
  bool viaLink() const { return _viaLink; }
  bool lastFromLink() const { return _viaLink; }   // one source at a time
  uint8_t txState() const { return _tx; }          // for the USB "link" report
  bool loraAsleep() const { return _loraAsleep; }

  struct Counters { uint32_t linkTx = 0, linkRefused = 0, tooBig = 0, linkRx = 0, resent = 0; };
  const Counters& counters() const { return _n; }

  // mesh::Radio
  void begin() override { lora().begin(); }
  int  recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override { return lora().packetScore(snr, packet_len); }
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  void loop() override;
  int  getNoiseFloor() const override { return lora().getNoiseFloor(); }
  void triggerNoiseFloorCalibrate(int t) override { if (!_viaLink) lora().triggerNoiseFloorCalibrate(t); }
  void setCADEnabled(bool enable) override { lora().setCADEnabled(enable); }
  void resetAGC() override { if (!_viaLink) lora().resetAGC(); }
  bool isInRecvMode() const override { return _tx == TX_NONE && (_viaLink || lora().isInRecvMode()); }
  // LBT is the companion's job on the link. A resend of ours on LoRa reads as a busy
  // channel, so the dispatcher waits for it instead of starting over the top.
  bool isReceiving() override { return _tx == TX_RESEND || (!_viaLink && lora().isReceiving()); }
  float getLastRSSI() const override { return _viaLink ? _rssi : lora().getLastRSSI(); }
  float getLastSNR() const override { return _viaLink ? _snr : lora().getLastSNR(); }

private:
  enum Tx : uint8_t { TX_NONE, TX_LORA, TX_LINK, TX_RESEND };

  // radio_driver, looked up on use: it's a reference set up in another file's
  // static initialisers, so it can't be bound safely in our constructor.
  static RadioLibWrapper& lora();

  bool     _viaLink = false;
  bool     _loraAsleep = false;
  uint32_t _linkUpSince = 0;
  Tx       _tx = TX_NONE;
  uint32_t _txSeq = 0, _txStart = 0, _txEst = 0;
  float    _snr = 0, _rssi = 0;
  Counters _n;

  // Link sends not yet confirmed by the companion.
  static constexpr uint8_t UNCONFIRMED_MAX = 8;
  struct Unconfirmed { uint32_t seq = 0, at = 0; uint8_t len = 0; bool resend = false; uint8_t buf[174]; };
  Unconfirmed _unc[UNCONFIRMED_MAX];

  void remember(uint32_t seq, const uint8_t* bytes, int len);
  void checkUnconfirmed();

  void sleepLora();
  void wakeLora();
  uint32_t linkAirtime(int len) const;
};

extern MultiRadio g_radio;
