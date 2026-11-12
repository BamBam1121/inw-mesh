// The /nrf page's installer: put the board in update mode (1200 baud touch), then flash the picked build over
// Web Serial with nrf-dfu.js. Refuses a build meant for another board (USB vendor ID of the port).
import { loadDfuZip, NrfDfu, touchReset, webSerialIo } from './nrf-dfu.js?v=2';

const BUILDS = {
  seed: { name: 'nRF Seed (XIAO nRF52840)', zip: '/nrf-fw/squatch-nrf-seed-xiao-nrf52840-1.18-filter-easysky.zip',
    vendors: [0x2886] },
  photon: { name: 'Photon PowerSaving (MeshSmith Photon nRF52)',
    zip: '/nrf-fw/squatch-photon-nrf52-1.18-filter-easysky.zip', vendors: [0x2886] },
  rak: { name: 'RAK 1W PowerSaving (RAK3401)', zip: '/rak1w-fw/squatch-repeater-rak3401-1w-1.18-filter-easysky.zip',
    vendors: [0x239a] },
};
// Seeed boards and Adafruit-bootloader boards (RAK) on USB
const FILTERS = [{ usbVendorId: 0x2886 }, { usbVendorId: 0x239a }];

const $ = id => document.getElementById(id);
const logEl = $('nrf-log'), bar = $('nrf-bar');
const log = m => { logEl.textContent = m; };
const build = () => BUILDS[document.querySelector('input[name="build"]:checked').value];
let busy = false;

function vendorOf(port) {
  const i = port.getInfo ? port.getInfo() : {};
  return i.usbVendorId;
}

// "Show every USB port": no vendor filter (a board on other firmware can have another USB ID)
const pickPort = () => navigator.serial.requestPort($('nrf-all') && $('nrf-all').checked ? {} : { filters: FILTERS });
const KNOWN = [0x2886, 0x239a];

// Refuse only a port that is clearly the other kind of board; an unknown USB ID is let through
function checkBoard(port, b) {
  const v = vendorOf(port);
  if (v !== undefined && KNOWN.includes(v) && !b.vendors.includes(v)) {
    throw new Error('This port belongs to a different board than ' + b.name + '. Pick the other build above, or the right port.');
  }
}

async function run(fn) {
  if (busy) return;
  if (!('serial' in navigator)) {
    log('This browser can\'t talk to USB devices. Use Chrome or Edge on a computer, or flash the file yourself (below).');
    return;
  }
  busy = true;
  $('nrf-touch').disabled = $('nrf-flash').disabled = true;
  try {
    await fn();
  } catch (e) {
    if (e && e.name === 'NotFoundError') {
      log('No port picked. If the list was empty: tick "Show every USB port" and try again, check the cable carries data ' +
        '(some are charge-only), or double-tap the board\'s reset button.');
    } else {
      log('Stopped: ' + (e && e.message ? e.message : e));
    }
  } finally {
    busy = false;
    $('nrf-touch').disabled = $('nrf-flash').disabled = false;
  }
}

// Opening fails when something else has the port (the MeshCore app, config.meshcore.io, another tab, a serial
// monitor): say so plainly instead of the browser's "Failed to open serial port"
async function openOrExplain(open) {
  try {
    await open();
  } catch (e) {
    if (e && (e.name === 'NetworkError' || e.name === 'InvalidStateError')) {
      throw new Error('the port is busy. Close anything else using the board (the MeshCore app, config.meshcore.io, ' +
        'other tabs, a serial monitor), unplug it and plug it back in, then try again.');
    }
    throw e;
  }
}

$('nrf-touch').addEventListener('click', () => run(async () => {
  const b = build();
  const port = await pickPort();
  checkBoard(port, b);
  log('Restarting the board into update mode...');
  await openOrExplain(() => touchReset(port));
  log('Done. Wait two seconds, then press "Pick the update port and install" and pick the new port.');
}));

$('nrf-flash').addEventListener('click', () => run(async () => {
  const b = build();
  bar.style.width = '0';
  // The port first: Chrome shows its chooser only right after the click, and a slow download used that time up
  const port = await pickPort();
  checkBoard(port, b);
  log('Loading ' + b.name + '...');
  const res = await fetch(b.zip, { cache: 'no-cache' });
  if (!res.ok) throw new Error('could not load the firmware (' + res.status + ')');
  const img = await loadDfuZip(await res.arrayBuffer());
  await openOrExplain(() => port.open({ baudRate: 115200 }));
  const io = webSerialIo(port);
  try {
    await new NrfDfu(io, log).flash(img, f => { bar.style.width = Math.round(f * 100) + '%'; });
  } finally {
    await io.close();
    try { await port.close(); } catch (e) { /* the board restarted */ }
  }
  log("Installed " + b.name + ". The board restarts by itself. Next: step 3, set it up.");
}));
