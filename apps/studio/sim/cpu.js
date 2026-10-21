// RV32E interpreter (the menu CPU is a FemtoRV32: RV32I base, 16 registers, no M, no CSR).
// bus: { load(addr, size, signed) -> number | undefined (stall), store(addr, size, value) }
// A load that returns undefined stalls: the instruction is retried on the next step, like
// the real CPU waiting on REG_WAIT.
'use strict';

class RV32E {
  constructor(bus) {
    this.bus = bus;
    this.x = new Int32Array(16);
    this.pc = 0;
    this.stalled = false;
    this.count = 0;
  }

  reset(pc) {
    this.x.fill(0);
    this.pc = pc >>> 0;
    this.stalled = false;
  }

  // runs up to n instructions; stops early on a stall
  run(n) {
    const x = this.x, bus = this.bus;
    let pc = this.pc;
    this.stalled = false;
    for (let i = 0; i < n; i++) {
      const ins = bus.fetch(pc);
      const op = ins & 0x7f, rd = (ins >>> 7) & 15, f3 = (ins >>> 12) & 7;
      const rs1 = (ins >>> 15) & 15, rs2 = (ins >>> 20) & 15;
      let next = (pc + 4) >>> 0;
      switch (op) {
        case 0x37: if (rd) x[rd] = ins & 0xfffff000; break;                         // LUI
        case 0x17: if (rd) x[rd] = (pc + (ins & 0xfffff000)) | 0; break;            // AUIPC
        case 0x6f: {                                                                // JAL
          const imm = ((ins >> 31) << 20) | (((ins >>> 12) & 0xff) << 12) | (((ins >>> 20) & 1) << 11) | (((ins >>> 21) & 0x3ff) << 1);
          if (rd) x[rd] = next;
          next = (pc + imm) >>> 0;
          break;
        }
        case 0x67: {                                                                // JALR
          const t = ((x[rs1] + (ins >> 20)) & ~1) >>> 0;
          if (rd) x[rd] = next;
          next = t;
          break;
        }
        case 0x63: {                                                                // branches
          const a = x[rs1], b = x[rs2];
          let take;
          switch (f3) {
            case 0: take = a === b; break;
            case 1: take = a !== b; break;
            case 4: take = a < b; break;
            case 5: take = a >= b; break;
            case 6: take = (a >>> 0) < (b >>> 0); break;
            default: take = (a >>> 0) >= (b >>> 0); break;
          }
          if (take) {
            const imm = ((ins >> 31) << 12) | (((ins >>> 7) & 1) << 11) | (((ins >>> 25) & 0x3f) << 5) | (((ins >>> 8) & 15) << 1);
            next = (pc + imm) >>> 0;
          }
          break;
        }
        case 0x03: {                                                                // loads
          const a = (x[rs1] + (ins >> 20)) >>> 0;
          let v;
          switch (f3) {
            case 0: v = bus.load(a, 1, true); break;
            case 1: v = bus.load(a, 2, true); break;
            case 2: v = bus.load(a, 4, false); break;
            case 4: v = bus.load(a, 1, false); break;
            default: v = bus.load(a, 2, false); break;
          }
          if (v === undefined) { this.pc = pc; this.stalled = true; this.count += i; return i; }
          if (rd) x[rd] = v | 0;
          break;
        }
        case 0x23: {                                                                // stores
          const a = (x[rs1] + (((ins >> 25) << 5) | ((ins >>> 7) & 31))) >>> 0;
          bus.store(a, f3 === 0 ? 1 : f3 === 1 ? 2 : 4, x[rs2]);
          break;
        }
        case 0x13: {                                                                // OP-IMM
          const imm = ins >> 20, a = x[rs1];
          let v;
          switch (f3) {
            case 0: v = a + imm; break;
            case 1: v = a << (imm & 31); break;
            case 2: v = a < imm ? 1 : 0; break;
            case 3: v = (a >>> 0) < (imm >>> 0) ? 1 : 0; break;
            case 4: v = a ^ imm; break;
            case 5: v = (ins & 0x40000000) ? a >> (imm & 31) : a >>> (imm & 31); break;
            case 6: v = a | imm; break;
            default: v = a & imm; break;
          }
          if (rd) x[rd] = v;
          break;
        }
        case 0x33: {                                                                // OP
          const a = x[rs1], b = x[rs2];
          let v;
          switch (f3) {
            case 0: v = (ins & 0x40000000) ? a - b : a + b; break;
            case 1: v = a << (b & 31); break;
            case 2: v = a < b ? 1 : 0; break;
            case 3: v = (a >>> 0) < (b >>> 0) ? 1 : 0; break;
            case 4: v = a ^ b; break;
            case 5: v = (ins & 0x40000000) ? a >> (b & 31) : a >>> (b & 31); break;
            case 6: v = a | b; break;
            default: v = a & b; break;
          }
          if (rd) x[rd] = v;
          break;
        }
        case 0x0f: case 0x73: break;                                                // FENCE, SYSTEM: nothing to do
        default:
          throw new Error(`illegal instruction ${(ins >>> 0).toString(16)} at ${pc.toString(16)}`);
      }
      pc = next;
    }
    this.pc = pc;
    this.count += n;
    return n;
  }
}

if (typeof module !== 'undefined') module.exports = { RV32E };
