## Test16 — unified ECO menu (1.2.15)

Battery now contains an ECO mode section, followed by the unchanged official
Battery saver section. ECO controls the CPU clock reduction, light sleep,
existing idle haptic/audio behaviour and NFC internal standby with one switch.
It does not turn off GPS, Bluetooth or Wi-Fi; their existing sleep blockers remain.
The green [ECO] status badge reports the selected policy, not instantaneous sleep.
Both [ECO] and SAVER may appear together.

Raise to wake is displayed OFF while ECO is selected. Pressing it shows
“Turn off ECO to enable raise to wake.” for three seconds without changing
its saved preference. Leaving ECO restores that preference automatically.
Quiet when face down still uses the existing fresh pre-notification sample.

Upgrade: the previous CPU + light-sleep selection determines ECO. At boot,
the old separate NFC preference is reconciled with that selection. A fresh
install remains OFF. These preferences still use the existing independent NVS
keys; no settings-blob format is changed. Failed preference writes are reported.

No SD power change, NFC rail cut or LCD wake change is included. The existing
sleep-error latch is NOT fixed by this menu release; toggling ECO off and on
remains the manual recovery. NFC operation and LCD wake still need hardware
validation. Test both ECO states, Battery saver together with ECO, radio receive,
notifications, face-down silence, and NFC read/write/emulation on healthy hardware.

# Experimental CPU power saving (T-LoRa Pager)

Settings > Battery > battery saver > **CPU : power saving** is the single
user-facing control for CPU downclocking and light sleep. Its short description
is "Lowers CPU speed and enables light sleep". It turns both existing preferences on/off together;
`inw-cpu/saving` and `inw-cpu/light` are retained for compatibility, including an
already enabled test9 installation. It is off by default. It is independent of Battery saver:
enabling it does not change GPS, Bluetooth, Wi-Fi, radio settings or loop delays.
The existing UI settings blob is unchanged, including its backup format.
The new CPU preference is not part of exported UI settings backups.

When the logical screen state is off, the CPU switches to 80 MHz immediately,
including during notification banners, audio playback, mesh RX/TX, GPS updates
and background storage work. The clock captured at boot is restored at logical
screen wake, before the panel/wake animation, or when the setting is disabled. Clock changes
use Arduino's `setCpuFrequencyMhz`; failed requests are reported over USB serial.
The CPU-clock option alone does not sleep the CPU. LoRa stays in continuous RX.
A separate experimental light-sleep option is described below.

The approach is informed by Wadamesh beta_98's screen-dependent 240/80 MHz
switching, with an opt-in setting and the pager's existing 30 ms idle delay.

## Build

```sh
python tools/test_cpu_power.py
python tools/test_cpu_sleep.py
python tools/test_motion_sleep.py
python tools/test_sleep_integration.py
pio run -e t-lora-pager
```

The full build includes NFC. `t-lora-pager-public` omits NFC. For an already
installed Squatch Mesh, the application image is
`.pio/build/t-lora-pager/firmware.bin`.
The fork's "CPU power saving test build" GitHub Actions workflow also uploads
this image, the bootloader, partition table and source commit as a build artifact.
It does not publish a site or an OTA release.
Export a backup before flashing. Do not erase flash for this test. Use an
installer that supports the existing partition layout and active OTA slot;
a launcher or an OTA update may have selected a different application slot.
This fork does not change the
official OTA URL or signing keys, so an official update can replace the test build.

## On-device checks (required before an upstream PR)

### MeshCore web flasher / Custom Firmware

Use **SquatchMesh-1.2.15-cpu-test15-Pager-merged.bin**, not the application-only
file, for a full install. The merged image includes the 16 MB bootloader,
partition table, blank OTA-selection data (selecting app0), and the application.
It also covers NVS with blank bytes: back up settings and identity first.
The flasher recognises the `-merged.bin` suffix, writes it at 0x0 and automatically
selects a full erase with a warning. This wipes on-device identity, contacts,
settings and messages; export/SD backup is required if they need to be kept.
An application-only image at 0x10000 does not switch a device booting app1 to
app0, and cannot boot an erased device. Never erase when flashing an app-only file.
Check Settings > System > version for `1.2.15-cpu-test15` after installing.

### Hardware validation

1. With the setting off, check normal operation and the normal CPU clock.
2. Enable it, turn the screen off and wait at least two seconds. The serial log
   should show `[power] CPU 80 MHz`. Wake with the side button; it should show
   the original clock before the wake animation. Repeat with raise-to-wake and
   message-triggered wake, then with message-triggered wake disabled.
   The USB command `cpu` reports the preference and actual clock without waking
   the screen, so it can confirm the clock while the panel is dark.
3. Send and receive direct/channel messages with the screen off. Check delivery
   acknowledgements, retries and queued messages. Repeat with a phone connected
   over BLE and Wi-Fi enabled, including a contact sync.
4. Play notifications with screen wake disabled: the CPU must remain at 80 MHz
   during the banner and sound, not just after they finish. Check for audio gaps.
   Send/receive messages and exercise background storage while it stays dark. Check the map, SD backup, USB console and update checks.
5. Disable the setting and reboot; it must remain off. Enable and reboot; it
   must remain on and downclock only after the screen goes dark.
6. Compare average battery current with the setting off/on under identical
   screen, GPS, BLE/Wi-Fi and mesh-traffic conditions. Use battery-side current
   measurements or a controlled long discharge; USB input current while charging
   is not a measurement of battery discharge. Check that no resets or extra
   message losses appear. No power or reliability claims are made before this.

Test3: CPU status/log lines use CRLF for compatibility with the MeshCore web
console. The `cpu` command no longer needs a following `themes` command.

## Test9: CPU sleep until an event or scheduled work (experimental)

Test10 replaces the two experimental toggles and diagnostic rows with the
single **CPU : power saving** toggle, alongside the existing Battery saver.
The description is directly below it. USB `cpu`/`motion` diagnostics remain
available; System already displays the firmware version. Both preferences are
absent from exported UI backups. An incomplete old pair is shown OFF; enabling
it synchronizes both to ON. Disabling turns both OFF. The second-write failure
path attempts to restore the first preference and reports the failure.

This is a menu change; the test9 sleep, radio, motion and alert algorithms remain
the same. Awake work stays at 80 MHz with the logical screen off;
screen wake restores the boot clock (240 MHz on this build).

The 30 ms sleep slices of test4 are removed, and test5's 5-second battery limit is removed. After the 2-minute boot grace,
`esp_light_sleep_start()` sleeps until the LoRa DIO1 IRQ, the middle/side button
(GPIO0), or the earliest useful deadline. Sleep is capped at 30 seconds, as in
MeshCore/EasySkyMesh ESP32 repeaters. With both CPU options ON and the logical screen OFF, the Pager's battery
reading, charging/saver checks and RTC battery checkpoint now share one
30-second schedule. Normal screen-on operation or either CPU option OFF uses
5-second readings. Critical-low-voltage confirmation uses elapsed time (one
minute), not twelve readings, so changing the cadence does not make it six
minutes. There is no independent 5-second battery/checkpoint wake left. A radio IRQ or button can wake it immediately in that interval.
Deadlines use signed millis differences and are rechecked after preparation.
Pending DM retries/login, adverts, GPS window starts, external sensor readings
and alert-output edges can require an earlier timer wake. USB insertion can be
noticed on the next scheduled battery check (up to about 30 s while sleeping).
The LoRa receiver remains in continuous RX. No RX duty cycling is added.

### Original input behavior preserved

Verified against SquatchMesh 1.2.14 (`8ef76e1`, `src/main.cpp`): once the logical
screen is OFF, keyboard and wheel input is discarded. There is no additional
long-inactivity threshold. The middle/side button wakes the screen; raise to
wake can also do so if enabled outside screen-off saving. Notification wake follows the existing option.
Test4's added wheel wake has been removed. Keyboard, wheel rotation and wheel
press have no sleep wake source and do not inhibit sleep if held. On subsequent
radio/timer/button wake, the original loop drains and ignores their buffered
input while dark. CPU timer/radio wakes do not themselves light the screen.

Test8 introduced the following screen-off policy, retained in test9. It replaces test7's motion IRQ watch with a strict screen-off pause.
With both CPU options ON and the logical screen OFF, raw accelerometer sampling
is stopped immediately, without waiting for immobility. The configured ODR must
read back as zero; failure falls back to polling and visibly blocks long sleep.
Neither Tilt nor Significant Motion is armed. GPIO8 is never a CPU wake source,
and a high IMU line does not prevent sleep. Walking cannot extend an active
sampling window because there is no such window.

Raise-to-wake is suspended while screen-off saving is active, including during
an alert read. The saved motion preferences are retained. The middle button
still wakes the screen, and notification screen wake still follows the original
setting. Screen wake or disabling either CPU option restores normal sampling.
Stay-on-held only samples while the screen is on in this mode.

`quiet when face down` remains available without motion wakes. Immediately
before deciding notification sound/light, the paused accelerometer takes three
consecutive fresh, valid and stable 25 Hz samples, then stops immediately.
A cold restart is allowed up to 500 ms, including settling; normal reads stop as
soon as the three samples arrive. Invalid startup samples reset the streak,
not the whole alert result. Silence requires a stable orientation screen-down
within 30 degrees, acceleration magnitude between 0.85 and 1.15 g, and successive
vector changes no greater than 0.08 g. A stable face-up reading completes just as
quickly and permits sound. A failed/timeout read permits sound rather than using
stale orientation. The sensor is retried on the next message and successful
resume clears a previous transient error. The read never raises the screen.

Test9 fixes two weaknesses in test8's alert read: a single invalid startup sample
latched rejection for the whole read, and its 180 ms limit could expire before
three samples arrived after a cold restart. A successful subsequent resume also
failed to clear the sensor failure flag. The regression simulation reproduces a
quiet first message followed by failed reads when later restarts take 240 ms and
start with zero samples. Twelve successive face-down alerts now stay quiet;
turning face-up permits sound, and the next message recovers from a transient
resume error without reboot or setting changes. This simulates a mechanism
consistent with the observed symptom; hardware traces are still needed to
confirm the precise restart behavior on the user's sensor.

The short local stability check replaces the normal path's two-second
qualification while asleep; it cannot prove the Pager was stationary before
the message arrived. No cached orientation is trusted. USB `cpu`/`motion` now
includes the last alert result and read duration for hardware diagnosis.

**Man-down remains on the original continuous-polling path.** Enabling this
alarm blocks long sleep, as Battery's `man-down uses polling` status explains.
The alarm is not silently disabled or given different immobility semantics.
Raise-to-wake remains suspended while the screen-off saving policy is active.

### Guards and observations

Sleep is also inhibited by the screen/USB, audio playback, enabled BLE/Wi-Fi,
GPS rail power, pending mesh work, unfinished backlight/panel shutdown,
notifications/effects, pending settings saves and active field tools.
The background store must be idle; its queue mutex is held across sleep so
another core cannot begin flash work during entry. Synchronous work completes
before the attempt. GPIO wake configuration is removed and the radio's rising
edge ISR configuration is restored after each attempt. No flash/PSRAM power-down
is forced. A FreeRTOS yield follows wake. On sleep failure, attempts stop until
the option is toggled.

As in test4, with Bluetooth explicitly OFF the inactive Bluedroid host and
controller are suspended before sleeping and resumed before Bluetooth is
re-enabled. Bonds are kept. BLE/Wi-Fi connections are not maintained through
manual light sleep. Radio/GPS/motion preferences are not changed automatically.

Battery shows the motion state and explains that raise-to-wake is paused in
light-sleep mode. USB `cpu` reports `motion_wake=off`. Battery and USB retain
sleep count, accumulated elapsed milliseconds, last/max sleep-call duration and
wake cause. These durations include entry/exit overhead. USB inhibits new sleeps
but retains counters. Hardware checks required before an upstream PR:

1. With both CPU options ON, BLE/Wi-Fi OFF, USB unplugged and man-down OFF,
   wait past the two-minute boot grace. Walk with the Pager in a pocket: the
   screen must remain dark, and motion must not cause continuous CPU activity.
2. Verify the middle button wakes immediately. Keyboard, wheel rotation and
   wheel press must not wake it; saved raise-to-wake must resume outside this mode.
3. With message-screen wake OFF, verify messages/ACKs/retries and sound at 80 MHz,
   followed by sleep. With message-screen wake ON, verify the original screen rule.
4. With quiet-face-down ON, send messages face-down, face-up, while turning and
   while walking. Turn it over during a long sleep before sending another message
   to check that old orientation cannot keep sound muted. Repeat many cycles.
5. Verify battery checks at 30 s while dark, scheduled adverts/retries, storage,
   external sensors, GPS windows, USB insertion and BLE/Wi-Fi re-enabling.
6. Enable man-down: long sleep must stop; original alarm behavior must remain.
7. Compare battery-side current/autonomy with test7 under matching conditions.
   Compilation and simulated tests establish neither hardware stability nor gain.

## Test11: integration with official SquatchMesh 1.2.14

The CPU policy and the single Battery > battery saver > CPU : power saving
control remain the same. Three compatibility fixes follow the audit:

- Battery I2C reads still occur every 30 s while screen-off saving is selected.
  The original 5 s model is advanced once per elapsed 5 s tick using the latest
  readings, so a 30 s reading performs six math updates, not six bus reads.
  This preserves voltage filtering, remaining-capacity drift and the display's
  monotonic percentage rate, instead of slowing them sixfold. Catch-up is capped
  at 100 model ticks after an exceptional long stall. The coulomb counter is read
  and integrated only once. Critical shutdown still confirms low readings over
  60 s; a 30 s sampler can detect a threshold later by up to its reading cadence.
- InwNode captures its free packet-pool baseline after the MeshCore constructor.
  Sleep's radio guard now checks both MyMesh::hasPendingWork() and missing free
  slots. A delayed incoming packet or manually held packet therefore blocks long
  sleep until ALL slots have returned. Existing outbound/dirty-contact behavior
  is retained. No MeshCore dependency patch, radio timing change or RX tuning
  change is required. This conservatively stays awake during delayed RX rather
  than trying to sleep between internal queue deadlines.
- NFC read/write/tag-emulation sessions report an active state to the sleep gate.
  CPU light sleep is inhibited with status `NFC active` until the session closes.
  No NFC wake source is added. A session hidden beneath the lock page is still
  active until its NFC page is closed; it cannot be mistaken for an idle chip.
  Builds without NFC use a false inline stub and need no NFC link dependency.

The integration test compiles the real Battery header, the real sleep gate,
production node constructor/radio guard and NFC active getter against hardware
fakes. It checks 5 s vs. 30 s model equivalence, one I2C group per reading,
monotonic charge display, millis wrap, a near-empty battery's display/shutdown
schedule, multiple allocated RX packets, existing outbound/dirty guards, NFC
read/write/emulation and closing, and the no-NFC stub. It does not establish
hardware radio/NFC reliability. On the Pager, verify delayed flood reception
with a nonzero RX delay base, NFC phone taps while dark with automatic locking
disabled, closing the NFC page then resuming sleep, and a normal battery discharge.


## Test12: diagnose time spent awake

No power policy, wake source, clock, radio or peripheral setting is changed.
USB `cpu` adds a diagnostic window, screen-on/off durations, sleep percentage of
screen-off time, and mutually exclusive time buckets. Blocked idle time is
attributed to the FIRST blocking guard, with separate panel/fade/overlay/settings/
field/storage buckets; simultaneous lower-priority guards are not counted twice.
Loop work is measured separately in eight existing loop phases plus scheduling,
so slow drawing, sensors/radio or storage cannot masquerade as blocked idle time.
Screen state is sampled at phase boundaries; a transition inside a phase can
misattribute that one phase's duration. Times include sleep entry/exit overhead.
Wake counts distinguish timer/GPIO/other; GPIO IRQ vs button is not inferred.
All counters are RAM-only: no periodic logging, flash writes or new wakeups.

1. Over raw USB serial, send `cpu reset` (CRLF). This resets only diagnostic and
   sleep counters, not preferences or a latched sleep failure.
2. Unplug USB, leave the screen off and the Pager face down for a known period.
3. Reconnect without rebooting and send `cpu`. USB/screen-on time appears
   separately; `sleep_pct_dark` uses actual observed screen-off time, not time
   since boot. Capture the entire diagnostic output. Connecting USB may reset
   some serial setups: a tiny window/counters then means the measurement was lost.
4. Do not use the phone's Bluetooth CLI. Rebooting clears the counters.

A high blocked-idle bucket identifies the first guard preventing sleep. A high
work bucket identifies the loop stage consuming awake time. A high sleep share
with excessive battery-side draw instead calls for hardware/current measurements.


## Test13: allow sleep before a deferred contacts save

Test12 hardware diagnostics found 64.3% of screen-off time blocked by `mesh busy`.
Code review identifies a definite overly broad guard: hasPendingWork combines real
outbound packets with dirty_contacts_expiry, which official SquatchMesh batches
for 2 minutes after edits and 20 minutes after routine adverts/path changes.
Keeping the CPU awake throughout those future waits is unnecessary. The capture
cannot prove which subcondition accounts for every measured blocked second.

The existing build-time MyMesh.cpp patch now publishes dirty_contacts_expiry via
inwNoteContactSaveDeadline when hasPendingWork is queried. Its return value and
all official callers retain their meaning. InwNode refreshes this deadline
synchronously while building each sleep budget; due work blocks sleep and future
work caps sleep at the exact save deadline (also capped by the usual 30 seconds).
Actual background writes retain their queue lock across sleep entry. No save is
forced early, skipped or rescheduled. No smaller periodic polling is introduced.
The radio guard still blocks outgoing packets, missing pool slots (including
inbound delays / TX in flight), and non-RX radio states. Diagnostics distinguish
these three radio/pool reasons plus an invalid pool baseline. CPU clock, input
wake sources, motion/quiet-face-down policy and all other guards are unchanged.

Host regression exercises the production patch hook and node methods: a contacts
save 20 minutes in the future permits 30 seconds of sleep; a nearer deadline
shortens sleep; due and overdue saves prevent it; moved/cleared deadlines refresh
immediately; unsigned millis wrap works. Existing allocated-packet and outbound
queue tests remain. Hardware verification must confirm saves occur normally,
RX/TX and audio still work, and mesh blocking decreases before claiming a gain.


## Test14: idle peripherals and fresh face-down alerts

The single CPU : power saving option is unchanged. When both saved CPU flags
are enabled and the logical screen is off, the full NFC build deinitializes an
unused NFC instance and cuts EXP_NFC_EN. CS is released with open-drain HIGH,
following LilyGo's power-down handling. Opening an NFC page powers the rail,
waits 20 ms, and performs the original fresh RFAL initialization (including ISR
attachment). Active read/write/emulate sessions remain powered and continue to
block CPU sleep. Closing a session permits shutdown on the next loop; an old
page destructor cannot close a newer session. Leaving saving restores the rail
without eagerly initializing RFAL. The no-NFC build retains its previous rail
policy and needs no NFC implementation at link time.

The haptic driver enters register standby (MODE=0x40) only after GO reads zero;
failed reads keep it awake. Its configuration remains stored. A vibration first
leaves standby and allows 250 us for wakeup. No new timer wake or sleep blocker
is added: if an effect completes during CPU sleep, standby is deferred until
an existing radio/timer/button wake permits the next loop check. Checks during
an otherwise active loop are limited to once per 100 ms. Existing effects,
patterns, modes, and vibration during screen-off notifications remain available.

The codec uses its established stop register sequence immediately after begin,
even if no startup sound is selected. It tracks I2S ownership, checks power-up
register writes, cleans up install/pin/register failures, and makes repeated
stop safe. The amp policy is unchanged. Initialization/cleanup fixes apply even
with CPU saving off. No guessed new codec register power sequence is introduced.

GPS scheduling now reconciles its rail with the actual settings on every loop.
Settings restored from SD after a wiped NVS also apply GPS power immediately.
This fixes default GPS ON surviving a later restoration of GPS OFF. Field-tool
GPS overrides and the original 2-minute-per-half-hour off-screen window remain.

A regression simulation reproduces the manual-sleep face-up -> turn-face-down
symptom using a buffered burst of three old orientation readings. Test13 accepts
that burst and makes the wrong quiet decision. Test14 drains stopped-stream
FIFO data before restarting, disregards the first 120 ms, and requires accepted
stable readings separated by at least 30 ms within the existing 500 ms limit.
It also covers an alert arriving before the first dark motion tick. The reverse
turn (face down -> face up), repeated alerts, cold restarts with invalid samples,
read/stop failures, and the man-down exception are retained in the tests. This
reproduces a plausible mechanism; it is not a hardware trace of the user's IMU.
No motion wake, sampling between messages, keyboard/wheel wake or new CPU clock
exception is introduced. SD, keyboard power, and the IMU supply remain unchanged.

USB cpu additionally prints requested GPS/NFC rail states, haptic standby and
codec sleep flags, plus the expander output/direction registers read back at the
instant of the command. These are register/software diagnostics, not measured
rail voltages or current. Verify both notification sound and vibration at 80 MHz,
the manual-sleep/turn test in both directions, repeated NFC open/close/read/write/
emulation, and GPS OFF after erase/restore without a second reboot. Measure
battery-side current or a matched discharge curve before claiming a power gain.


## Alignment with official SquatchMesh 1.2.15

Base: release tag v1.2.15, commit 37dd6ebb6fe0a358aff818d18193c2e6438f4373.
This retains the test14 power policy; the firmware reports 1.2.15-cpu-test15.
The official delta from our 1.2.14 base changes only platformio.ini (version)
and src/history.cpp (whole-history writes in 16 KiB pieces with failure logs).
The version conflict is resolved by keeping the upstream version and our test
suffix. History is taken unchanged from the release.

The history rewrite remains synchronous, invoked by startup rescue and compact
on the loop task. CPU sleep occurs only after loop work returns, so no sleep is
inserted between these chunks. The existing queued-storage guard and shared SPI
bus remain unchanged. No new timer, wake source, peripheral power rule, radio
queue/deadline change or notification path is introduced by this release.
The five host suites check the CPU policy, sleep guards/deadlines, motion and
manual-sleep face-down regression, battery integration and peripheral recovery.
A complete NFC-enabled Pager build validates integration with the release.
Hardware reception, notifications, NFC and consumption still need device testing.


## Test15: independent NFC power saving, supply always on

Settings > Battery > battery saver adds **NFC : power saving** and the description
**Puts unused NFC into standby**, directly below the CPU option, in the full NFC
build only. Its own opt-in NVS preference is inw-nfc/saving (default OFF); like
the CPU preference it is not in the ordinary UI settings backup. Activate both
options explicitly for the new battery test. CPU policy/guards, motion, panel
commands, animations and shared SPI host setup remain identical to test14.

This supersedes test14's NFC rail shutdown: neither option ever cuts EXP_NFC_EN
or floats NFC CS now. Supply and inactive CS remain high, including while CPU
light sleep and screen wake/rendering occur. NFC saving is independent of CPU
saving and screen state: whenever there is no active NFC session, it enters the
ST25R3916 internal Power-down mode and stays there until needed. With the option
OFF, original idle RFAL behavior is retained; disabling it after standby resumes
normal RFAL initialization. Active reading/writing/emulating sessions are never
interrupted by either setting, and still block CPU light sleep.

The pinned RFAL v1.0.0 deinitialize explicitly keeps OSC on. Therefore after
RFAL deinit (mask IRQs, detach ISR) the firmware disables MCU/LF clock outputs
in IO_CONF1, preserving unrelated bits, then writes OP_CONTROL=0 to disable the
oscillator, RF RX/TX, field detection and wake-up mode. It verifies OP_CONTROL=0,
clock bits and a valid ST25R3916/3916B identity before marking standby. All chip
access uses RFAL's existing SPI transactions; no SPI begin/end/reset is added.
A fresh RFAL initialization reattaches IRQ on each subsequent NFC session.

An init/write/readback failure is latched and reported, not endlessly retried.
Toggling the NFC setting or opening an NFC page permits another attempt. No new
wake source, deadline, recurring register read or polling task is added. USB cpu
adds [nfc] saving/standby/state and cached last op/io readback (not live current).
The datasheet's ~0.8 uA typical applies to the NFC chip under its stated conditions,
not a measured board current. Verify standby, actual NFC operation and whole-board
consumption on the device before claiming that value has been achieved.

Host tests cover persisted opt-in and rejected writes, CPU/screen independence,
all NFC modes, old-page destructor protection, oscillator-off despite RFAL deinit,
upper IO bits preserved, read/write/identity failures (including zero/FF), bounded
retries, disable/re-enable recovery and no-NFC stubs. The panel recovery/reset
experiment was deferred and is NOT part of this version. This controlled change
is intended to test whether eliminating NFC supply cycling removes the screen
bug; its electrical root cause and resolution still require hardware confirmation.

References: ST25R3916 DS12484 Rev8, sections 4.1/4.2 and Table121;
RFAL v1.0.0 commit f0b22770e509efcbcaa7c5ebf8fd3f04796a14f3,
rfalDeinitialize(), st25r3916Deinitialize(), OP_CONTROL and IO_CONF1 definitions.
