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
  under Tools > field). Any key cancels it. From 1.2.2 the man-down alarm can start it too (below).

Motion sensor (1.2.2): raise to wake, quiet when face down, man-down alarm
- The pager has a Bosch BHI260AP motion sensor (accelerometer + gyroscope). No magnetometer, so no
  compass; nothing in the firmware claims one.
- Raise to wake: Settings > Display > raise to wake, ON by default. Lifting the pager into view (tilted
  towards you, out of a pocket or up off a table) turns the screen on at the lock face; it goes dark again
  after 10 s if not unlocked. It can now and then light in a pocket (sitting down, say): that costs almost
  nothing and keys still can't unlock it. Turn it off there if it bothers them.
- Quiet when face down: Settings > Notifications, OFF by default. Flat, screen down and still for 2 s:
  messages arrive with no sound, vibration, keyboard light or screen wake. Picking it up ends it. A pocket
  holds the pager on its edge, so it doesn't trigger there; lying flat face-down in a bag would.
- Man-down alarm: Tools > field > SOS beacon > "no movement for" (off, 5, 10, 15, 30 min), OFF by default.
  No movement and no key for that long: a minute of chirps and buzzes, faster at the end, on an amber
  "Are you OK?" screen, then the normal 20 s SOS countdown (red, siren). Moving the pager or any key resets
  it. It waits while charging and while an SOS is already on. The SOS message says "(no movement for N
  min)". It needs an SOS channel like the normal SOS. The chirps sound even with sounds off or in quiet
  hours, on purpose.
- A pager left on a table with the man-down alarm on WILL chirp and then send an SOS unless someone
  cancels: tell them to turn it off when they put the pager down for the day.
- "motion sensor not responding" (at boot or in those menus): restart the pager. If it stays, hand off to
  the developer.
- The lock screen's scene leans a little as the pager is tilted (stars and far hills slide, the character
  stays): that's the motion sensor, on purpose.
- Battery saver (20%, or turned on by hand) switches the motion sensor off: raise to wake, quiet when face
  down and the tilting scene pause until it ends. The man-down alarm is the exception: if it's on, the
  sensor keeps running for it alone. "starts when battery saver ends" on turning one of them on is why.
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
  there. From pager 1.2.8 / T-Deck 1.2.4-beta6, Settings > Display > "animate screen changes" turns off
  the ones between screens for anyone who finds them slow; earlier versions have no such setting.

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
- A region keeps messages to the repeaters that carry it, instead of a plain flood. Its key is made from
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
- On 1.1.17 or later with "radio=none" / "radio init failed: radio not responding", there are two causes
  and they look the same, so do NOT say which it is and do NOT call it a hardware fault:
  (1) The radio did not start this time (from 1.2.8 the pager tries three times at start-up). Seen for real on 2026-10-02: a pager on 1.2.7 reported no radio
  on several starts in a row, then came up and worked. Reinstalling does not help. Ask them to: take the
  SD card out if one is in, switch fully off, wait ten seconds, switch on. If the radio comes up, ask them
  to put the card back and say whether it still does (the developer wants to know if the card matters).
  (2) A pager sold with a radio the mesh can't use. LilyGo sells it with an SX1262 or LR1121 (both work)
  and also with a CC1101 or an SX1280 (2.4 GHz); Squatch Mesh and MeshCore cannot use those two. Such a
  pager boots and Wi-Fi works, but it never has a radio. Ask what their order or the box says.
  Always hand off to the developer with: the version, which radio the order says, whether an SD card was
  in, and whether a full power-off brought the radio up.
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

SD card says "not mounted" / "not found" (Tools > device info, Settings > Data)
- The pager reads cards formatted FAT32 with an ordinary (MBR) partition table. A card that a Mac or PC
  reads fine can still fail here: macOS Disk Utility often erases cards with the "GUID Partition Map"
  scheme, and exFAT (the default for cards over 32 GB) is not read either. The likely fix on a Mac: Disk
  Utility > View > Show All Devices, select the card itself (not the volume under it), Erase, Format
  "MS-DOS (FAT)", Scheme "Master Boot Record". On Windows: format as FAT32 (cards over 32 GB need a tool
  such as guiformat). This erases the card, so copy anything on it off first.
- From pager 1.2.8 the pager tells the two apart: "none" / "not found" means no card answers; "can't be
  read" means a card is in but isn't FAT32 with an ordinary partition table. For a card that can't be
  read, Settings > Backups has "format this card for the pager": it erases the card and makes it FAT32
  (about ten seconds for a 32 GB card), then backs up to it. It is only offered for a card that can't be
  read; a working card is never formatted, and there is no way to format one from the pager.
- The T-Deck has the same from 1.2.4-beta6. Before 1.2.8 / beta6 there is no format command on the
  device: format on a computer as above.
- If a card formatted that way still isn't mounted after a restart, or a second card fails too, hand off
  to the developer with the version, the card's size and make, and how it was formatted.

Data
- Contacts and channels are kept on the pager and, with an SD card, also backed up to it. Loss of
  contacts or messages is always worth handing off to the developer.

Halloween theme (pager 1.2.7 and later, T-Deck 1.2.4-beta4 and later)
- A fifth built-in theme: Settings > Theme > Halloween. Orange and purple, and the sasquatch on the lock
  screen is in costume: zombie, witch, vampire, ghost, pumpkin head, skeleton or mummy.
- The costume changes every time the screen goes off and comes back on (and at every restart), and so does
  where he walks (a street of houses, a graveyard, a pumpkin patch, the woods). It moves on to the next one
  each time; there is no setting to pick one. (In pager 1.2.6 beta and T-Deck 1.2.4-beta4 it changed only
  at a restart.)
- From pager 1.2.7 and T-Deck 1.2.4-beta5 it has its own screen changes: slime running down going forward,
  a swarm of bats going back, the lock screen opening like doors, a jack-o'-lantern when locking, lightning
  on waking, and the picture melting away when the screen turns off. They're meant to be there.
- Its sounds are short tunes with a rhythm instead of plain beeps (start-up, message, direct message,
  mention, plugging in), and its vibration is a heartbeat. Volume and "sound off" work as for any theme.
- It has the Squatch theme's cards and screen changes. It can't be used as the look for a theme of your
  own in the theme maker (that offers Squatch, Blocks, Hero and Aurora).

Your own themes (pager 1.2.7 and later, T-Deck 1.2.4-beta4 and later)
- squatchmesh.com/theme-maker makes a theme of your own: pick one of the four looks to build on (it sets
  the lock scene, card shape, sounds and screen changes), pick the colours, give it any name (up to 20
  letters), and watch a live preview of the real screens. Then plug the device in with a USB data cable and
  press the send button. No reflash: the device restarts once when the page connects, takes the theme,
  switches to it, and lists it in Settings > Theme under the four built-in ones.
- Sending needs Chrome or Edge on a computer, the same as the installer. Phones, Safari and Firefox can't
  send over USB; the page still works there for designing, and gives a link to open on a computer.
- Up to four of your own themes on a device. Sending one with a name it already has replaces that one.
  When it has four, take one off first: on the page press "See what's on it" (it lists what's on the
  device with "remove" beside each, without sending anything), or from pager 1.2.8 / T-Deck 1.2.4-beta6
  on the device itself, at the bottom of Settings > Theme.
- Nothing leaves their computer: the theme goes over the USB cable to the device and nowhere else. It is
  not sent to the website, not shared on the mesh, and other people never see the name.
- Themes stay through restarts and firmware updates. A full erase (installer "start fresh") removes them.
- "This firmware is from before themes": update first (Settings > System > check for updates), then send.
- If a colour choice makes text hard to read the page warns about it; a theme that came out unreadable
  can be fixed by choosing another theme in Settings > Theme, or by sending it again with the
  same name and better colours.

New in 1.2.8 (released 2026-10-02, the current pager release)
- Sounds no longer skip. The start-up tune had a gap in it just as the lock screen appeared, in every theme.
- SD cards: the pager says when a card is in but can't be read, and can format it (see the SD card notes).
  It also no longer tries to mount a card that isn't there or can't be read, which could crash it.
- The "Update to X?" question only appears on the home screen, and a version you say no to isn't offered
  again (Settings > System > check for updates still offers it).
- The radio is tried three times at start-up, and a problem report says why if it didn't answer.
- No more freeze once a minute. With Wi-Fi on and a long contact list, everything stood still for over a
  second every minute (it showed as typing or scrolling that hung, then caught up).
- Keys pressed while one screen changes to the next are no longer lost.
- Settings > Display > "animate screen changes": off makes screens change at once instead of with the
  theme's half-second animation (locking, unlocking, waking and sleeping keep theirs). On by default.
- Settings > Theme lists your own themes at the bottom with "remove" beside each.

New in 1.2.7 (released 2026-10-01; 1.2.6 was only on the beta channel for an hour)
- The Halloween theme (see "Halloween theme" above).
- Themes of your own from squatchmesh.com/theme-maker (see "Your own themes" above).

New in 1.2.5 (released 2026-10-01; there was no official 1.2.4, only a beta)
- A redrawn sasquatch on the lock screen: fur, a face, he waves, blinks and moves his mouth when he talks.
- "Stay on while held": the screen stays on while the pager is in your hand. Settings > Display, the last item.
- Problem reports now carry the name the pager uses on the mesh (see Problem reports below).
- Fixed: the map with no SD card in (it crawled and could crash; it now keeps downloaded tiles in memory and
  loses them when the map is closed), a failed map tile holding up the next ones, a chime on plugging in when
  already full (95% or more), "1 hops", and the sasquatch's late-night hellos (10 pm to 5 am).
- Still open: a rare crash on the map while a downloaded tile is saved to an SD card. If someone reports the
  map restarting the pager, hand it to the developer.
- Pagers on 1.2.3 or later install it by themselves when idle on Wi-Fi.

New in 1.2.3 (the release after the 1.2.2 beta; 1.2.2 was only ever a beta, 1.2.3 has all of it)
- Updates install by themselves: on Wi-Fi it checks when Wi-Fi connects and every 6 hours, and puts a new
  official release in once the pager is idle (screen off 2 minutes, charging or above 30%, no SOS or phone
  sync). It says "Updated" afterwards; contacts, channels and settings are kept. Settings > System >
  "install updates by itself" turns it off (then it asks first, as before). Beta builds always ask.
  Pagers on 1.2.1 or older still get asked once for 1.2.3; the self-install starts from 1.2.3 on.
- Problem reports: after a crash, a watchdog restart or a real error, the pager sends the developer a
  short report over Wi-Fi when idle (version, why it restarted, where it crashed, memory, battery, its last
  log lines with Wi-Fi names and channel names removed; from pager 1.2.5 and T-Deck 1.2.4-beta3 also the
  name the device uses on the mesh, so the developer knows whose it is), plus a once-a-day check-in (board,
  version, a random number, no name). Never messages, contacts, keys or position.
  Settings > System > "send problem reports" turns both off. Tools > "send log to the developer" sends the
  log on purpose - useful to suggest when someone describes a bug. The privacy page has the details.
  If someone asks whether the firmware phones home: yes, only this, only on Wi-Fi, and it can be turned off.
- The talking sasquatch: on the lock screen he has a speech bubble. He reacts to a real shake (back and
  forth; picking it up or setting it down doesn't count) and hops, says hello for the time of day when the
  pager is picked up after a while, and speaks up for a new message, the charger plugged in, a low battery,
  and being held tipped right over. Settings > Display > "sasquatch talks" turns him off. The other themes'
  characters talk too. He needs the motion sensor for shakes (off in battery saver).
- The lock screen shows the time once (the big clock) and the date; the top bar has no clock there but
  keeps it on every other screen.
- Wi-Fi left on away from saved networks now tries less and less often (up to every 15 minutes with the
  screen off) instead of every 20 seconds - it used to cost a lot of battery.

T-Deck beta (1.2.2-beta6 and later)
- Screen dark on battery: only the LoRa radio runs. The GPS sleeps until the screen wakes, and Wi-Fi turns
  off a minute into the dark and comes back when the screen wakes. Plugged in, Wi-Fi stays on, so on a
  T-Deck updates install themselves while it charges on Wi-Fi.
- The same problem reports and daily check-in as the pager, and the same switch to turn them off.
- It has a Bluetooth console for the developer's tools (admin password only); phones won't list it in
  Bluetooth settings, which is normal.

T-Deck keyboard: numbers and symbols
- Numbers and symbols are typed with the SYM key (bottom row, beside the space bar), not ALT: press sym,
  then the key with the number or symbol printed on it (or hold sym while pressing it). The keyboard is its
  own small computer inside the T-Deck and works this way under every firmware.
- ALT does not type anything in Squatch Mesh. On the keyboard's own chip, alt+B switches the key
  backlight; Squatch Mesh sets the backlight itself (Settings > Display).
- If sym + a key gives nothing at all, ask which keys they tried and hand off to the developer.

Signal bars (pager 1.2.9 beta / T-Deck 1.2.4-beta7, both 2026-10-02)
- Four bars in the top bar, next to the battery, and a larger set on the lock screen. They show how well
  the last packet was heard (its SNR): 4 bars at 5 dB or better, 3 from 0 dB, 2 from -7 dB, 1 below
  that. Unlit means nothing was heard lately. They say how well this device hears its nearest
  neighbour, not whether a message will reach someone far away.
- Settings > Display: "signal bars" off / small / large (top bar), "lock screen signal" off / small /
  large, "lock screen signal side" left / right, and "signal check every" never / 1 / 2 / 5 / 10 / 15 /
  30 / 60 min (5 by default).
- The signal check: when nothing has been heard for that long and the screen is on, the device asks the
  repeaters in direct range to answer. It is zero hop, so it is never repeated across the mesh. It
  doesn't run with the screen off or in power saver. One bar sweeps while it waits; the bars fill when
  an answer comes.
- From pager 1.2.10 beta / T-Deck 1.2.4-beta8 the bars show the best packet of the last two minutes, so
  they no longer jump with every packet.
- Pager 1.2.9, 1.2.10 and 1.2.11 are betas: only pagers set to take beta updates get them. The pager release is
  still 1.2.8.

T-Deck GPS that stays on "searching" (added 2026-10-09; read this before handing a GPS complaint off)
- Ask first: is it a T-Deck or a T-Deck Plus? The plain LilyGo T-Deck has NO GPS in it. Only the T-Deck Plus
  has one built in (or a plain T-Deck its owner wired a module into). On 1.2.4-beta9 and beta10 a T-Deck
  with no GPS still says "searching": those builds cannot tell a missing module from one with no fix yet.
  1.2.4-beta11 can (see below).
- On a T-Deck Plus the GPS only runs while the screen is on. With the screen dark it is put to sleep to
  save the battery, so leaving the T-Deck outside with its screen off gets nothing, however long.
- A GPS that has no fix yet has to listen to open sky for about a minute without a break, sometimes two
  or three. The lock screen goes dark after 10 seconds and the other screens after the screen timeout
  (60 seconds unless changed), and each time the GPS has to start again. Someone who only glances at it
  can see "searching" all day. This is the usual cause, and it is a weakness of the firmware, not of
  their T-Deck: say so, and that the developer is changing it so a first fix finishes with the screen dark.
- What to tell them to do for the first fix: go outdoors with open sky, open Tools > GPS (or the map), and
  keep the screen on for three minutes by touching it now and then. Settings > Display > "screen off
  after" can be made longer for this. Once it has a fix, later fixes take seconds for the next few hours.
- Also check: Settings > GPS is on, and battery saver is off (saver turns the GPS off).
- 1.2.4-beta11 shows what the module is doing (Settings > System > "beta updates" on, then check for
  updates): Tools > GPS says "module not heard" (no data from a module: a plain T-Deck, or a fault), or
  "hearing N satellites" with how strong the best one is.
- Hand off only if: a T-Deck Plus on beta11 says "not heard"; or it hears 5 or more satellites with the
  screen kept on outdoors for five minutes and still has no fix. Ask for an email address so the
  developer can reply.

The installer, on Linux and with the wrong thing picked (added 2026-10-09)
- "Couldn't open the USB port" on Linux is nearly always permissions: the user is not allowed to use
  serial ports. Fix: `sudo usermod -aG dialout $USER` (on Arch the group is `uucp`), then log out and
  back in. Chrome or Chromium installed as a snap or flatpak often cannot reach serial ports at all:
  use the ordinary .deb or .rpm Chrome. If it still fails, ModemManager or brltty may be holding the
  port; unplug, plug in again and try once more.
- In the browser's list of devices the pager and the T-Deck are called "USB JTAG/serial debug unit".
  Entries like ttyS0, a Bluetooth port, or "USB-Serial Controller" (an adapter cable) are something
  else, and picking one ends in "failed to connect".
- "That looks like a T-Deck, not a pager" (or the other way round): they are on the other device's
  page. Nothing was written. The T-Deck's installer is squatchmesh.com/t-deck, the pager's is
  squatchmesh.com/install.

A pager or T-Deck that runs warm or drains fast (added 2026-10-09)
- Warm while charging, until it reaches 100%, is normal. Warm while idle off the charger is not.
- Before handing off, ask: is it on the charger; which firmware; are Wi-Fi, GPS and Bluetooth on
  (Wi-Fi away from a saved network and the GPS are the two big ones); is SOS, the trail or a range
  test running (they keep the GPS and radio busy); is anything plugged into the top socket; about how
  many percent an hour it loses with the screen off; and Settings > Battery > battery health.
- Ask them to press Tools > "send log to the developer" while it is warm, and to leave an email
  address. Without an address the developer cannot ask anything further.
- If it is too hot to hold, or the battery looks swollen: stop using it and stop charging it. Hand
  off as urgent.

T-Deck 1.2.4-beta11 (2026-10-08) - only for T-Decks with beta updates switched on
- It is the newest test build: a T-Deck with Settings > System > "beta updates (every build)" switched on
  gets it over Wi-Fi (Settings > System > check for updates). Everyone else stays on 1.2.4-beta9, and the
  installer at squatchmesh.com/t-deck still installs beta9.
- Wakes faster: a message lights the screen in about half a second, with its sound playing cleanly. It
  used to take around three seconds, with the sound breaking up.
- Lists follow the finger by the pixel instead of jumping a row at a time: Settings, conversations, the
  chat list, contacts.
- Fewer freezes: the update check and problem reports no longer hold the screen and the radio while they
  talk to the internet, and the lock screen no longer stutters while the T-Deck saves.
- Trackball: it counts every step of the ball, and up/down and left/right do different things on the home
  screen, the Settings tiles, quick settings and the emoji picker. Rolling sideways on a value changes it.
  Settings > Display > "trackball speed" sets slow, medium or fast (medium unless changed). If the
  trackball feels too quick or too slow, that is the setting.
- GPS: Tools > GPS and Tools > hardware check now say whether a GPS module is heard at all and how many
  satellites it can hear, not only "searching". "not heard" means no data is arriving from a module (no
  module, or its wiring); "hearing 0 satellites" means the module works and needs open sky; a count with
  a signal number means it is receiving and a fix should follow within minutes outdoors. Someone stuck on
  "searching" on beta9 or beta10 should take beta11 and read that line.
- The update question shows all of its text (it was cut off after a few lines).
- A crash left on the device by another firmware (a multi-boot launcher, or what was installed before) is
  no longer sent in as a Squatch Mesh crash.
- Screen-change animations are the same speed as before, and the same as the pager's.

T-Deck 1.2.4-beta10 (2026-10-04) - only for T-Decks with beta updates switched on
- The T-Deck now has two update channels, like the pager. Everyone gets 1.2.4-beta9, from the installer at
  squatchmesh.com/t-deck and over Wi-Fi. A T-Deck with Settings > System > "beta updates (every build)"
  switched on gets the newest build (1.2.4-beta10 when this was written; 1.2.4-beta11 now). To try it: switch that on, then
  Settings > System > check for updates (or leave it charging on Wi-Fi). To stop getting test builds,
  switch it off; the device stays on what it has until the regular build catches up.
- Quick settings: pull down from the top of the screen, or tap the right side of the top bar. A panel with
  a brightness slider and a volume slider that follow the finger, and tiles for Wi-Fi, Bluetooth, GPS,
  Sound, battery Saver and all Settings. A dot on a radio's tile means it is connected (a network, the
  phone, a GPS fix); a ring means it is on and still looking. Swipe up, or the back arrow, closes it.
  The trackball works too: roll to move, click to switch a tile or take hold of a slider.
- Night brightness: Settings > Display > "night brightness" (off, or a level), "night from" and "night
  until" (9 pm and 7 am unless changed). Between those hours the screen uses the night level and goes
  back by itself in the morning. It needs the clock to be set. The quick settings' brightness slider
  changes whichever level is in use at the time.
- The Display setting "signal check every" is now called "signal check if quiet for": it was never a
  timer, it only runs when nothing has been heard for that long.

Pager 1.2.11 beta and T-Deck 1.2.4-beta9 (both 2026-10-03, later the same day; beta9 is the T-Deck build everyone gets)
- Settings > Backups > "auto backup every": hour, 3 hours, 6 hours, 12 hours or day. It was a line of text
  saying "once a day" that could not be changed. Day is still the default. Backups run with the screen
  off, to /inw on the SD card.
- T-Deck home screen: the time is shown once, in the large clock. The top bar shows the device's name
  there (other screens keep the time in the top bar).
- Checking for updates: if the device can't connect to the update site it tries once more by itself, and
  then says "couldn't connect to the update site" (it used to say "update site said -1"). That is the
  device's connection at that moment, not the site: trying again a little later is the fix.
- Pager 1.2.11 is a beta: only pagers set to take beta updates get it. The pager release is still 1.2.8.

Pager 1.2.10 beta and T-Deck 1.2.4-beta8 (both 2026-10-03)
- Map: a restart while zooming or moving around the map is fixed. Finishing one map-tile download closed
  the file the previous tile was being saved to on the SD card (a fault in the HTTPS library, worked
  around). Anyone who reports the device restarting on the map should update to this version first.
- Screen: no more rainbow static when a message lights the screen (the light came on before the panel had
  woken), and no more static now and then at power-on.
- Direct messages are tried up to 15 times where it was 3. The third and later tries flood, and from the
  fourth the wait between tries grows, so they spread over a few minutes. Settings: "flood on last
  retry" is now called "flood from the third try".
- A retried copy of a message already received is shown once. The bubble says x2, x3 for the copies that
  arrived, and there is no second notification. The same goes for a message the sender sent again by
  hand within three minutes with nothing said in between. A device on older firmware, or another app,
  still shows every copy it receives.
- T-Deck: tapping the yellow "N new" badge in the top bar goes to the new messages. With unread messages
  in one conversation it opens that conversation; with several it opens the Messages list. On the lock
  screen it opens after the swipe up.
- Multi-boot launchers (Launcher by bmorcelli, and similar): from this version an update over Wi-Fi never
  replaces another firmware by itself. If the slot an update would be written to holds a firmware that
  isn't Squatch Mesh, the device shows "Update available: use your launcher" once and leaves it alone.
  Settings > System > check for updates still works there, and says plainly that it will replace the other
  firmware. To update and keep the other firmware: download the new firmware.bin (the T-Deck one is on the
  GitHub release for that version), put it on the SD card and install it from the launcher over the
  existing Squatch Mesh entry. Versions before this one (pager 1.2.9, T-Deck 1.2.4-beta7 and older) do
  overwrite the other slot when they update; turning off Settings > System > "install updates by itself"
  stops that.
- Switching to another firmware from a launcher and back: the other firmware can erase the storage all of
  them share. Squatch Mesh then puts back the name, keys, channels and radio settings from its safety
  copy, and contacts and messages from the SD card's daily backup if there is a card (contacts otherwise
  come back as nodes are heard). Before this version the safety copy was only refreshed at start-up, so
  channels added since the last start were lost; now it is refreshed whenever channels change. Advice:
  keep an SD card in, and use Settings > Backups > "back up to sd now" before switching firmware.
- The "N new" count (top bar, lock screen, home) follows each chat's notification setting. A muted chat
  adds nothing to it. A chat set to "@mentions only" adds only messages that mention you, and so does
  every channel when Settings > Notifications has "only when @mentioned" on or channel messages off.
  The Messages list still shows each chat's own unread count. Before, every unread message counted.
- Setting the clock by hand (Settings > Clock & time): with a 12-hour clock the time is typed with am or
  pm, for example "2026-10-03 9:15 pm". Before, the only way to enter an afternoon time was 24-hour
  ("21:15"), which still works.
- Settings > region preset: the tick now follows the preset that was picked. USA and Canada use the same
  radio settings, and the tick used to stay on Canada after picking USA. The radio was set correctly
  either way; only the tick was wrong.
- T-Deck: a finger resting on the screen counts as using it, so the screen doesn't dim under it.
- The log records each time the screen dims and how long the device had been idle. If someone says the
  screen dims while they are using it, ask them to send the log right after it happens (Tools > send log
  to the developer) and hand off to the developer.

T-Deck 1.2.4-beta7 (released 2026-10-02)
- Signal bars (see above). Nothing else changed from beta6.

T-Deck 1.2.4-beta6 (released 2026-10-02)
- Sounds no longer skip when the device is busy (the start-up tune had a gap in it).
- Typing: keys pressed while one screen changes to the next are kept (they used to be lost), a save to
  storage no longer starts while a message is being typed, and the freeze once a minute is gone (with
  Wi-Fi on and many contacts, everything stood still for over a second each minute).
- Settings > Display > "animate screen changes": off makes screens change at once. On by default.
- The typing screen's hint said "alt + key for 123". It is the SYM key; the hint now says so.
- SD cards: the T-Deck says when a card is in but can't be read, can format it (Settings > Backups), and
  no longer tries to mount a card that isn't there or can't be read, which could crash it.
- Settings > Theme lists your own themes at the bottom with "remove" beside each.
- The "Update to X?" question only appears on the home screen, and a version you say no to isn't offered
  again. (With "install updates by itself" on, as it is by default, the T-Deck doesn't ask at all.)

T-Deck 1.2.4-beta5 (released 2026-10-01)
- Halloween: the costume and place change every time the screen comes on, and the theme has its own
  screen changes (see "Halloween theme" above).

T-Deck 1.2.4-beta4 (released 2026-10-01)
- The Halloween theme and themes of your own from squatchmesh.com/theme-maker, the same as pager 1.2.7
  (see "Halloween theme" and "Your own themes" above). On the T-Deck you can tap the sasquatch in his
  costume just as in the other themes.

T-Deck 1.2.4-beta3 (released 2026-10-01)
- A flat battery no longer makes it restart over and over. Below 3.3 V it shows "Battery empty", sleeps, and
  starts by itself once a charger has lifted it to 3.5 V (it looks every 3 minutes; a trackball click makes it
  look now). If someone says their T-Deck shows "Battery empty" and won't start: charge it for 15 minutes or
  more. If they are sure it is charged, holding the trackball down while the message shows starts it anyway,
  and sliding the power switch off and on clears the wait - then hand it to the developer, since the reading
  may be wrong on that unit.
- A T-Deck that turned itself off for an empty battery comes back on by itself once charged.
- Problem reports carry the T-Deck's mesh name (see Problem reports above).
- A map tile that fails to download no longer holds the next ones up.

T-Deck 1.2.4-beta2 (released 2026-09-30)
- Battery: the T-Deck has no charger chip, so it works out "plugged in" from the battery voltage. Up to
  beta1 its own screen or Wi-Fi switching off raised the voltage enough to look like a charger: it showed
  charging with nothing plugged in, and the percentage could be far off (one report: 19%, then 55% half
  an hour later, unplugged). Fixed in beta2, and the percentage is closer to the truth. Anyone seeing that
  on an older build should update. It still has no fuel gauge, so the percentage is an estimate.
- No chime or buzz for plugging in when the battery is already full (95% or more).
- The lock face is laid out for the T-Deck's own screen: a big clock in the middle, the date and unread
  count on one line under it, and the scene's character in the middle rather than off to the right.
- Circles and rounded corners (avatars, badges, cards, buttons) have smooth edges instead of jagged ones.

T-Deck 1.2.4-beta1 (released 2026-09-29)
- The talking sasquatch on the lock face (as on the pager), with his new look. On the T-Deck you tap him
  instead of shaking: he hops and says something; three taps quickly and he sees stars. Tapping anywhere
  else on the lock face still says "swipe up to unlock". Settings > Display > "sasquatch talks" turns it off.
- Notifications can be tapped: a message banner opens that conversation (a new-contact banner opens
  People); swipe a banner up to dismiss it. On the lock face a tap never unlocks: it says "swipe up to
  open it", and the swipe that unlocks opens the conversation.
- With no SD card, the map now shows the tiles it downloads over Wi-Fi (kept in memory, not saved).
- The T-Deck has no motion sensor, so the pager's raise to wake, stay on while held and shake reactions
  don't apply to it.
