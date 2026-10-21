/*
 * VR4300 (MIPS III) interpreter: integer unit, COP0 (system control, TLB,
 * exceptions, interrupts). The FPU lives in fpu.c.
 */
#include <string.h>

#include "n64.h"

#define RS(op) (((op) >> 21) & 31)
#define RT(op) (((op) >> 16) & 31)
#define RD(op) (((op) >> 11) & 31)
#define SA(op) (((op) >> 6) & 31)
#define IMM(op) ((uint16_t)(op))
#define SIMM(op) ((int64_t)(int16_t)(op))
#define FUNCT(op) ((op) & 63)

typedef enum { ACC_LOAD, ACC_STORE, ACC_FETCH } Access;

/* ------------------------------------------------------------------ */
/* Exceptions                                                          */

static void exception(N64 *n, uint32_t code, uint32_t offset, uint32_t ce) {
  Cpu *c = &n->cpu;
  uint64_t status = c->cp0[CP0_STATUS];
  uint64_t cause = c->cp0[CP0_CAUSE] & ~(0x7Cull | 0x30000000ull);
  cause |= (uint64_t)(code & 31) << 2 | (uint64_t)(ce & 3) << 28;
  if (!(status & ST_EXL)) {
    if (c->cur_delay) {
      c->cp0[CP0_EPC] = c->cur_pc - 4;
      cause |= 0x80000000ull;
    } else {
      c->cp0[CP0_EPC] = c->cur_pc;
      cause &= ~0x80000000ull;
    }
  } else {
    offset = 0x180; /* nested: always the general vector */
  }
  c->cp0[CP0_CAUSE] = cause;
  c->cp0[CP0_STATUS] = status | ST_EXL;
  uint64_t base = (status & ST_BEV) ? 0xFFFFFFFFBFC00200ull : 0xFFFFFFFF80000000ull;
  c->pc = base + offset;
  c->next_pc = c->pc + 4;
  c->in_delay = false;
}

static void address_error(N64 *n, uint64_t va, bool store) {
  Cpu *c = &n->cpu;
  c->cp0[CP0_BADVADDR] = va;
  /* Hardware also updates Context's BadVPN2 field here. */
  c->cp0[CP0_CONTEXT] = (c->cp0[CP0_CONTEXT] & ~0x7FFFF0ull) | ((va >> 13) & 0x7FFFF) << 4;
  exception(n, store ? EXC_ADES : EXC_ADEL, 0x180, 0);
}

static void tlb_exception(N64 *n, uint64_t va, bool store, bool refill) {
  Cpu *c = &n->cpu;
  c->cp0[CP0_BADVADDR] = va;
  c->cp0[CP0_CONTEXT] = (c->cp0[CP0_CONTEXT] & ~0x7FFFF0ull) | ((va >> 13) & 0x7FFFF) << 4;
  c->cp0[CP0_XCONTEXT] = (c->cp0[CP0_XCONTEXT] & ~0x1FFFFFFF0ull) | ((va >> 13) & 0x7FFFFFF) << 4 |
                         ((va >> 62) & 3) << 31;
  c->cp0[CP0_ENTRYHI] = (va & 0xC00000FFFFFFE000ull) | (c->cp0[CP0_ENTRYHI] & 0xFF);
  uint32_t off = 0x180;
  if (refill && !(c->cp0[CP0_STATUS] & ST_EXL)) {
    bool is64 = (c->cp0[CP0_STATUS] & ST_KX) != 0;
    off = is64 ? 0x080 : 0x000;
  }
  exception(n, store ? EXC_TLBS : EXC_TLBL, off, 0);
}

/* ------------------------------------------------------------------ */
/* Address translation                                                  */

static int tlb_lookup(Cpu *c, uint64_t va, uint32_t *pa, bool store, bool *refill) {
  uint8_t asid = (uint8_t)c->cp0[CP0_ENTRYHI];
  for (int i = 0; i < 32; i++) {
    TlbEntry *e = &c->tlb[i];
    uint64_t m = e->mask | 0x1FFF;
    uint64_t vpn_mask = 0xC00000FFFFFFE000ull & ~m;
    if ((va & vpn_mask) != (e->hi & vpn_mask)) continue;
    if (!e->global && (uint8_t)e->hi != asid) continue;
    uint64_t half = (m + 1) >> 1;
    uint64_t lo = (va & half) ? e->lo1 : e->lo0;
    if (!(lo & 2)) { *refill = false; return -1; }          /* invalid */
    if (store && !(lo & 4)) return -2;                      /* not dirty: TLB modification */
    uint64_t pfn = (lo >> 6) & 0xFFFFF;
    *pa = (uint32_t)((pfn << 12) | (va & (half - 1)));
    return 0;
  }
  *refill = true;
  return -1;
}

static bool translate(N64 *n, uint64_t va, Access acc, uint32_t *pa) {
  Cpu *c = &n->cpu;
  bool store = acc == ACC_STORE;
  uint64_t status = c->cp0[CP0_STATUS];
  bool kernel = (status & (ST_EXL | ST_ERL)) || ((status >> 3) & 3) == 0;
  bool sext = (uint64_t)(int64_t)(int32_t)va == va;

  if (sext) {
    uint32_t a = (uint32_t)va;
    if (!kernel && a >= 0x80000000u) { address_error(n, va, store); return false; }
    if (a >= 0x80000000u && a < 0xC0000000u) {
      *pa = a & 0x1FFFFFFFu; /* KSEG0 (cached) / KSEG1 (uncached) */
      return true;
    }
  } else {
    bool ok64 = kernel ? (status & ST_KX) : (status & ST_UX);
    if (!ok64) { address_error(n, va, store); return false; }
    if ((va >> 62) == 2 && kernel) { /* XKPHYS: unmapped */
      *pa = (uint32_t)(va & 0xFFFFFFFFu);
      return true;
    }
  }
  bool refill = false;
  int r = tlb_lookup(c, va, pa, store, &refill);
  if (r == 0) return true;
  if (r == -2) {
    c->cp0[CP0_BADVADDR] = va;
    c->cp0[CP0_CONTEXT] = (c->cp0[CP0_CONTEXT] & ~0x7FFFF0ull) | ((va >> 13) & 0x7FFFF) << 4;
    c->cp0[CP0_ENTRYHI] = (va & 0xC00000FFFFFFE000ull) | (c->cp0[CP0_ENTRYHI] & 0xFF);
    exception(n, EXC_MOD, 0x180, 0);
    return false;
  }
  tlb_exception(n, va, store, refill);
  return false;
}

/* ------------------------------------------------------------------ */
/* Loads and stores through the MMU                                     */

/* KSEG0/KSEG1 addresses that land in RDRAM skip translation and the bus. */
static inline uint8_t *direct(N64 *n, uint64_t va, uint32_t size) {
  if ((va & 0xFFFFFFFFC0000000ull) != 0xFFFFFFFF80000000ull) return NULL;
  uint32_t pa = (uint32_t)va & 0x1FFFFFFF;
  return pa + size <= RDRAM_SIZE ? n->rdram + pa : NULL;
}

static bool ld8(N64 *n, uint64_t va, uint8_t *v) {
  uint8_t *d = direct(n, va, 1);
  if (d) { *v = *d; return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_LOAD, &pa)) return false;
  bus_read8(n, pa, v);
  return true;
}
static bool ld16(N64 *n, uint64_t va, uint16_t *v) {
  if (va & 1) { address_error(n, va, false); return false; }
  uint8_t *d = direct(n, va, 2);
  if (d) { *v = rd16be(d); return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_LOAD, &pa)) return false;
  bus_read16(n, pa, v);
  return true;
}
static bool ld32(N64 *n, uint64_t va, uint32_t *v) {
  if (va & 3) { address_error(n, va, false); return false; }
  uint8_t *d = direct(n, va, 4);
  if (d) { *v = rd32be(d); return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_LOAD, &pa)) return false;
  bus_read32(n, pa, v);
  return true;
}
static bool ld64(N64 *n, uint64_t va, uint64_t *v) {
  if (va & 7) { address_error(n, va, false); return false; }
  uint8_t *d = direct(n, va, 8);
  if (d) { *v = rd64be(d); return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_LOAD, &pa)) return false;
  bus_read64(n, pa, v);
  return true;
}
static bool st8(N64 *n, uint64_t va, uint8_t v) {
  uint8_t *d = direct(n, va, 1);
  if (d) { *d = v; return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_STORE, &pa)) return false;
  bus_write8(n, pa, v);
  return true;
}
static bool st16(N64 *n, uint64_t va, uint16_t v) {
  if (va & 1) { address_error(n, va, true); return false; }
  uint8_t *d = direct(n, va, 2);
  if (d) { wr16be(d, v); return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_STORE, &pa)) return false;
  bus_write16(n, pa, v);
  return true;
}
static bool st32(N64 *n, uint64_t va, uint32_t v) {
  if (va & 3) { address_error(n, va, true); return false; }
  uint8_t *d = direct(n, va, 4);
  if (d) { wr32be(d, v); return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_STORE, &pa)) return false;
  bus_write32(n, pa, v);
  return true;
}
static bool st64(N64 *n, uint64_t va, uint64_t v) {
  if (va & 7) { address_error(n, va, true); return false; }
  uint8_t *d = direct(n, va, 8);
  if (d) { wr64be(d, v); return true; }
  uint32_t pa;
  if (!translate(n, va, ACC_STORE, &pa)) return false;
  bus_write64(n, pa, v);
  return true;
}

/* ------------------------------------------------------------------ */
/* 64x64 -> 128 multiply without compiler extensions (MSVC builds too)  */

static void mul64u(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
  uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
  uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
  *lo = (mid << 32) | (uint32_t)p00;
  *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static void mul64s(int64_t a, int64_t b, uint64_t *hi, uint64_t *lo) {
  mul64u((uint64_t)a, (uint64_t)b, hi, lo);
  if (a < 0) *hi -= (uint64_t)b;
  if (b < 0) *hi -= (uint64_t)a;
}

/* ------------------------------------------------------------------ */
/* COP0                                                                 */

static uint32_t random_value(Cpu *c) { return (uint32_t)c->cp0[CP0_RANDOM]; }

static void cp0_write(N64 *n, uint32_t r, uint64_t v) {
  Cpu *c = &n->cpu;
  switch (r) {
    case CP0_INDEX: c->cp0[r] = (c->cp0[r] & 0x80000000u) | (v & 0x3F); break;
    case CP0_RANDOM: case CP0_PRID: case CP0_BADVADDR: case CP0_CACHEERR: break;
    case CP0_ENTRYLO0: case CP0_ENTRYLO1: c->cp0[r] = v & 0x3FFFFFFF; break;
    case CP0_CONTEXT: c->cp0[r] = (c->cp0[r] & 0x7FFFF0ull) | (v & ~0x7FFFFFull); break;
    case CP0_XCONTEXT: c->cp0[r] = (c->cp0[r] & 0x1FFFFFFF0ull) | (v & ~0x1FFFFFFFFull); break;
    case CP0_PAGEMASK: c->cp0[r] = v & 0x01FFE000; break;
    case CP0_WIRED: c->cp0[r] = v & 0x3F; c->cp0[CP0_RANDOM] = 31; break;
    case CP0_COUNT: c->cp0[r] = (uint32_t)v; c->count_frac = 0; break;
    case CP0_ENTRYHI: c->cp0[r] = v & 0xC00000FFFFFFE0FFull; break;
    case CP0_COMPARE:
      c->cp0[r] = (uint32_t)v;
      c->cp0[CP0_CAUSE] &= ~(1ull << 15); /* clear timer interrupt (IP7) */
      break;
    case CP0_STATUS: c->cp0[r] = (uint32_t)v & 0xFF57FFFFu; break;
    case CP0_CAUSE: c->cp0[r] = (c->cp0[r] & ~0x300ull) | (v & 0x300); break;
    case CP0_CONFIG: c->cp0[r] = (c->cp0[r] & ~0x0F00800Full) | (v & 0x0F00800F); break;
    case CP0_LLADDR: c->cp0[r] = (uint32_t)v; break;
    case CP0_TAGLO: c->cp0[r] = v & 0x0FFFFFC0; break;
    case CP0_TAGHI: c->cp0[r] = 0; break;
    default: c->cp0[r] = v; break;
  }
}

static void tlb_write(Cpu *c, uint32_t i) {
  TlbEntry *e = &c->tlb[i & 31];
  e->mask = c->cp0[CP0_PAGEMASK] & 0x01FFE000;
  e->hi = c->cp0[CP0_ENTRYHI] & ~(e->mask) & 0xC00000FFFFFFE0FFull;
  e->lo0 = c->cp0[CP0_ENTRYLO0] & 0x3FFFFFFE;
  e->lo1 = c->cp0[CP0_ENTRYLO1] & 0x3FFFFFFE;
  e->global = (c->cp0[CP0_ENTRYLO0] & c->cp0[CP0_ENTRYLO1] & 1) != 0;
}

static void cpu_sync(Cpu *c);

static void cop0(N64 *n, uint32_t op) {
  /* COP0 reads and writes Count/Random/Compare/Status: bring the batched
   * timers up to date first, and re-check interrupts after. */
  cpu_sync(&n->cpu);
  n->cpu.irq_check = true;
  Cpu *c = &n->cpu;
  uint32_t rt = RT(op), rd = RD(op);
  switch (RS(op)) {
    case 0x00: /* MFC0 */
      if (rt) c->gpr[rt] = sext32((uint32_t)c->cp0[rd]);
      return;
    case 0x01: /* DMFC0 */
      if (rt) c->gpr[rt] = c->cp0[rd];
      return;
    case 0x04: /* MTC0 */
      cp0_write(n, rd, sext32((uint32_t)c->gpr[rt]));
      n64_update_interrupts(n);
      return;
    case 0x05: /* DMTC0 */
      cp0_write(n, rd, c->gpr[rt]);
      n64_update_interrupts(n);
      return;
  }
  if (!(RS(op) & 0x10)) { exception(n, EXC_RI, 0x180, 0); return; }
  switch (FUNCT(op)) {
    case 0x01: { /* TLBR */
      TlbEntry *e = &c->tlb[c->cp0[CP0_INDEX] & 31];
      c->cp0[CP0_PAGEMASK] = e->mask;
      c->cp0[CP0_ENTRYHI] = e->hi & ~e->mask;
      c->cp0[CP0_ENTRYLO0] = e->lo0 | (e->global ? 1 : 0);
      c->cp0[CP0_ENTRYLO1] = e->lo1 | (e->global ? 1 : 0);
      return;
    }
    case 0x02: tlb_write(c, (uint32_t)c->cp0[CP0_INDEX]); return;  /* TLBWI */
    case 0x06: tlb_write(c, random_value(c)); return;              /* TLBWR */
    case 0x08: { /* TLBP */
      uint64_t hi = c->cp0[CP0_ENTRYHI];
      c->cp0[CP0_INDEX] = 0x80000000u;
      for (uint32_t i = 0; i < 32; i++) {
        TlbEntry *e = &c->tlb[i];
        uint64_t vm = 0xC00000FFFFFFE000ull & ~(e->mask | 0x1FFF);
        if ((hi & vm) == (e->hi & vm) && (e->global || (uint8_t)e->hi == (uint8_t)hi)) {
          c->cp0[CP0_INDEX] = i;
          break;
        }
      }
      return;
    }
    case 0x18: { /* ERET */
      uint64_t st = c->cp0[CP0_STATUS];
      if (st & ST_ERL) {
        c->pc = c->cp0[CP0_ERROREPC];
        c->cp0[CP0_STATUS] = st & ~(uint64_t)ST_ERL;
      } else {
        c->pc = c->cp0[CP0_EPC];
        c->cp0[CP0_STATUS] = st & ~(uint64_t)ST_EXL;
      }
      c->next_pc = c->pc + 4;
      c->llbit = false;
      c->in_delay = false;
      n64_update_interrupts(n);
      return;
    }
    default:
      exception(n, EXC_RI, 0x180, 0);
  }
}

/* ------------------------------------------------------------------ */
/* Branch helpers                                                       */

static inline void branch(Cpu *c, bool taken, uint64_t target) {
  if (taken) c->next_pc = target;
  c->in_delay = true;
}

static inline void branch_likely(Cpu *c, bool taken, uint64_t target) {
  if (taken) {
    c->next_pc = target;
    c->in_delay = true;
  } else {
    /* Skip the delay slot. */
    c->pc = c->next_pc;
    c->next_pc = c->pc + 4;
  }
}

#define W(r, v) do { if (r) c->gpr[r] = (v); } while (0)

static bool cop_usable(N64 *n, uint32_t unit) {
  uint64_t st = n->cpu.cp0[CP0_STATUS];
  bool kernel = (st & (ST_EXL | ST_ERL)) || ((st >> 3) & 3) == 0;
  bool ok = unit == 0 ? (kernel || (st & ST_CU0)) : (st & (ST_CU0 << unit)) != 0;
  if (!ok) exception(n, EXC_CPU, 0x180, unit);
  return ok;
}

/* ------------------------------------------------------------------ */
/* SPECIAL (opcode 0)                                                   */

static void special(N64 *n, uint32_t op) {
  Cpu *c = &n->cpu;
  uint32_t rs = RS(op), rt = RT(op), rd = RD(op), sa = SA(op);
  uint64_t s = c->gpr[rs], t = c->gpr[rt];
  switch (FUNCT(op)) {
    case 0x00: W(rd, sext32((uint32_t)t << sa)); break;                               /* SLL */
    case 0x02: W(rd, sext32((uint32_t)t >> sa)); break;                               /* SRL */
    case 0x03: W(rd, sext32((uint32_t)((int64_t)t >> sa))); break;                    /* SRA */
    case 0x04: W(rd, sext32((uint32_t)t << (s & 31))); break;                         /* SLLV */
    case 0x06: W(rd, sext32((uint32_t)t >> (s & 31))); break;                         /* SRLV */
    case 0x07: W(rd, sext32((uint32_t)((int64_t)t >> (s & 31)))); break;              /* SRAV */
    case 0x08: branch(c, true, s); break;                                             /* JR */
    case 0x09: { uint64_t link = c->cur_pc + 8; branch(c, true, s); W(rd, link); break; } /* JALR */
    case 0x0C: exception(n, EXC_SYS, 0x180, 0); break;                                /* SYSCALL */
    case 0x0D: exception(n, EXC_BP, 0x180, 0); break;                                 /* BREAK */
    case 0x0F: break;                                                                 /* SYNC */
    case 0x10: W(rd, c->hi); break;                                                   /* MFHI */
    case 0x11: c->hi = s; break;                                                      /* MTHI */
    case 0x12: W(rd, c->lo); break;                                                   /* MFLO */
    case 0x13: c->lo = s; break;                                                      /* MTLO */
    case 0x14: W(rd, t << (s & 63)); break;                                           /* DSLLV */
    case 0x16: W(rd, t >> (s & 63)); break;                                           /* DSRLV */
    case 0x17: W(rd, (uint64_t)((int64_t)t >> (s & 63))); break;                      /* DSRAV */
    case 0x18: { /* MULT */
      int64_t p = (int64_t)(int32_t)s * (int64_t)(int32_t)t;
      c->lo = sext32((uint32_t)p);
      c->hi = sext32((uint32_t)((uint64_t)p >> 32));
      break;
    }
    case 0x19: { /* MULTU */
      uint64_t p = (uint64_t)(uint32_t)s * (uint32_t)t;
      c->lo = sext32((uint32_t)p);
      c->hi = sext32((uint32_t)(p >> 32));
      break;
    }
    case 0x1A: { /* DIV */
      int32_t a = (int32_t)s, b = (int32_t)t;
      if (b == 0) {
        c->lo = a < 0 ? 1 : (uint64_t)-1;
        c->hi = sext32((uint32_t)a);
      } else if (a == INT32_MIN && b == -1) {
        c->lo = sext32((uint32_t)a);
        c->hi = 0;
      } else {
        c->lo = sext32((uint32_t)(a / b));
        c->hi = sext32((uint32_t)(a % b));
      }
      break;
    }
    case 0x1B: { /* DIVU */
      uint32_t a = (uint32_t)s, b = (uint32_t)t;
      if (b == 0) {
        c->lo = (uint64_t)-1;
        c->hi = sext32(a);
      } else {
        c->lo = sext32(a / b);
        c->hi = sext32(a % b);
      }
      break;
    }
    case 0x1C: mul64s((int64_t)s, (int64_t)t, &c->hi, &c->lo); break;   /* DMULT */
    case 0x1D: mul64u(s, t, &c->hi, &c->lo); break;                     /* DMULTU */
    case 0x1E: { /* DDIV */
      int64_t a = (int64_t)s, b = (int64_t)t;
      if (b == 0) {
        c->lo = a < 0 ? 1 : (uint64_t)-1;
        c->hi = (uint64_t)a;
      } else if (a == INT64_MIN && b == -1) {
        c->lo = (uint64_t)a;
        c->hi = 0;
      } else {
        c->lo = (uint64_t)(a / b);
        c->hi = (uint64_t)(a % b);
      }
      break;
    }
    case 0x1F: /* DDIVU */
      if (t == 0) {
        c->lo = (uint64_t)-1;
        c->hi = s;
      } else {
        c->lo = s / t;
        c->hi = s % t;
      }
      break;
    case 0x20: { /* ADD */
      uint32_t r = (uint32_t)s + (uint32_t)t;
      if ((~((uint32_t)s ^ (uint32_t)t) & ((uint32_t)s ^ r)) >> 31) { exception(n, EXC_OV, 0x180, 0); break; }
      W(rd, sext32(r));
      break;
    }
    case 0x21: W(rd, sext32((uint32_t)s + (uint32_t)t)); break;          /* ADDU */
    case 0x22: { /* SUB */
      uint32_t r = (uint32_t)s - (uint32_t)t;
      if ((((uint32_t)s ^ (uint32_t)t) & ((uint32_t)s ^ r)) >> 31) { exception(n, EXC_OV, 0x180, 0); break; }
      W(rd, sext32(r));
      break;
    }
    case 0x23: W(rd, sext32((uint32_t)s - (uint32_t)t)); break;          /* SUBU */
    case 0x24: W(rd, s & t); break;                                      /* AND */
    case 0x25: W(rd, s | t); break;                                      /* OR */
    case 0x26: W(rd, s ^ t); break;                                      /* XOR */
    case 0x27: W(rd, ~(s | t)); break;                                   /* NOR */
    case 0x2A: W(rd, (int64_t)s < (int64_t)t); break;                    /* SLT */
    case 0x2B: W(rd, s < t); break;                                      /* SLTU */
    case 0x2C: { /* DADD */
      uint64_t r = s + t;
      if ((~(s ^ t) & (s ^ r)) >> 63) { exception(n, EXC_OV, 0x180, 0); break; }
      W(rd, r);
      break;
    }
    case 0x2D: W(rd, s + t); break;                                      /* DADDU */
    case 0x2E: { /* DSUB */
      uint64_t r = s - t;
      if (((s ^ t) & (s ^ r)) >> 63) { exception(n, EXC_OV, 0x180, 0); break; }
      W(rd, r);
      break;
    }
    case 0x2F: W(rd, s - t); break;                                      /* DSUBU */
    case 0x30: if ((int64_t)s >= (int64_t)t) exception(n, EXC_TR, 0x180, 0); break; /* TGE */
    case 0x31: if (s >= t) exception(n, EXC_TR, 0x180, 0); break;                   /* TGEU */
    case 0x32: if ((int64_t)s < (int64_t)t) exception(n, EXC_TR, 0x180, 0); break;  /* TLT */
    case 0x33: if (s < t) exception(n, EXC_TR, 0x180, 0); break;                    /* TLTU */
    case 0x34: if (s == t) exception(n, EXC_TR, 0x180, 0); break;                   /* TEQ */
    case 0x36: if (s != t) exception(n, EXC_TR, 0x180, 0); break;                   /* TNE */
    case 0x38: W(rd, t << sa); break;                                    /* DSLL */
    case 0x3A: W(rd, t >> sa); break;                                    /* DSRL */
    case 0x3B: W(rd, (uint64_t)((int64_t)t >> sa)); break;               /* DSRA */
    case 0x3C: W(rd, t << (sa + 32)); break;                             /* DSLL32 */
    case 0x3E: W(rd, t >> (sa + 32)); break;                             /* DSRL32 */
    case 0x3F: W(rd, (uint64_t)((int64_t)t >> (sa + 32))); break;        /* DSRA32 */
    default: exception(n, EXC_RI, 0x180, 0); break;
  }
}

static void regimm(N64 *n, uint32_t op) {
  Cpu *c = &n->cpu;
  int64_t s = (int64_t)c->gpr[RS(op)];
  uint64_t target = c->cur_pc + 4 + (uint64_t)(SIMM(op) << 2);
  int64_t imm = SIMM(op);
  switch (RT(op)) {
    case 0x00: branch(c, s < 0, target); break;          /* BLTZ */
    case 0x01: branch(c, s >= 0, target); break;         /* BGEZ */
    case 0x02: branch_likely(c, s < 0, target); break;   /* BLTZL */
    case 0x03: branch_likely(c, s >= 0, target); break;  /* BGEZL */
    case 0x08: if (s >= imm) exception(n, EXC_TR, 0x180, 0); break;                        /* TGEI */
    case 0x09: if ((uint64_t)s >= (uint64_t)imm) exception(n, EXC_TR, 0x180, 0); break;    /* TGEIU */
    case 0x0A: if (s < imm) exception(n, EXC_TR, 0x180, 0); break;                         /* TLTI */
    case 0x0B: if ((uint64_t)s < (uint64_t)imm) exception(n, EXC_TR, 0x180, 0); break;     /* TLTIU */
    case 0x0C: if (s == imm) exception(n, EXC_TR, 0x180, 0); break;                        /* TEQI */
    case 0x0E: if (s != imm) exception(n, EXC_TR, 0x180, 0); break;                        /* TNEI */
    case 0x10: c->gpr[31] = c->cur_pc + 8; branch(c, s < 0, target); break;          /* BLTZAL */
    case 0x11: c->gpr[31] = c->cur_pc + 8; branch(c, s >= 0, target); break;         /* BGEZAL */
    case 0x12: c->gpr[31] = c->cur_pc + 8; branch_likely(c, s < 0, target); break;   /* BLTZALL */
    case 0x13: c->gpr[31] = c->cur_pc + 8; branch_likely(c, s >= 0, target); break;  /* BGEZALL */
    default: exception(n, EXC_RI, 0x180, 0); break;
  }
}

/* ------------------------------------------------------------------ */
/* Main decoder                                                         */

static void execute(N64 *n, uint32_t op) {
  Cpu *c = &n->cpu;
  uint32_t rs = RS(op), rt = RT(op);
  uint64_t s = c->gpr[rs], t = c->gpr[rt];
  uint64_t addr = s + (uint64_t)SIMM(op);
  uint64_t btarget = c->cur_pc + 4 + (uint64_t)(SIMM(op) << 2);

  switch (op >> 26) {
    case 0x00: special(n, op); break;
    case 0x01: regimm(n, op); break;
    case 0x02: branch(c, true, ((c->cur_pc + 4) & ~0x0FFFFFFFull) | ((uint64_t)(op & 0x3FFFFFF) << 2)); break; /* J */
    case 0x03: /* JAL */
      c->gpr[31] = c->cur_pc + 8;
      branch(c, true, ((c->cur_pc + 4) & ~0x0FFFFFFFull) | ((uint64_t)(op & 0x3FFFFFF) << 2));
      break;
    case 0x04: branch(c, s == t, btarget); break;                   /* BEQ */
    case 0x05: branch(c, s != t, btarget); break;                   /* BNE */
    case 0x06: branch(c, (int64_t)s <= 0, btarget); break;          /* BLEZ */
    case 0x07: branch(c, (int64_t)s > 0, btarget); break;           /* BGTZ */
    case 0x08: { /* ADDI */
      uint32_t a = (uint32_t)s, b = (uint32_t)SIMM(op), r = a + b;
      if ((~(a ^ b) & (a ^ r)) >> 31) { exception(n, EXC_OV, 0x180, 0); break; }
      W(rt, sext32(r));
      break;
    }
    case 0x09: W(rt, sext32((uint32_t)s + (uint32_t)SIMM(op))); break;  /* ADDIU */
    case 0x0A: W(rt, (int64_t)s < SIMM(op)); break;                     /* SLTI */
    case 0x0B: W(rt, s < (uint64_t)SIMM(op)); break;                    /* SLTIU */
    case 0x0C: W(rt, s & IMM(op)); break;                               /* ANDI */
    case 0x0D: W(rt, s | IMM(op)); break;                               /* ORI */
    case 0x0E: W(rt, s ^ IMM(op)); break;                               /* XORI */
    case 0x0F: W(rt, sext32((uint32_t)IMM(op) << 16)); break;           /* LUI */
    case 0x10: if (cop_usable(n, 0)) cop0(n, op); break;                /* COP0 */
    case 0x11: /* COP1 */
      if (!cop_usable(n, 1)) break;
      if (RS(op) == 0x08) { /* BC1F / BC1T / BC1FL / BC1TL */
        bool cond = (n->cpu.fcr31 >> 23) & 1;
        bool taken = (RT(op) & 1) ? cond : !cond;
        if (RT(op) & 2) branch_likely(c, taken, btarget); else branch(c, taken, btarget);
        break;
      }
      fpu_cop1(n, op);
      break;
    case 0x12: /* COP2: unusable unless CU2, then reserved */
      if (cop_usable(n, 2)) exception(n, EXC_RI, 0x180, 0);
      break;
    case 0x13: exception(n, EXC_RI, 0x180, 0); break;                   /* COP3 */
    case 0x14: branch_likely(c, s == t, btarget); break;                /* BEQL */
    case 0x15: branch_likely(c, s != t, btarget); break;                /* BNEL */
    case 0x16: branch_likely(c, (int64_t)s <= 0, btarget); break;       /* BLEZL */
    case 0x17: branch_likely(c, (int64_t)s > 0, btarget); break;        /* BGTZL */
    case 0x18: { /* DADDI */
      uint64_t b = (uint64_t)SIMM(op), r = s + b;
      if ((~(s ^ b) & (s ^ r)) >> 63) { exception(n, EXC_OV, 0x180, 0); break; }
      W(rt, r);
      break;
    }
    case 0x19: W(rt, s + (uint64_t)SIMM(op)); break;                    /* DADDIU */
    case 0x1A: { /* LDL */
      uint64_t d;
      if (!ld64(n, addr & ~7ull, &d)) break;
      uint32_t sh = 8 * (uint32_t)(addr & 7);
      uint64_t mask = sh ? ~0ull << sh : ~0ull;
      W(rt, (t & ~mask) | (d << sh));
      break;
    }
    case 0x1B: { /* LDR */
      uint64_t d;
      if (!ld64(n, addr & ~7ull, &d)) break;
      uint32_t sh = 8 * (7 - (uint32_t)(addr & 7));
      uint64_t mask = ~0ull >> sh;
      W(rt, (t & ~mask) | (d >> sh));
      break;
    }
    case 0x20: { uint8_t v; if (ld8(n, addr, &v)) W(rt, sext8(v)); break; }     /* LB */
    case 0x21: { uint16_t v; if (ld16(n, addr, &v)) W(rt, sext16(v)); break; }  /* LH */
    case 0x22: { /* LWL */
      uint32_t w;
      if (!ld32(n, addr & ~3ull, &w)) break;
      uint32_t sh = 8 * (uint32_t)(addr & 3);
      uint32_t mask = 0xFFFFFFFFu << sh;
      W(rt, sext32(((uint32_t)t & ~mask) | (w << sh)));
      break;
    }
    case 0x23: { uint32_t v; if (ld32(n, addr, &v)) W(rt, sext32(v)); break; }  /* LW */
    case 0x24: { uint8_t v; if (ld8(n, addr, &v)) W(rt, v); break; }            /* LBU */
    case 0x25: { uint16_t v; if (ld16(n, addr, &v)) W(rt, v); break; }          /* LHU */
    case 0x26: { /* LWR */
      uint32_t w;
      if (!ld32(n, addr & ~3ull, &w)) break;
      uint32_t sh = 8 * (3 - (uint32_t)(addr & 3));
      uint32_t mask = 0xFFFFFFFFu >> sh;
      W(rt, sext32(((uint32_t)t & ~mask) | (w >> sh)));
      break;
    }
    case 0x27: { uint32_t v; if (ld32(n, addr, &v)) W(rt, v); break; }          /* LWU */
    case 0x28: st8(n, addr, (uint8_t)t); break;                                  /* SB */
    case 0x29: st16(n, addr, (uint16_t)t); break;                                /* SH */
    case 0x2A: { /* SWL */
      uint32_t w;
      if (!ld32(n, addr & ~3ull, &w)) break;
      uint32_t sh = 8 * (uint32_t)(addr & 3);
      uint32_t mask = 0xFFFFFFFFu >> sh;
      st32(n, addr & ~3ull, (w & ~mask) | ((uint32_t)t >> sh));
      break;
    }
    case 0x2B: st32(n, addr, (uint32_t)t); break;                                /* SW */
    case 0x2C: { /* SDL */
      uint64_t d;
      if (!ld64(n, addr & ~7ull, &d)) break;
      uint32_t sh = 8 * (uint32_t)(addr & 7);
      uint64_t mask = ~0ull >> sh;
      st64(n, addr & ~7ull, (d & ~mask) | (t >> sh));
      break;
    }
    case 0x2D: { /* SDR */
      uint64_t d;
      if (!ld64(n, addr & ~7ull, &d)) break;
      uint32_t sh = 8 * (7 - (uint32_t)(addr & 7));
      uint64_t mask = sh ? ~0ull << sh : ~0ull;
      st64(n, addr & ~7ull, (d & ~mask) | (t << sh));
      break;
    }
    case 0x2E: { /* SWR */
      uint32_t w;
      if (!ld32(n, addr & ~3ull, &w)) break;
      uint32_t sh = 8 * (3 - (uint32_t)(addr & 3));
      uint32_t mask = 0xFFFFFFFFu << sh;
      st32(n, addr & ~3ull, (w & ~mask) | ((uint32_t)t << sh));
      break;
    }
    case 0x2F: break; /* CACHE: caches aren't emulated */
    case 0x30: { /* LL */
      uint32_t v, pa;
      if (addr & 3) { address_error(n, addr, false); break; }
      if (!translate(n, addr, ACC_LOAD, &pa)) break;
      bus_read32(n, pa, &v);
      W(rt, sext32(v));
      c->llbit = true;
      c->cp0[CP0_LLADDR] = pa >> 4;
      break;
    }
    case 0x31: { /* LWC1 */
      uint32_t v;
      if (!cop_usable(n, 1)) break;
      if (ld32(n, addr, &v)) fpu_lwc1(n, rt, v);
      break;
    }
    case 0x34: { /* LLD */
      uint64_t v;
      uint32_t pa;
      if (addr & 7) { address_error(n, addr, false); break; }
      if (!translate(n, addr, ACC_LOAD, &pa)) break;
      bus_read64(n, pa, &v);
      W(rt, v);
      c->llbit = true;
      c->cp0[CP0_LLADDR] = pa >> 4;
      break;
    }
    case 0x35: { /* LDC1 */
      uint64_t v;
      if (!cop_usable(n, 1)) break;
      if (ld64(n, addr, &v)) fpu_ldc1(n, rt, v);
      break;
    }
    case 0x37: { uint64_t v; if (ld64(n, addr, &v)) W(rt, v); break; }          /* LD */
    case 0x38: /* SC */
      if (c->llbit) {
        if (st32(n, addr, (uint32_t)t)) W(rt, 1);
      } else {
        W(rt, 0);
      }
      break;
    case 0x39: /* SWC1 */
      if (cop_usable(n, 1)) st32(n, addr, fpu_swc1(n, rt));
      break;
    case 0x3C: /* SCD */
      if (c->llbit) {
        if (st64(n, addr, t)) W(rt, 1);
      } else {
        W(rt, 0);
      }
      break;
    case 0x3D: /* SDC1 */
      if (cop_usable(n, 1)) st64(n, addr, fpu_sdc1(n, rt));
      break;
    case 0x3F: st64(n, addr, t); break;                                          /* SD */
    default: exception(n, EXC_RI, 0x180, 0); break;
  }
}

/* Raised from fpu.c */
void cpu_fpu_exception(N64 *n) { exception(n, EXC_FPE, 0x180, 0); }
void cpu_reserved(N64 *n) { exception(n, EXC_RI, 0x180, 0); }

/* ------------------------------------------------------------------ */

void cpu_reset(N64 *n) {
  Cpu *c = &n->cpu;
  memset(c, 0, sizeof *c);
  c->pc = 0xFFFFFFFFBFC00000ull;
  c->next_pc = c->pc + 4;
  c->cp0[CP0_RANDOM] = 31;
  c->cp0[CP0_STATUS] = ST_ERL | ST_BEV | 0x34000000u;
  c->cp0[CP0_PRID] = 0x00000B22;
  c->cp0[CP0_CONFIG] = 0x7006E463;
  c->cp0[CP0_EPC] = c->cp0[CP0_ERROREPC] = 0xFFFFFFFFFFFFFFFFull;
}

static inline bool interrupt_pending(Cpu *c) {
  uint64_t st = c->cp0[CP0_STATUS];
  if (!(st & ST_IE) || (st & (ST_EXL | ST_ERL))) return false;
  return (c->cp0[CP0_CAUSE] & st & 0xFF00) != 0;
}

/* Apply the instructions counted in c->pending to cycles, Count (half the
 * pipeline clock; Count == Compare raises IP7) and Random (counts down
 * from 31 to Wired, one step per instruction). */
static void cpu_sync(Cpu *c) {
  uint32_t p = c->pending;
  if (!p) return;
  c->pending = 0;
  c->cycles += p;
  uint32_t ticks = (c->count_frac + p) / 2;
  c->count_frac = (c->count_frac + p) % 2;
  if (ticks) {
    uint32_t before = (uint32_t)c->cp0[CP0_COUNT], cmp = (uint32_t)c->cp0[CP0_COMPARE];
    if ((uint32_t)(cmp - before - 1) < ticks) { c->cp0[CP0_CAUSE] |= 1ull << 15; c->irq_check = true; }
    c->cp0[CP0_COUNT] = before + ticks;
  }
  uint32_t wired = (uint32_t)c->cp0[CP0_WIRED] & 0x3F;
  if (wired > 31) {
    c->cp0[CP0_RANDOM] = 31;
  } else {
    uint32_t period = 32 - wired, rnd = (uint32_t)c->cp0[CP0_RANDOM];
    uint32_t pos = rnd >= wired && rnd <= 31 ? 31 - rnd : 0;
    c->cp0[CP0_RANDOM] = 31 - (pos + p) % period;
  }
}

static inline bool fetch_execute(N64 *n) {
  Cpu *c = &n->cpu;
  c->cur_pc = c->pc;
  c->cur_delay = c->in_delay;
  c->in_delay = false;
  if (c->pc & 3) {
    address_error(n, c->pc, false);
    return false;
  }
  uint32_t pa, op;
  uint64_t pc = c->pc;
  /* Fast path: KSEG0/KSEG1 code in RDRAM (nearly all code). */
  if ((pc & 0xFFFFFFFFC0000000ull) == 0xFFFFFFFF80000000ull && (pa = (uint32_t)pc & 0x1FFFFFFF) < RDRAM_SIZE) {
    op = rd32be(n->rdram + pa);
  } else {
    if (!translate(n, pc, ACC_FETCH, &pa)) return false;
    bus_read32(n, pa, &op);
  }
  c->pc = c->next_pc;
  c->next_pc += 4;
  execute(n, op);
  return true;
}

/* One instruction with exact timing: the timers advance, then a pending
 * interrupt is taken instead of the instruction. */
void cpu_step(N64 *n) {
  Cpu *c = &n->cpu;
  c->pending++;
  cpu_sync(c);
  c->irq_check = false;
  if (interrupt_pending(c)) {
    c->cur_pc = c->pc;
    c->cur_delay = c->in_delay;
    c->in_delay = false;
    exception(n, EXC_INT, 0x180, 0);
    return;
  }
  fetch_execute(n);
}

/* Instructions until the one whose Count tick reaches Compare (>= 1). */
static uint32_t steps_to_compare(const Cpu *c) {
  uint32_t ticks = (uint32_t)c->cp0[CP0_COMPARE] - (uint32_t)c->cp0[CP0_COUNT];
  if (ticks == 0) ticks = 0xFFFFFFFFu;  /* just passed: a full wrap away */
  uint64_t steps = (uint64_t)ticks * 2 - c->count_frac;
  return steps > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)steps;
}

void cpu_run(N64 *n, uint32_t cycles) {
  Cpu *c = &n->cpu;
  c->irq_check = true;
  uint32_t i = 0;
  while (i < cycles) {
    /* Run freely up to (not including) the step that hits Compare; that
     * one goes through cpu_step so the timer interrupt lands exactly. */
    uint32_t to_cmp = steps_to_compare(c);
    uint32_t free = cycles - i < to_cmp ? cycles - i : to_cmp - 1;
    if (free == 0) {
      cpu_step(n);
      i++;
    }
    for (uint32_t k = 0; k < free; k++) {
      c->pending++;
      i++;
      if (c->irq_check) {
        c->irq_check = false;
        if (interrupt_pending(c)) {
          cpu_sync(c);
          c->cur_pc = c->pc;
          c->cur_delay = c->in_delay;
          c->in_delay = false;
          exception(n, EXC_INT, 0x180, 0);
          break;   /* Status changed: recompute the free run */
        }
      }
      fetch_execute(n);
      if (c->idle) {
        /* Waiting on the RCP: let the rest of the slice pass without
         * executing it, keeping Count (and its Compare interrupt) right. */
        c->idle = false;
        c->poll_count = 0;
        c->pending += cycles - i;
        cpu_sync(c);
        return;
      }
      if (c->pending == 0) break;  /* a COP0 op synced: Compare may have moved */
    }
  }
  cpu_sync(c);
}
