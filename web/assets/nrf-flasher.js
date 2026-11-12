// The /nrf page's installer: put the board in update mode (1200 baud touch), then flash the picked build over
// Web Serial with nrf-dfu.js. Refuses a build meant for another board (USB vendor ID of the port).
import { loadDfuZip, NrfDfu, touchReset, webSerialIo } from './nrf-dfu.js?v=1';

const BUILDS = {
  seed: { name: 'nRF Seed (XIAO nRF52840)', zip: '/nrf-fw/squatch-nrf-seed-xiao-nrf52840-1.18-filter-easysky.zip',
    vendors: [0x2886] },
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

function checkBoard(port, b) {
  const v = vendorOf(port);
  if (v !== undefined && !b.vendors.includes(v)) {
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
    if (e && e.name === 'NotFoundError') log('No port picked.');
    else log('Stopped: ' + (e && e.message ? e.message : e));
  } finally {
    busy = false;
    $('nrf-touch').disabled = $('nrf-flash').disabled = false;
  }
}

$('nrf-touch').addEventListener('click', () => run(async () => {
  const b = build();
  const port = await navigator.serial.requestPort({ filters: FILTERS });
  checkBoard(port, b);
  log('Restarting the board into update mode...');
  await touchReset(port);
  log('Done. Wait two seconds, then press "Pick the update port and install" and pick the new port.');
}));

$('nrf-flash').addEventListener('click', () => run(async () => {
  const b = build();
  bar.style.width = '0';
  log('Loading ' + b.name + '...');
  const res = await fetch(b.zip, { cache: 'no-cache' });
  if (!res.ok) throw new Error('could not load the firmware (' + res.status + ')');
  const img = await loadDfuZip(await res.arrayBuffer());
  const port = await navigator.serial.requestPort({ filters: FILTERS });
  checkBoard(port, b);
  await port.open({ baudRate: 115200 });
  const io = webSerialIo(port);
  try {
    await new NrfDfu(io, log).flash(img, f => { bar.style.width = Math.round(f * 100) + '%'; });
  } finally {
    await io.close();
    try { await port.close(); } catch (e) { /* the board restarted */ }
  }
  log("Installed " + b.name + ". The board restarts by itself. Next: step 3, set it up.");
}));
