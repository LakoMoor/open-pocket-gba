// Builds a FAT16 SD card image from a folder (sub-folders and long names included), and reads
// files back out of an image. Used by the simulator; FatFs in the firmware mounts the result.
'use strict';
const fs = require('fs');
const path = require('path');

const SEC = 512, SPC = 4, CLUS = SEC * SPC;           // 2 KB clusters
const RESERVED = 4, NFATS = 2, ROOT_ENTRIES = 512;

function shortName(name, used) {
  const up = name.toUpperCase();
  const dot = up.lastIndexOf('.');
  let base = (dot > 0 ? up.slice(0, dot) : up).replace(/[^A-Z0-9_$~!#%&\-{}()@'`^]/g, '');
  let ext = (dot > 0 ? up.slice(dot + 1) : '').replace(/[^A-Z0-9_]/g, '').slice(0, 3);
  const fits = base.length <= 8 && base === (dot > 0 ? up.slice(0, dot) : up) && ext === (dot > 0 ? up.slice(dot + 1) : '') && ext.length <= 3;
  let sn = (base.slice(0, 8).padEnd(8) + ext.padEnd(3));
  if (!fits || used.has(sn)) {
    for (let n = 1; ; n++) {
      const tail = '~' + n;
      sn = (base.slice(0, 8 - tail.length) + tail).padEnd(8) + ext.padEnd(3);
      if (!used.has(sn)) break;
    }
  }
  used.add(sn);
  return { sn, lfn: !fits };
}

function lfnChecksum(sn) {
  let s = 0;
  for (let i = 0; i < 11; i++) s = (((s & 1) << 7) | (s >> 1)) + sn.charCodeAt(i) & 0xff;
  return s;
}

// directory entries (32 bytes each) for a list of {name, isDir, cluster, size}
function dirEntries(items, self, parent) {
  const out = [];
  const used = new Set();
  const plain = (sn, attr, cl, size) => {
    const e = Buffer.alloc(32);
    e.write(sn, 0, 11, 'latin1');
    e[11] = attr;
    e.writeUInt16LE(0x5000, 14); e.writeUInt16LE(0x5a21, 16);   // a fixed date: 2025-01-01
    e.writeUInt16LE(0x5000, 22); e.writeUInt16LE(0x5a21, 24);
    e.writeUInt16LE(cl & 0xffff, 26);
    e.writeUInt32LE(size >>> 0, 28);
    return e;
  };
  if (self !== undefined) {
    out.push(plain('.          ', 0x10, self, 0));
    out.push(plain('..         ', 0x10, parent || 0, 0));
  }
  for (const it of items) {
    const { sn, lfn } = shortName(it.name, used);
    if (lfn) {
      const chars = [...it.name].map(c => c.charCodeAt(0));
      chars.push(0);
      while (chars.length % 13) chars.push(0xffff);
      const parts = chars.length / 13, sum = lfnChecksum(sn);
      for (let p = parts; p >= 1; p--) {
        const e = Buffer.alloc(32);
        e[0] = p | (p === parts ? 0x40 : 0);
        e[11] = 0x0f; e[13] = sum;
        const seg = chars.slice((p - 1) * 13, p * 13);
        const pos = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30];
        seg.forEach((c, i) => e.writeUInt16LE(c, pos[i]));
        out.push(e);
      }
    }
    out.push(plain(sn, it.isDir ? 0x10 : 0x20, it.cluster, it.isDir ? 0 : it.size));
  }
  return Buffer.concat(out);
}

function buildImage(folder, sizeMB = 64) {
  const total = sizeMB * 2048;                               // sectors
  const clusters = Math.floor((total - RESERVED - 32) / SPC);
  const fatSecs = Math.ceil((clusters + 2) * 2 / SEC);
  const rootSecs = ROOT_ENTRIES * 32 / SEC;
  const dataStart = RESERVED + NFATS * fatSecs + rootSecs;
  const img = new Uint8Array(total * SEC);
  const fat = new Uint16Array(clusters + 2);
  fat[0] = 0xfff8; fat[1] = 0xffff;
  let next = 2;
  const alloc = bytes => {
    const n = Math.max(1, Math.ceil(bytes / CLUS)), first = next;
    for (let i = 0; i < n; i++) fat[next + i] = i === n - 1 ? 0xffff : next + i + 1;
    next += n;
    return first;
  };
  const put = (cl, data) => img.set(data, (dataStart + (cl - 2) * SPC) * SEC);

  function walk(dir, self, parent) {
    const items = [];
    if (dir && fs.existsSync(dir)) {
      for (const name of fs.readdirSync(dir).sort()) {
        const p = path.join(dir, name), st = fs.statSync(p);
        if (st.isDirectory()) items.push({ name, isDir: true, path: p });
        else if (st.size < 32 << 20) items.push({ name, isDir: false, path: p, size: st.size });
      }
    }
    for (const it of items) if (!it.isDir) { const d = fs.readFileSync(it.path); it.cluster = d.length ? alloc(d.length) : 0; if (d.length) put(it.cluster, d); }
    for (const it of items) if (it.isDir) it.cluster = alloc(CLUS * 4);       // room for 256 entries
    for (const it of items) if (it.isDir) walk(it.path, it.cluster, self);
    const ents = dirEntries(items, self, parent);
    if (self === undefined) img.set(ents.subarray(0, ROOT_ENTRIES * 32), (RESERVED + NFATS * fatSecs) * SEC);
    else put(self, ents.subarray(0, CLUS * 4));
  }
  walk(folder, undefined, 0);

  // boot sector
  const b = Buffer.alloc(SEC);
  b.set([0xeb, 0x3c, 0x90]); b.write('OPENPOCK', 3, 'latin1');
  b.writeUInt16LE(SEC, 11); b[13] = SPC; b.writeUInt16LE(RESERVED, 14); b[16] = NFATS;
  b.writeUInt16LE(ROOT_ENTRIES, 17); b.writeUInt16LE(total < 65536 ? total : 0, 19); b[21] = 0xf8;
  b.writeUInt16LE(fatSecs, 22); b.writeUInt16LE(63, 24); b.writeUInt16LE(255, 26);
  b.writeUInt32LE(total >= 65536 ? total : 0, 32); b[36] = 0x80; b[38] = 0x29; b.writeUInt32LE(0x0b0e0a51, 39);
  b.write('OPENPOCKET ', 43, 'latin1'); b.write('FAT16   ', 54, 'latin1'); b[510] = 0x55; b[511] = 0xaa;
  img.set(b, 0);
  const fb = Buffer.from(fat.buffer);
  for (let i = 0; i < NFATS; i++) img.set(fb, (RESERVED + i * fatSecs) * SEC);
  return img;
}

// files of the image as {path: Buffer} (root and sub-folders; long names restored)
function readImage(img) {
  const b = Buffer.from(img.buffer, img.byteOffset, img.length);
  const spc = b[13], res = b.readUInt16LE(14), nf = b[16], roots = b.readUInt16LE(17), fsz = b.readUInt16LE(22);
  const dataStart = res + nf * fsz + roots * 32 / SEC, fat0 = res * SEC;
  const chain = cl => { const out = []; while (cl >= 2 && cl < 0xfff8 && out.length < 65536) { out.push(cl); cl = b.readUInt16LE(fat0 + cl * 2); } return out; };
  const clData = cls => Buffer.concat(cls.map(c => b.subarray((dataStart + (c - 2) * spc) * SEC, (dataStart + (c - 1) * spc) * SEC)));
  const files = {};
  function dir(buf, prefix) {
    let lfn = '';
    for (let o = 0; o + 32 <= buf.length; o += 32) {
      const e = buf.subarray(o, o + 32);
      if (e[0] === 0) break;
      if (e[0] === 0xe5) { lfn = ''; continue; }
      if (e[11] === 0x0f) {
        const pos = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30];
        let s = '';
        for (const p of pos) { const c = e.readUInt16LE(p); if (c === 0 || c === 0xffff) break; s += String.fromCharCode(c); }
        lfn = s + lfn;
        continue;
      }
      const sn = e.toString('latin1', 0, 8).trim(), ext = e.toString('latin1', 8, 11).trim();
      const name = lfn || (ext ? sn + '.' + ext : sn);
      lfn = '';
      if (sn === '.' || sn === '..' || (e[11] & 0x08)) continue;
      const cl = e.readUInt16LE(26);
      if (e[11] & 0x10) dir(clData(chain(cl)), prefix + name + '/');
      else files[prefix + name] = clData(chain(cl)).subarray(0, e.readUInt32LE(28));
    }
  }
  dir(b.subarray((res + nf * fsz) * SEC, (res + nf * fsz) * SEC + roots * 32), '');
  return files;
}

module.exports = { buildImage, readImage };
