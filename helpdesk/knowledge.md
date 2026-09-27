These notes add to the website pages. Where they disagree, the website wins.

Where people get help
- This helper on https://squatchmesh.com/help.
- GitHub, for public questions and bug reports: https://github.com/BamBam1121/inw-mesh/issues and
  https://github.com/BamBam1121/inw-mesh/discussions (needs a free GitHub account).
- The developer, by the hand-off from this helper. One volunteer; replies can take a few days.
- Email: support@squatchmesh.com reaches the developer directly, for anyone who'd rather write an email.
  The hand-off is still best from here, because it sends the conversation along.

Finding the firmware version
- On the pager: Settings > System > version.

Buttons and power (the guide's "Buttons & power" section has the same)
- Bottom edge, left to right: left = reset (restart, nothing lost); middle = BOOT, the one button the
  firmware can use; right = power on only.
- Turning it off needs firmware 1.2.0 or later: hold the middle button until "Power off?" appears, then
  Enter (or press the wheel). Also Settings > power off. Before 1.2.0 there was no way to turn it off;
  the answer then is to update (Settings > System > check for updates).
- Turning it on: hold the right button about a second, or plug in USB. The right button is wired to the
  charger chip, not the processor, so no firmware can give it another job.
- It can't power off with USB plugged in (the charger keeps it running); unplug first.
- Powered off hears nothing: messages sent meanwhile are missed. To save battery but keep receiving,
  tap the middle button to turn just the screen off.
- Five fast taps on the middle button start the SOS countdown, 20 seconds from 1.2.2 (if SOS is set up
  under Tools > field). Any key cancels it.
- Download mode for flashing: hold middle (BOOT), tap left (reset), let go of middle.

Sound, battery and screen (1.2.1)
- No sound at all, though sound is on in Settings: before 1.2.1 one bad moment (a restart in the middle of
  a sound, say) could leave the speaker stuck silent until the pager was fully powered off, and it can't
  power off on USB. Fix: update to 1.2.1 (Settings > System > check for updates). Also check the volume
  isn't at 0. If it's still silent on 1.2.1, hand off to the developer.
- Battery percentage: from 1.2.1 the pager counts the charge going in and out itself. It shows 99% while
  charging and 100% only once the charger reports full. If the figure seems off after updating, charging
  to full once sets it straight.
- Every theme has its own animation when you move between screens, unlock and lock. They're meant to be
  there; there's no setting to turn them off.

First start (1.2.2)
- After a fresh install and storage setup, three questions: radio region (MeshCore presets, or keep the
  current radio), time zone (named zones, follow daylight saving), clock and units (12/24 h, miles/km).
  Each has "keep"; all can be changed later in Settings. Updating from 1.2.1 asks them once too.

Wi-Fi (1.2.2)
- It says why it can't join: "wrong password?", "not found (2.4 GHz only)" (5 GHz networks are invisible
  to it), "no answer, weak signal?", "joined, router gave no address". Before 1.2.2 it could say
  "scanning..." forever; that's fixed. To change a saved password: scan + join the network again.

Message details (1.2.2)
- A message's details (roll onto it and press; the bottom of that menu) show the route, with repeaters
  named when they're in your contacts. "4h 2B" beside
  the hops means 4 hops, 2-byte repeater ids; Settings > Messages > "with repeater id size" hides the 2B.

Top header, sensors and IO9 (1.2.2)
- Pinout (LilyGo's J3): 1 GND, 2 3.3 V, 3 TX (IO43), 4 RX (IO44), 5 SCK, 6 MOSI, 7 MISO (5-7 are the
  radio's bus: leave them alone), 8 IO9, 9 SDA (IO3), 10 NRF_CE, 11 SCL (IO2), 12 5 V (only on USB).
- Sensors: BME280, BMP280, SHT3x, SHT4x, AHT20, BH1750 on 3.3 V, GND, SDA, SCL. Shown under Tools > top
  header and sent as telemetry. Not showing: check it's 3.3 V (not 5 V) and SDA/SCL aren't swapped.
- IO9: an LED (IO9 > 330 ohm > LED > GND) or an active 3.3 V buzzer (IO9 and GND). Modes: off, flash on
  new messages, on while unread.

Region scopes (1.2.2) - same as the MeshCore app's regions / flood scope
- A region keeps messages to the repeaters that carry it, instead of the whole mesh. Its key is made from
  the name, so it has to match the repeaters' exactly, capitals included (names are letters, digits and -,
  shown without #). A repeater drops a message for a region it doesn't carry: a wrong name means messages
  that reach almost no one.
- It works as the app does: a list of regions you've added, a per-channel "Set Region Scope" (in the
  channel's chat: roll onto a message, press, "region scope"; or Settings > Channels), and a "default region
  scope" for everything else (Settings > Radio & Mesh; the app's Experimental > Default Region Scope is the
  same setting). A channel's region overrides the default; "clear scope" puts it back on the default.
- A scoped channel's chat title shows "Region: name".
- "discover regions" asks the repeaters in direct range which regions they carry and how many still pass
  messages with no region. Choosing from that list is the safe way.
- Messages sent from the MeshCore app over Bluetooth follow the app's own region settings; messages sent on
  the pager follow the pager's.
- Messages stopped arriving after setting a region: clear it, then use "discover regions" to find one the
  repeaters carry. Private regions (names starting with $) aren't supported yet.

Things to check before handing off a problem
- Which firmware version, and whether it was installed from squatchmesh.com/install or updated over Wi-Fi.
- What is on the screen (a photo helps the developer; the visitor can mention they have one and the
  developer will ask for it by email).
- Whether it happens every time.

Installing and flashing
- The website installer works in Chrome or Edge on a computer (Web Serial). Safari and phones can't
  flash. New Firefox versions can try, but its USB support is new and has failed to open the pager's
  port (seen on a Mac): Chrome or Edge is the reliable choice.
- The installer never needs "erase device" ticked. Erasing wipes contacts, channels and messages.
- If no port shows up: try a different USB cable (many are charge-only), a different USB port, and
  follow the "If something goes wrong" section of https://squatchmesh.com/install.
- If a pager won't start after installing, follow "It will not start afterwards" on the install page
  (BOOT/RESET into download mode, then first install again) before trying anything else.

Two different radios - the most likely cause of "radio not responding"
- LilyGo sells the T-Lora Pager with either an SX1262 or an LR1121 radio; they look the same outside.
  Squatch Mesh drives BOTH since version 1.1.17 (it detects which one is fitted). Versions before 1.1.17
  only drove the SX1262: on an LR1121 pager they boot fine but the radio never comes up ("radio init
  failed" / "radio not responding" / "radio down", nothing in or out, no RF in the status bar).
- So when someone says the radio doesn't work, FIRST ask their version (Settings > System > version).
  If it is older than 1.1.17, the fix is simply to update: Settings > System > check for updates over
  Wi-Fi (works without the radio), or the Update button on squatchmesh.com/install. No erase, nothing lost.
- If they are on 1.1.17 or later and the radio still does not come up, hand off to the developer, with
  which radio it is if they know (a Wadamesh firmware file name saying sx1262 or lr1121 is the reliable
  answer). https://squatchmesh.com/install#radio

Errors the browser installer reports (the install page sends these to me automatically)
- "No port picked" / NotFoundError: the port chooser was closed, or the pager is on a charge-only cable.
  A data USB-C cable and a different USB port fix most of these.
- "Failed to open serial port" (the installer now says "Couldn't open the pager's USB port"): nothing
  was written, the pager is unchanged. In order: unplug the pager and plug it back in, press try again
  and pick the pager again when the browser asks (a fresh pick fixes a port that went stale when the
  pager restarted); if it is Firefox, use Chrome or Edge; otherwise close anything else holding the
  port - another tab with the installer, Arduino IDE, a serial monitor. The report includes the browser.
  Not a firmware fault: no hand-off unless it still fails in Chrome/Edge after a replug and a fresh pick.
- Timeouts, "Failed to initialize", "Chip not responding": put the pager into flash mode -
  Settings > System > usb flash mode, or hold BOOT, tap RESET, release BOOT - then press install again.
- A failure part-way through writing is safe to retry: press the same button again. Both buttons write
  the bootloader and partition table, so a half-written pager is recoverable by running first install
  again. Never tell anyone to tick "erase device" to fix a failed flash - that erases their contacts.

Keeping keys and contacts on a first install (from other firmware)
- A first install replaces storage. On first start Squatch Mesh restores identity, contacts and channels from
  the SD card: from a Wadamesh store (/meshcomod on the card) or from a MeshCore .json config export saved
  on the card as /meshcore-backup.json. Walk people through https://squatchmesh.com/install#keep-data
  BEFORE they flash. From Wadamesh: SD card in, turn on "Store data on SD", let it reboot, check the card
  has meshcomod/identity, then flash with the card in.
- Ripple, Meshtastic and factory firmware keys can't be carried over; they start with a new identity.
- If someone already flashed from other firmware without doing this and lost their keys or contacts, hand
  off to the developer (urgent) and tell them not to reformat the SD card or flash again.

Data
- Contacts and channels are kept on the pager and, with an SD card, also backed up to it. Loss of
  contacts or messages is always worth handing off to the developer.
