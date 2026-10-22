/* R3000A (MIPS I) interpreter: integer unit, COP0 exceptions and
 * interrupts, load and branch delay slots, the multiply/divide unit, and
 * coprocessor dispatch (COP2 = GTE, see gte.c). */
#include <string.h>

#include "ps1.h"

/* GTE (gte.c) */
uint32_t gte_read_data(PS1 *p, uint32_t r);
void gte_write_data(PS1 *p, uint32_t r, uint32_t v);
uint32_t gte_read_ctrl(PS1 *p, uint32_t r);
void gte_write_ctrl(PS1 *p, uint32_t r, uint32_t v);
void gte_command(PS1 *p, uint32_t op);
void hle_bios_call(PS1 *p, uint32_t vector);

static inline int32_t sext16(uint32_t v) { return (int32_t)(int16_t)v; }

/* ------------------------------------------------------------------ */
/* Exceptions                                                           */

static void exception(PS1 *p, uint32_t code, uint32_t cop) {
  Cpu *c = &p->cpu;
  uint32_t sr = c->cop0[C0_SR];
  /* Push the interrupt-enable / user-mode pairs (bits 0-5) two places. */
  c->cop0[C0_SR] = (sr & ~0x3Fu) | ((sr << 2) & 0x3Fu);
  uint32_t cause = c->cop0[C0_CAUSE] & ~(0x7Cu | 0x30000000u | 0xC0000000u);
  cause |= (code & 31) << 2 | (cop & 3) << 28;
  if (c->cur_delay) {
    c->cop0[C0_EPC] = c->cur_pc - 4;
    cause |= 0x80000000u;                       /* BD */
    if (c->branch_taken) cause |= 0x40000000u;  /* BT: the branch was taken */
    c->cop0[C0_TAR] = c->pc;                    /* where the branch was going */
  } else {
    c->cop0[C0_EPC] = c->cur_pc;
  }
  c->cop0[C0_CAUSE] = cause;
  c->pc = (sr & SR_BEV) ? 0xBFC00180u : 0x80000080u;
  c->next_pc = c->pc + 4;
  c->in_delay = false;
  c->branch_taken = false;
}

static void address_error(PS1 *p, uint32_t va, bool store) {
  p->cpu.cop0[C0_BADVADDR] = va;
  exception(p, store ? EXC_ADES : EXC_ADEL, 0);
}

/* ------------------------------------------------------------------ */
/* Memory through the segments                                          */

/* KUSEG and KSEG0/1 see the same 512 MB of physical space; KSEG2 only has
 * the cache control register. Scratchpad isn't reachable through KSEG1. */
static inline bool to_phys(uint32_t va, uint32_t *pa) {
  if (va < 0x80000000u) {
    if (va >= 0x20000000u) return false;
    *pa = va;
  } else if (va < 0xC0000000u) {
    *pa = va & 0x1FFFFFFFu;
    if (va >= 0xA0000000u && *pa >= 0x1F800000u && *pa < 0x1F800400u) return false;
  } else {
    *pa = va;   /* KSEG2: bus.c knows 0xFFFE0130 */
  }
  return true;
}

static bool load(PS1 *p, uint32_t va, int size, uint32_t *v) {
  if (va & (uint32_t)(size - 1)) { address_error(p, va, false); return false; }
  uint32_t pa;
  if (to_phys(va, &pa) && pa < 0x00800000u) {   /* RAM, mirrored */
    const uint8_t *m = p->ram + (pa & (RAM_SIZE - 1));
    *v = size == 4 ? rd32le(m) : size == 2 ? rd16le(m) : *m;
    p->cpu.cycles += 1;
    return true;
  }
  if (!to_phys(va, &pa) || !bus_read(p, pa, size, v)) {
    p->cpu.cop0[C0_BADVADDR] = va;
    exception(p, EXC_DBE, 0);
    return false;
  }
  p->cpu.cycles += 2;
  return true;
}

static bool store(PS1 *p, uint32_t va, int size, uint32_t v) {
  if (va & (uint32_t)(size - 1)) { address_error(p, va, true); return false; }
  /* Cache isolated (the BIOS flushing the cache): the store goes nowhere. */
  if (p->cpu.cop0[C0_SR] & SR_ISC) return true;
  uint32_t pa;
  if (to_phys(va, &pa) && pa < 0x00800000u) {
    uint8_t *m = p->ram + (pa & (RAM_SIZE - 1));
    if (size == 4) wr32le(m, v);
    else if (size == 2) wr16le(m, (uint16_t)v);
    else *m = (uint8_t)v;
    return true;
  }
  if (!to_phys(va, &pa) || !bus_write(p, pa, size, v)) {
    p->cpu.cop0[C0_BADVADDR] = va;
    exception(p, EXC_DBE, 0);
    return false;
  }
  return true;
}

/* ------------------------------------------------------------------ */
/* Registers and delay slots                                            */

static inline void W(Cpu *c, uint32_t r, uint32_t v) {
  c->gpr[r] = v;
  c->write_reg = r;
}

/* A load's value lands after the next instruction. */
static inline void L(Cpu *c, uint32_t r, uint32_t v) {
  c->load_reg = r;
  c->load_val = v;
}

/* LWL/LWR merge with a load to the same register still in flight. */
static inline uint32_t merge_base(const Cpu *c, uint32_t r) {
  return c->pend_reg == r ? c->pend_val : c->gpr[r];
}

static inline void branch(Cpu *c, bool taken, uint32_t target) {
  if (taken) c->next_pc = target;
  c->in_delay = true;
  c->branch_taken = taken;
}

/* MFHI/MFLO wait for a multiply or divide still running. */
static inline void muldiv_wait(Cpu *c) {
  if (c->cycles < c->muldiv_ready) c->cycles = c->muldiv_ready;
}

/* ------------------------------------------------------------------ */
/* Coprocessors                                                         */

static bool cop_usable(PS1 *p, uint32_t n) {
  /* COP0 is always usable in kernel mode; the others need SR.CUn. */
  uint32_t sr = p->cpu.cop0[C0_SR];
  if (n == 0 && !(sr & 2)) return true;   /* KUc = 0: kernel */
  if (sr & (1u << (28 + n))) return true;
  exception(p, EXC_CPU, n);
  return false;
}

static void cop0(PS1 *p, uint32_t op) {
  Cpu *c = &p->cpu;
  uint32_t rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31;
  switch (rs) {
    case 0x00: /* MFC0 */
    case 0x02: /* CFC0 (no control registers: same) */
      L(c, rt, rd < 16 ? c->cop0[rd] : 0);
      return;
    case 0x04: { /* MTC0 */
      uint32_t v = c->gpr[rt];
      switch (rd) {
        case C0_CAUSE: c->cop0[rd] = (c->cop0[rd] & ~0x300u) | (v & 0x300u); return;  /* software interrupts only */
        case C0_PRID: case C0_BADVADDR: case C0_EPC: case C0_TAR: return;             /* read-only */
        default: if (rd < 16) c->cop0[rd] = v; return;
      }
    }
    case 0x10: /* COP0 command */
      if ((op & 0x3F) == 0x10) {   /* RFE: pop the interrupt-enable / mode stack */
        uint32_t sr = c->cop0[C0_SR];
        c->cop0[C0_SR] = (sr & ~0xFu) | ((sr >> 2) & 0xFu);
      }
      return;
    default:
      exception(p, EXC_RI, 0);
      return;
  }
}

static void cop2(PS1 *p, uint32_t op) {
  Cpu *c = &p->cpu;
  uint32_t rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31;
  if (op & (1u << 25)) { gte_command(p, op); return; }
  switch (rs) {
    case 0x00: L(c, rt, gte_read_data(p, rd)); return;   /* MFC2 */
    case 0x02: L(c, rt, gte_read_ctrl(p, rd)); return;   /* CFC2 */
    case 0x04: gte_write_data(p, rd, c->gpr[rt]); return; /* MTC2 */
    case 0x06: gte_write_ctrl(p, rd, c->gpr[rt]); return; /* CTC2 */
    default: exception(p, EXC_RI, 0); return;
  }
}

/* ------------------------------------------------------------------ */
/* Execute                                                              */

static void execute(PS1 *p, uint32_t op) {
  Cpu *c = &p->cpu;
  uint32_t rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sa = (op >> 6) & 31;
  uint32_t s = c->gpr[rs], t = c->gpr[rt];
  uint32_t imm = op & 0xFFFF;
  int32_t simm = sext16(imm);
  uint32_t baddr = c->cur_pc + 4 + ((uint32_t)simm << 2);

  switch (op >> 26) {
    case 0x00: /* SPECIAL */
      switch (op & 0x3F) {
        case 0x00: W(c, rd, t << sa); break;                                 /* SLL */
        case 0x02: W(c, rd, t >> sa); break;                                 /* SRL */
        case 0x03: W(c, rd, (uint32_t)((int32_t)t >> sa)); break;            /* SRA */
        case 0x04: W(c, rd, t << (s & 31)); break;                           /* SLLV */
        case 0x06: W(c, rd, t >> (s & 31)); break;                           /* SRLV */
        case 0x07: W(c, rd, (uint32_t)((int32_t)t >> (s & 31))); break;      /* SRAV */
        case 0x08: /* JR */
          if (s & 3) {
            /* The jump happens; fetching the target faults (at the target). */
            branch(c, true, s);
          } else {
            branch(c, true, s);
          }
          break;
        case 0x09: /* JALR: rs is read before rd is written */
          W(c, rd, c->cur_pc + 8);
          branch(c, true, s);
          break;
        case 0x0C: exception(p, EXC_SYS, 0); return;                        /* SYSCALL */
        case 0x0D: exception(p, EXC_BP, 0); return;                         /* BREAK */
        case 0x10: muldiv_wait(c); W(c, rd, c->hi); break;                  /* MFHI */
        case 0x11: c->hi = s; break;                                         /* MTHI */
        case 0x12: muldiv_wait(c); W(c, rd, c->lo); break;                  /* MFLO */
        case 0x13: c->lo = s; break;                                         /* MTLO */
        case 0x18: { /* MULT */
          int64_t r = (int64_t)(int32_t)s * (int32_t)t;
          c->hi = (uint32_t)((uint64_t)r >> 32);
          c->lo = (uint32_t)r;
          /* Faster for small operands (the multiplier exits early). */
          uint32_t m = (int32_t)s < 0 ? ~s : s;
          c->muldiv_ready = c->cycles + (m < 0x800 ? 6 : m < 0x100000 ? 9 : 13);
          break;
        }
        case 0x19: { /* MULTU */
          uint64_t r = (uint64_t)s * t;
          c->hi = (uint32_t)(r >> 32);
          c->lo = (uint32_t)r;
          c->muldiv_ready = c->cycles + (s < 0x800 ? 6 : s < 0x100000 ? 9 : 13);
          break;
        }
        case 0x1A: /* DIV */
          if (t == 0) {
            c->hi = s;
            c->lo = (int32_t)s >= 0 ? 0xFFFFFFFFu : 1;
          } else if (s == 0x80000000u && t == 0xFFFFFFFFu) {
            c->hi = 0;
            c->lo = 0x80000000u;
          } else {
            c->lo = (uint32_t)((int32_t)s / (int32_t)t);
            c->hi = (uint32_t)((int32_t)s % (int32_t)t);
          }
          c->muldiv_ready = c->cycles + 36;
          break;
        case 0x1B: /* DIVU */
          if (t == 0) {
            c->hi = s;
            c->lo = 0xFFFFFFFFu;
          } else {
            c->lo = s / t;
            c->hi = s % t;
          }
          c->muldiv_ready = c->cycles + 36;
          break;
        case 0x20: { /* ADD */
          uint32_t r = s + t;
          if (~(s ^ t) & (s ^ r) & 0x80000000u) { exception(p, EXC_OV, 0); return; }
          W(c, rd, r);
          break;
        }
        case 0x21: W(c, rd, s + t); break;                                   /* ADDU */
        case 0x22: { /* SUB */
          uint32_t r = s - t;
          if ((s ^ t) & (s ^ r) & 0x80000000u) { exception(p, EXC_OV, 0); return; }
          W(c, rd, r);
          break;
        }
        case 0x23: W(c, rd, s - t); break;                                   /* SUBU */
        case 0x24: W(c, rd, s & t); break;                                   /* AND */
        case 0x25: W(c, rd, s | t); break;                                   /* OR */
        case 0x26: W(c, rd, s ^ t); break;                                   /* XOR */
        case 0x27: W(c, rd, ~(s | t)); break;                                /* NOR */
        case 0x2A: W(c, rd, (int32_t)s < (int32_t)t); break;                 /* SLT */
        case 0x2B: W(c, rd, s < t); break;                                   /* SLTU */
        default: exception(p, EXC_RI, 0); return;
      }
      break;

    case 0x01: { /* REGIMM: bit 0 of rt = GEZ; rt 0x10/0x11 also link.
                  * Other rt values decode as plain BLTZ/BGEZ (no link). */
      bool ge = (rt & 1) != 0;
      bool taken = ge ? (int32_t)s >= 0 : (int32_t)s < 0;
      if ((rt & 0x1E) == 0x10) W(c, 31, c->cur_pc + 8);
      branch(c, taken, baddr);
      break;
    }
    case 0x02: branch(c, true, (c->pc & 0xF0000000u) | ((op & 0x3FFFFFF) << 2)); break;        /* J */
    case 0x03: W(c, 31, c->cur_pc + 8); branch(c, true, (c->pc & 0xF0000000u) | ((op & 0x3FFFFFF) << 2)); break; /* JAL */
    case 0x04: branch(c, s == t, baddr); break;                                                 /* BEQ */
    case 0x05: branch(c, s != t, baddr); break;                                                 /* BNE */
    case 0x06: branch(c, (int32_t)s <= 0, baddr); break;                                        /* BLEZ */
    case 0x07: branch(c, (int32_t)s > 0, baddr); break;                                         /* BGTZ */
    case 0x08: { /* ADDI */
      uint32_t r = s + (uint32_t)simm;
      if (~(s ^ (uint32_t)simm) & (s ^ r) & 0x80000000u) { exception(p, EXC_OV, 0); return; }
      W(c, rt, r);
      break;
    }
    case 0x09: W(c, rt, s + (uint32_t)simm); break;                    /* ADDIU */
    case 0x0A: W(c, rt, (int32_t)s < simm); break;                     /* SLTI */
    case 0x0B: W(c, rt, s < (uint32_t)simm); break;                    /* SLTIU */
    case 0x0C: W(c, rt, s & imm); break;                               /* ANDI */
    case 0x0D: W(c, rt, s | imm); break;                               /* ORI */
    case 0x0E: W(c, rt, s ^ imm); break;                               /* XORI */
    case 0x0F: W(c, rt, imm << 16); break;                             /* LUI */
    case 0x10: if (cop_usable(p, 0)) cop0(p, op); return;              /* COP0 */
    case 0x11: case 0x13:                                              /* COP1, COP3: don't exist */
      cop_usable(p, (op >> 26) & 3);
      return;
    case 0x12: if (cop_usable(p, 2)) cop2(p, op); return;              /* COP2 (GTE) */

    case 0x20: { uint32_t v; if (load(p, s + (uint32_t)simm, 1, &v)) L(c, rt, (uint32_t)(int32_t)(int8_t)v); return; }   /* LB */
    case 0x21: { uint32_t v; if (load(p, s + (uint32_t)simm, 2, &v)) L(c, rt, (uint32_t)sext16(v)); return; }             /* LH */
    case 0x22: { /* LWL */
      uint32_t a = s + (uint32_t)simm, v;
      if (!load(p, a & ~3u, 4, &v)) return;
      uint32_t cur = merge_base(c, rt), sh = (a & 3) * 8;
      L(c, rt, (cur & (0x00FFFFFFu >> sh)) | (v << (24 - sh)));
      return;
    }
    case 0x23: { uint32_t v; if (load(p, s + (uint32_t)simm, 4, &v)) L(c, rt, v); return; }                               /* LW */
    case 0x24: { uint32_t v; if (load(p, s + (uint32_t)simm, 1, &v)) L(c, rt, v & 0xFF); return; }                        /* LBU */
    case 0x25: { uint32_t v; if (load(p, s + (uint32_t)simm, 2, &v)) L(c, rt, v & 0xFFFF); return; }                      /* LHU */
    case 0x26: { /* LWR */
      uint32_t a = s + (uint32_t)simm, v;
      if (!load(p, a & ~3u, 4, &v)) return;
      uint32_t cur = merge_base(c, rt), sh = (a & 3) * 8;
      L(c, rt, (cur & ~(0xFFFFFFFFu >> sh)) | (v >> sh));
      return;
    }
    case 0x28: store(p, s + (uint32_t)simm, 1, t); return;             /* SB */
    case 0x29: store(p, s + (uint32_t)simm, 2, t); return;             /* SH */
    case 0x2A: { /* SWL */
      uint32_t a = s + (uint32_t)simm, m;
      if (p->cpu.cop0[C0_SR] & SR_ISC) return;
      if (!load(p, a & ~3u, 4, &m)) return;
      uint32_t sh = (a & 3) * 8;
      store(p, a & ~3u, 4, (m & (0xFFFFFF00u << sh)) | (t >> (24 - sh)));
      return;
    }
    case 0x2B: store(p, s + (uint32_t)simm, 4, t); return;             /* SW */
    case 0x2E: { /* SWR */
      uint32_t a = s + (uint32_t)simm, m;
      if (p->cpu.cop0[C0_SR] & SR_ISC) return;
      if (!load(p, a & ~3u, 4, &m)) return;
      uint32_t sh = (a & 3) * 8;
      store(p, a & ~3u, 4, (m & ~(0xFFFFFFFFu << sh)) | (t << sh));
      return;
    }
    case 0x30: case 0x31: case 0x33:                                   /* LWC0/1/3 */
      cop_usable(p, (op >> 26) & 3);
      return;
    case 0x32: { /* LWC2 */
      if (!cop_usable(p, 2)) return;
      uint32_t v;
      if (load(p, s + (uint32_t)simm, 4, &v)) gte_write_data(p, rt, v);
      return;
    }
    case 0x38: case 0x39: case 0x3B:                                   /* SWC0/1/3 */
      cop_usable(p, (op >> 26) & 3);
      return;
    case 0x3A: /* SWC2 */
      if (!cop_usable(p, 2)) return;
      store(p, s + (uint32_t)simm, 4, gte_read_data(p, rt));
      return;
    default:
      exception(p, EXC_RI, 0);
      return;
  }
}

/* ------------------------------------------------------------------ */
/* Stepping                                                             */

void cpu_reset(PS1 *p) {
  Cpu *c = &p->cpu;
  memset(c, 0, sizeof *c);
  c->pc = 0xBFC00000u;
  c->next_pc = c->pc + 4;
  c->cop0[C0_SR] = SR_BEV;
  c->cop0[C0_PRID] = 0x00000002u;
}

static inline bool interrupt_pending(const Cpu *c) {
  uint32_t sr = c->cop0[C0_SR];
  return (sr & SR_IEC) && (c->cop0[C0_CAUSE] & sr & 0xFF00u);
}

void cpu_step(PS1 *p) {
  Cpu *c = &p->cpu;
  /* The load issued by the previous instruction lands after this one. */
  c->pend_reg = c->load_reg;
  c->pend_val = c->load_val;
  c->load_reg = 0;
  c->write_reg = 0;
  c->cur_pc = c->pc;
  c->cur_delay = c->in_delay;
  c->in_delay = false;
  bool was_taken = c->branch_taken;
  c->branch_taken = was_taken && c->cur_delay;

  /* The hardware interrupt line (IP2) follows I_STAT & I_MASK. */
  if (p->i_stat & p->i_mask) c->cop0[C0_CAUSE] |= 0x400u;
  else c->cop0[C0_CAUSE] &= ~0x400u;

  c->cycles += 1;
  if (p->exe_pending && c->pc == 0x80030000u) ps1_sideload(p);
  /* Console output through a real BIOS: catch putchar on its way in. */
  if (!p->hle_bios && ((c->pc == 0xB0u && c->gpr[9] == 0x3D) || (c->pc == 0xA0u && c->gpr[9] == 0x3C)))
    ps1_tty_putc(p, (char)c->gpr[4]);
  if (interrupt_pending(c)) {
    exception(p, EXC_INT, 0);
  } else if (p->hle_bios && (c->pc == 0xA0u || c->pc == 0xB0u || c->pc == 0xC0u ||
                             c->pc == 0x800000A0u || c->pc == 0x800000B0u || c->pc == 0x800000C0u)) {
    hle_bios_call(p, c->pc & 0xFF);
  } else if (c->pc & 3) {
    address_error(p, c->pc, false);
  } else {
    uint32_t pa = 0, op;
    bool mapped = to_phys(c->pc, &pa);
    if (mapped && pa < 0x00800000u) {
      op = rd32le(p->ram + (pa & (RAM_SIZE - 1)));
    } else if (mapped && pa >= 0x1FC00000u && pa < 0x1FC00000u + BIOS_SIZE && p->bios) {
      op = rd32le(p->bios + (pa - 0x1FC00000u));
      c->cycles += 6;   /* uncached ROM: slow fetches */
    } else if (!mapped || !bus_read(p, pa, 4, &op)) {
      c->cop0[C0_BADVADDR] = c->pc;
      exception(p, EXC_IBE, 0);
      goto done;
    }
    c->pc = c->next_pc;
    c->next_pc += 4;
    execute(p, op);
  }
done:
  if (c->pend_reg && c->pend_reg != c->write_reg) c->gpr[c->pend_reg] = c->pend_val;
  c->gpr[0] = 0;
}

void cpu_run(PS1 *p, uint32_t cycles) {
  uint64_t end = p->cpu.cycles + cycles;
  while (p->cpu.cycles < end) cpu_step(p);
}
