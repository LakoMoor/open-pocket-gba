// Runs the menu firmware on the simulated board. Shared by the Studio window and the headless test.
'use strict';
const IPS = 12e6;                                   // FemtoRV32 at 33.75 MHz, about 3 clocks per instruction

class Sim {
  constructor(Board, RV32E, opts) {
    this.board = new Board(opts);
    this.cpu = new RV32E(this.board);
    this.cpu.reset(this.board.boot());
    this.error = null;
  }
  // runs until the virtual clock reaches `target` ms
  runUntil(target) {
    const b = this.board, c = this.cpu;
    let guard = 0;
    while (b.vms < target && guard++ < 2000) {
      if (b.rebootReq) { c.reset(b.boot()); continue; }
      const n = c.run(20000);
      b.advance(n * 1000 / IPS);
      if (c.stalled) {
        if (b.ev) continue;
        if (b.waitUntil > b.vms) b.vms = Math.min(b.waitUntil, target);
        b.advance(0);
      }
    }
  }
}
if (typeof module !== 'undefined') module.exports = { Sim, IPS };
