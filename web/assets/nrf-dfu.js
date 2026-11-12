// Squatch Mesh: nRF52 firmware update over USB serial, in the browser (Web Serial).
// Speaks the legacy serial DFU of the Adafruit nRF52 bootloader (RAK, Seeed XIAO nRF52840 and most MeshCore nRF52
// boards): SLIP-framed HCI packets with a CRC-16, each acknowledged by the board. Input: the firmware .zip that
// MeshCore's build makes (manifest.json, the application .bin and its .dat init packet).
// Transport-agnostic core (NrfDfu) so the same code is tested against a real board outside the browser.

const DFU_INIT_PACKET = 1, DFU_START_PACKET = 3, DFU_DATA_PACKET = 4, DFU_STOP_DATA_PACKET = 5;
const MODE_APPLICATION = 4;
const DATA_PACKET_SIZE = 512;
const FLASH_PAGE_SIZE = 4096;
const FLASH_PAGE_ERASE_MS = 89.7;
const FLASH_PAGE_WRITE_MS = (FLASH_PAGE_SIZE / 4) * 0.1;	// 102.4 ms
const ACK_TIMEOUT_MS = 1000;
const RETRIES = 3;

const sleep = ms => new Promise(r => setTimeout(r, ms));

function crc16(data, crc = 0xffff) {
  for (const b of data) {
    crc = ((crc >> 8) & 0x00ff) | ((crc << 8) & 0xff00);
    crc ^= b;
    crc ^= (crc & 0x00ff) >> 4;
    crc ^= (crc << 8) << 4;
    crc ^= ((crc & 0x00ff) << 4) << 1;
    crc &= 0xffff;
  }
  return crc;
}

function u32(v) { return [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff]; }

// One HCI packet: 4-byte header (sequence, ack, integrity + reliable flags, vendor type 14, length, header check),
// payload, CRC-16, all SLIP-escaped between two 0xC0
function hciPacket(seq, payload) {
  const len = payload.length;
  const h0 = seq | (((seq + 1) % 8) << 3) | (1 << 6) | (1 << 7);
  const h1 = 14 | ((len & 0x000f) << 4);
  const h2 = (len & 0x0ff0) >> 4;
  const h3 = (~(h0 + h1 + h2) + 1) & 0xff;
  const raw = [h0, h1, h2, h3, ...payload];
  const crc = crc16(raw);
  raw.push(crc & 0xff, (crc >> 8) & 0xff);
  const out = [0xc0];
  for (const b of raw) {
    if (b === 0xc0) out.push(0xdb, 0xdc);
    else if (b === 0xdb) out.push(0xdb, 0xdd);
    else out.push(b);
  }
  out.push(0xc0);
  return new Uint8Array(out);
}

function slipDecode(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] === 0xdb && i + 1 < bytes.length) {
      out.push(bytes[i + 1] === 0xdc ? 0xc0 : 0xdb);
      i++;
    } else {
      out.push(bytes[i]);
    }
  }
  return out;
}

// --- The .zip (stored or deflated entries; DecompressionStream does the inflate) ---
export async function readZip(buf) {
  const u8 = new Uint8Array(buf);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  let eocd = -1;
  for (let i = u8.length - 22; i >= Math.max(0, u8.length - 65557); i--) {
    if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error('not a zip file');
  const count = dv.getUint16(eocd + 10, true);
  let p = dv.getUint32(eocd + 16, true);
  const files = {};
  for (let n = 0; n < count; n++) {
    if (dv.getUint32(p, true) !== 0x02014b50) throw new Error('bad zip directory');
    const method = dv.getUint16(p + 10, true);
    const csize = dv.getUint32(p + 20, true);
    const nlen = dv.getUint16(p + 28, true), xlen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true);
    const loc = dv.getUint32(p + 42, true);
    const name = new TextDecoder().decode(u8.subarray(p + 46, p + 46 + nlen));
    const lnlen = dv.getUint16(loc + 26, true), lxlen = dv.getUint16(loc + 28, true);
    const data = u8.subarray(loc + 30 + lnlen + lxlen, loc + 30 + lnlen + lxlen + csize);
    if (method === 0) {
      files[name] = new Uint8Array(data);
    } else if (method === 8) {
      const ds = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
      files[name] = new Uint8Array(await new Response(ds).arrayBuffer());
    } else {
      throw new Error('zip entry ' + name + ' uses an unsupported compression');
    }
    p += 46 + nlen + xlen + clen;
  }
  return files;
}

// The application image and its init packet from a MeshCore DFU .zip
export async function loadDfuZip(buf) {
  const files = await readZip(buf);
  if (!files['manifest.json']) throw new Error('no manifest.json: not an nRF52 update .zip');
  const man = JSON.parse(new TextDecoder().decode(files['manifest.json'])).manifest;
  const app = man && man.application;
  if (!app) throw new Error('this .zip holds no application (bootloader or SoftDevice packages are not supported here)');
  const bin = files[app.bin_file], dat = files[app.dat_file];
  if (!bin || !dat) throw new Error('the .zip is missing ' + (!bin ? app.bin_file : app.dat_file));
  return { bin, dat };
}

// --- The protocol, over any byte transport: io = { write(Uint8Array), read(): Promise<Uint8Array|null> } ---
export class NrfDfu {
  constructor(io, log = () => {}) {
    this.io = io;
    this.log = log;
    this.seq = 0;
    this.rx = [];
  }

  // Bytes until a whole SLIP frame (between two 0xC0), or null at the timeout
  async readFrame(timeoutMs) {
    const end = Date.now() + timeoutMs;
    for (;;) {
      const s = this.rx.indexOf(0xc0);
      if (s >= 0) {
        const e = this.rx.indexOf(0xc0, s + 1);
        if (e > s + 1) {
          const frame = this.rx.slice(s + 1, e);
          this.rx = this.rx.slice(e + 1);
          return slipDecode(frame);
        }
        if (e === s + 1) { this.rx = this.rx.slice(e); continue; }	// two 0xC0 in a row: the second starts a frame
      }
      const left = end - Date.now();
      if (left <= 0) return null;
      const chunk = await this.io.read(left);
      if (chunk && chunk.length) this.rx.push(...chunk);
    }
  }

  async send(payload) {
    this.seq = (this.seq + 1) % 8;
    const pkt = hciPacket(this.seq, payload);
    const want = (this.seq + 1) % 8;
    for (let attempt = 0; attempt < RETRIES; attempt++) {
      await this.io.write(pkt);
      const deadline = Date.now() + ACK_TIMEOUT_MS;
      while (Date.now() < deadline) {
        const f = await this.readFrame(deadline - Date.now());
        if (!f) break;
        if (f.length >= 1 && ((f[0] >> 3) & 0x07) === want) return;
      }
    }
    throw new Error('the board did not answer on this port. It must be in update mode: use the first button, then pick the NEW port, or double-tap its reset button.');
  }

  // progress(fraction 0..1)
  async flash({ bin, dat }, progress = () => {}) {
    this.rx = [];
    this.log('Starting the update (' + bin.length + ' bytes)');
    await this.send([...u32(DFU_START_PACKET), ...u32(MODE_APPLICATION), ...u32(0), ...u32(0), ...u32(bin.length)]);
    const eraseMs = Math.max(500, (Math.floor(bin.length / FLASH_PAGE_SIZE) + 1) * FLASH_PAGE_ERASE_MS);
    this.log('Erasing');
    await sleep(eraseMs);
    await this.send([...u32(DFU_INIT_PACKET), ...dat, 0, 0]);
    const n = Math.ceil(bin.length / DATA_PACKET_SIZE);
    for (let i = 0; i < n; i++) {
      const part = bin.subarray(i * DATA_PACKET_SIZE, (i + 1) * DATA_PACKET_SIZE);
      await this.send([...u32(DFU_DATA_PACKET), ...part]);
      if (i % 8 === 0) await sleep(FLASH_PAGE_WRITE_MS);
      progress((i + 1) / n);
    }
    await sleep(FLASH_PAGE_WRITE_MS);
    this.log('Checking and starting the new firmware');
    await this.send([...u32(DFU_STOP_DATA_PACKET)]);
    this.log('Done');
  }
}

// --- Web Serial glue ---
// The running firmware drops into its bootloader when the port is opened at 1200 baud and closed ("1200 touch")
export async function touchReset(port) {
  await port.open({ baudRate: 1200 });
  await sleep(100);
  await port.close();
}

export function webSerialIo(port) {
  const reader = port.readable.getReader();
  const writer = port.writable.getWriter();
  let pending = null;
  return {
    async write(bytes) { await writer.write(bytes); },
    async read(timeoutMs) {
      if (!pending) pending = reader.read();
      const t = new Promise(r => setTimeout(() => r('timeout'), timeoutMs));
      const res = await Promise.race([pending, t]);
      if (res === 'timeout') return null;
      pending = null;
      return res.value || null;
    },
    async close() {
      try { await reader.cancel(); } catch (e) { /* closing anyway */ }
      reader.releaseLock();
      writer.releaseLock();
    },
  };
}
