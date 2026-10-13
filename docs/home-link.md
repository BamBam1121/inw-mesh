# Home link

When the pager is on a saved Wi-Fi network that has a companion set for it, it
sends and receives raw MeshCore packets through that companion over TCP instead of
its own LoRa radio. When it isn't, it uses its own radio as before. Never both. The usual case is an openHop Repeater at home:
the pager stops keying up its own radio and uses the repeater's companion listener.

The pager keeps its own identity, keys, contacts, channels and history. The
companion is only a radio on a wire: its own identity is never used for anything.

## Protocol

Plain MeshCore companion protocol over TCP (`'<' len16 payload` to the companion,
`'>' len16 payload` back).

| step | frame | expected |
|---|---|---|
| connect | `CMD_APP_START` (1), name `squatch-link` | any answer: `RESP_CODE_SELF_INFO` gives the companion's name and radio settings; a short one or an error is fine too |
| send | `CMD_SEND_RAW_PACKET` (65), priority 1, raw packet | `RESP_CODE_OK` / `RESP_CODE_ERR` |
| receive | (pushed) `PUSH_CODE_LOG_RX_DATA` 0x88: snr×4, rssi, raw packet | – |
| keepalive | `CMD_GET_DEVICE_TIME` every 20 s | `RESP_CODE_CURR_TIME`; 50 s of silence = dead |

Receiving needs no setup on either side: a MeshCore companion calls `logRxRaw()`
for every packet its radio hears and pushes it as 0x88 to whatever client is
connected, and openHop registers a raw-RX subscriber at start-up that does the
same for every client on every companion listener. openHop also never echoes a
client's own injected packets back to it, and sends unsolicited
`RESP_CODE_CURR_TIME` heartbeats, which the link ignores unless it asked the time.

There is no firmware or version check and no test packet: any companion that
answers app start in the companion protocol is used, MeshCore or not. The link only
gives up on one if it answers three real sends in a row with "unknown command"
(`ERR_CODE_UNSUPPORTED_CMD`); it tries again 10 minutes later, or straight away if
the network or host changes. Those three packets are lost; MeshCore's own retries
cover them. A companion that doesn't report its radio settings works too: airtime
is then estimated from the pager's own radio settings.

Reconnect backoff: 2, 5, 10, 30, 60 s.

## Size limit

A companion frame is at most 176 bytes, so raw packets over **174 bytes** can't be
sent through the link and packets over **173 bytes** never come back whole:
MeshCore skips them, openHop cuts them to 173 bytes. Any full-size push is dropped
(`cut` in `link`), since a cut-off packet can't be told from a whole one. Most traffic is far smaller; a long path plus a full-length message can
exceed it. Oversized transmits go out on the pager's own LoRa radio, woken just for
that packet. Oversized receptions are missed while on the link.

## Switching (src/multiradio.*)

`InwNode` is constructed on `MultiRadio`, which implements `mesh::Radio` and passes
each call to whichever radio is in use. The dispatcher already hands over finished
on-air packets and takes raw bytes back, so nothing about packets changes.

- **Link up** (for 3 s, so a flaky link doesn't bounce the radio): transmit and
  receive over the link, LoRa chip asleep.
- **Link down** (Wi-Fi gone, battery saver, companion off, 50 s of silence): wake
  LoRa, carry on as before.
- Switches happen between packets, never during one.
- **Airtime kept honest.** The companion answers as soon as it queues a packet. The
  dispatcher is held for the packet's estimated airtime anyway (the companion's
  settings, Semtech formula, MeshCore's preamble per SF), so its duty-cycle budget
  is right and we don't fill the companion's queue faster than it can transmit.
- **No listen-before-talk** on the link: that's the companion's job.
- **No client repeat** while on the link (`allowPacketForward`): anything forwarded
  would go out again from the companion, late.
- **Signal readouts** on the link are the companion's SNR/RSSI. Packet log entries
  carry `viaLink` so the sniffer can mark them.
- MeshCore's own direct `radio_driver` calls (radio settings, tx power, the app's
  radio stats) still go to the LoRa chip.

## Things to know

- Paths learnt at home end at the home repeater. Away from home, DMs on those paths
  fail once and MeshCore floods, as it does whenever you move.
- Zero-hop tools (discover repeaters, regions) answer from the companion's position.
- The companion's radio settings come from `SELF_INFO` and are shown in the status;
  the pager never changes them.
- A companion's TCP listener takes one client. If your phone is already on that
  listener, use another (openHop has three).
- The link is plain TCP with no authentication, like every MeshCore TCP companion.
  Keep the listener on your LAN; don't port-forward it.
- Wi-Fi with modem sleep costs more than LoRa receive. At home the pager is usually
  on charge; battery saver turns Wi-Fi off at 20% and the pager drops back to LoRa.

## USB commands

```
link                      status and counters
link on / link off
link host 192.168.1.20    companion for the network you're on (port 5000)
link host pi.lan:5001
link host                 clear it
```

## Test plan

1. Off: nothing changes; `link` shows `off`.
2. On, host set on home Wi-Fi. `link` → `up: <name> <freq>/<bw>/sf<n>`, then
   `via_link=1` 3 s later. Send a channel message; repeats are counted; the
   companion's UI shows it transmitting.
3. Receive: a message from another node arrives; `link_rx` rises.
4. Pull the companion's network: within 50 s `back on lora`; messages still go out.
   Restore: `using home link` after reconnect + 3 s.
5. Turn Wi-Fi off on the pager: immediate fallback.
6. Point it at a companion without raw packet support: after three sends, `companion refused raw packets`, back on LoRa.
7. Long path + long channel message: `too_big` rises and it goes out on LoRa.
8. After leaving the link, LoRa receives normally (adverts heard): checks the wake.
9. 24 h on the link: `drops` steady, heap steady (`log`).
