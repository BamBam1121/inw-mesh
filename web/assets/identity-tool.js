// /identity: put a saved private key (a config.meshcore.io export, or the 128 hex characters) on a MeshCore repeater
// over USB, reboot it, and check the public key it then reports. The key is read in the browser and sent only to
// the board; nothing goes to the site.
const $ = id => document.getElementById(id);
const out = $('id-log');
const log = m => { out.textContent += m + '\n'; out.scrollTop = out.scrollHeight; };
const sleep = ms => new Promise(r => setTimeout(r, ms));
let key = '';

function hex128(s) {
  const k = (s || '').trim().toLowerCase();
  return /^[0-9a-f]{128}$/.test(k) ? k : '';
}

function setKey(k, from) {
  key = k;
  $('id-key-state').textContent = k ? 'Key loaded from ' + from + '.' : 'No private key found in ' + from + '.';
  $('id-go').disabled = !k;
}

$('id-file').addEventListener('change', async e => {
  const f = e.target.files[0];
  if (!f) return;
  try {
    const data = JSON.parse(await f.text());
    setKey(hex128(data && data.vars && data.vars['prv.key']), f.name);
  } catch (err) {
    setKey('', f.name + ' (not a config export)');
  }
});
$('id-paste').addEventListener('input', e => {
  const k = hex128(e.target.value);
  if (k) setKey(k, 'the box');
  else if (e.target.value.trim()) $('id-key-state').textContent = 'A private key is 128 characters, 0-9 and a-f.';
});

// One line out, the reply lines back ("  -> ..."), on an open port
class Cli {
  constructor(port) {
    this.port = port;
    this.buf = '';
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    this.pump();
  }
  async pump() {
    const dec = new TextDecoder();
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.buf += dec.decode(value);
      }
    } catch (e) { /* the board rebooted */ }
  }
  async cmd(line, waitMs = 1500) {
    this.buf = '';
    await this.writer.write(new TextEncoder().encode(line + '\r\n'));
    const end = Date.now() + waitMs;
    while (Date.now() < end && !/->[^\n]*\n/.test(this.buf)) await sleep(50);
    const m = this.buf.match(/->\s*>?\s*([^\r\n]*)/);
    return m ? m[1].trim() : '';
  }
  async close() {
    try { await this.reader.cancel(); } catch (e) { /* closing */ }
    try { this.reader.releaseLock(); this.writer.releaseLock(); } catch (e) { /* closing */ }
    try { await this.port.close(); } catch (e) { /* closing */ }
  }
}

async function openCli(port) {
  try {
    await port.open({ baudRate: 115200 });
  } catch (e) {
    throw new Error('the port is busy: close config.meshcore.io, the MeshCore app and other tabs, then try again');
  }
  await sleep(300);
  return new Cli(port);
}

$('id-go').addEventListener('click', async () => {
  if (!('serial' in navigator)) { log('Use Chrome or Edge on a computer.'); return; }
  if (!key) return;
  $('id-go').disabled = true;
  out.textContent = '';
  let cli;
  try {
    const port = await navigator.serial.requestPort();
    cli = await openCli(port);
    const before = await cli.cmd('get public.key');
    if (!/^[0-9a-f]{64}$/i.test(before)) throw new Error('no MeshCore repeater answered on that port (got "' + before + '")');
    log('Board now: ' + before.slice(0, 6) + '...');
    const r = await cli.cmd('set prv.key ' + key, 2500);
    log('Set key: ' + (r || 'no answer'));
    log('Rebooting...');
    await cli.cmd('reboot', 500);
    await cli.close();
    cli = null;
    let after = '';
    for (let t = 0; t < 30 && !after; t++) {	// up to ~15 s for it to come back
      await sleep(500);
      const ports = await navigator.serial.getPorts();
      const p = ports.find(x => x === port) || ports[0];
      if (!p) continue;
      try {
        cli = await openCli(p);
        after = await cli.cmd('get public.key');
        if (!/^[0-9a-f]{64}$/i.test(after)) { after = ''; await cli.close(); cli = null; }
      } catch (e) { cli = null; }
    }
    if (!after) throw new Error('the board didn\'t come back on USB. Unplug it, plug it back in, then check with "get public.key".');
    log('Board after reboot: ' + after.slice(0, 6) + '...');
    log(after.toLowerCase() === before.toLowerCase()
      ? 'Its identity did not change. If the key you loaded is another board\'s, the board did not take it.'
      : 'Done: this board now answers as ' + after.slice(0, 6) + '. Make sure no other board on the air has the same key.');
  } catch (e) {
    if (e && e.name === 'NotFoundError') log('No port picked.');
    else log('Stopped: ' + (e && e.message ? e.message : e));
  } finally {
    if (cli) await cli.close();
    $('id-go').disabled = !key;
  }
});
