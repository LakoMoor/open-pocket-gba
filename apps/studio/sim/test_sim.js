// headless check: boot the real firmware, open the menu, print the log and the LCD text
const fs = require('fs'), path = require('path');
const { RV32E } = require('./cpu'), { Board, K } = require('./board'), { Sim } = require('./sim'), { buildImage } = require('./fat');
const root = path.join(__dirname, '..', '..', '..');
const flash = new Uint8Array(8 << 20).fill(0xff);
flash.set(fs.readFileSync(path.join(root, 'fw', 'build', 'fw.bin')), 0x100000);
const games = path.join(root, 'build', 'games.bin');
if (fs.existsSync(games)) flash.set(fs.readFileSync(games), 0x200000);
const sdDir = path.join(root, 'games');
let log = '';
const sim = new Sim(Board, RV32E, { flash, sd: buildImage(sdDir, 64), onUart: c => { log += c; } });
const t = Date.now();
sim.runUntil(6000);
console.log('--- log after 6 s virtual (' + (Date.now() - t) + ' ms real):\n' + log);
const lcd = sim.board.lcd;
for (let y = 0; y < 12; y++) { let s = ''; for (let x = 0; x < 16; x++) { const c = lcd[y * 16 + x] & 0x7f; s += c >= 32 && c < 127 ? String.fromCharCode(c) : '.'; } console.log('|' + s + '|'); }
