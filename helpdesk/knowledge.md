These notes add to the website pages. Where they disagree, the website wins.

Where people get help
- This helper on https://squatchmesh.com/help.
- GitHub, for public questions and bug reports: https://github.com/BamBam1121/inw-mesh/issues and
  https://github.com/BamBam1121/inw-mesh/discussions (needs a free GitHub account).
- The developer, by the hand-off from this helper. One volunteer; replies can take a few days.

Finding the firmware version
- On the pager: Settings > System > version.

Things to check before handing off a problem
- Which firmware version, and whether it was installed from squatchmesh.com/install or updated over Wi-Fi.
- What is on the screen (a photo helps the developer; the visitor can mention they have one and the
  developer will ask for it by email).
- Whether it happens every time.

Installing and flashing
- The website installer works in Chrome or Edge on a computer (Web Serial). Safari, Firefox and phones
  can't flash.
- The installer never needs "erase device" ticked. Erasing wipes contacts, channels and messages.
- If no port shows up: try a different USB cable (many are charge-only), a different USB port, and
  follow the "If something goes wrong" section of https://squatchmesh.com/install.
- If a pager won't start after installing, follow "It will not start afterwards" on the install page
  (BOOT/RESET into download mode, then first install again) before trying anything else.

Two different radios - the most likely cause of "radio not responding"
- LilyGo sells the T-Lora Pager with either an SX1262 or an LR1121 radio; they look the same outside.
  Squatch Mesh only drives the SX1262. On an LR1121 pager it installs and boots fine, but the radio never
  comes up: "radio not responding" / "radio down", no messages in or out, NO RF in the status bar.
- ALWAYS check this first when someone says the radio does not work, nothing sends, or nothing is received
  after installing. Ask: "Did you run Wadamesh before, and did its firmware file say sx1262 or lr1121?"
  The file name is the reliable answer. https://squatchmesh.com/install#radio
- If it is an LR1121: Squatch Mesh cannot work on it yet. Say so plainly, apologise for the wasted evening,
  tell them to go back to Wadamesh's lr1121 build for now, and hand off to the developer so they are counted
  as waiting for LR1121 support.
- If it is an SX1262 and the radio still does not come up, hand off to the developer.

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
