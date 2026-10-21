/*
 * RSP: the Reality Signal Processor. A 32-bit MIPS-like scalar unit running
 * from IMEM, with DMEM for data, COP0 mapped onto the SP/DP registers, and
 * COP2: a vector unit of 32 x 8 x 16-bit registers with 48-bit accumulators.
 */
#include <string.h>

#include "n64.h"

#define RS(op) (((op) >> 21) & 31)
#define RT(op) (((op) >> 16) & 31)
#define RD(op) (((op) >> 11) & 31)
#define SA(op) (((op) >> 6) & 31)

/* ------------------------------------------------------------------ */
/* DMEM (big-endian, addresses wrap at 4 KB)                            */

static inline uint8_t dm8(N64 *n, uint32_t a) { return n->dmem[a & 0xFFF]; }
static inline void dm8w(N64 *n, uint32_t a, uint8_t v) { n->dmem[a & 0xFFF] = v; }
static inline uint16_t dm16(N64 *n, uint32_t a) { return (uint16_t)(dm8(n, a) << 8 | dm8(n, a + 1)); }
static inline uint32_t dm32(N64 *n, uint32_t a) { return (uint32_t)dm16(n, a) << 16 | dm16(n, a + 2); }
static inline void dm16w(N64 *n, uint32_t a, uint16_t v) { dm8w(n, a, (uint8_t)(v >> 8)); dm8w(n, a + 1, (uint8_t)v); }
static inline void dm32w(N64 *n, uint32_t a, uint32_t v) { dm16w(n, a, (uint16_t)(v >> 16)); dm16w(n, a + 2, (uint16_t)v); }

/* ------------------------------------------------------------------ */
/* Vector register bytes (element 0 holds bytes 0..1, big-endian)       */

static inline uint8_t vb(Rsp *r, uint32_t v, uint32_t i) {
  i &= 15;
  uint16_t e = r->vr[v][i >> 1];
  return (i & 1) ? (uint8_t)e : (uint8_t)(e >> 8);
}
static inline void vbw(Rsp *r, uint32_t v, uint32_t i, uint8_t b) {
  i &= 15;
  uint16_t *e = &r->vr[v][i >> 1];
  *e = (i & 1) ? (uint16_t)((*e & 0xFF00) | b) : (uint16_t)((*e & 0x00FF) | b << 8);
}

/* Element selector for the vt operand. */
static inline uint32_t sel(uint32_t e, uint32_t i) {
  if (e < 2) return i;
  if (e < 4) return (i & ~1u) | (e & 1);
  if (e < 8) return (i & ~3u) | (e & 3);
  return e & 7;
}

/* ------------------------------------------------------------------ */
/* Accumulator helpers                                                  */

static inline int64_t sx48(int64_t v) { return (int64_t)((uint64_t)v << 16) >> 16; }
static inline uint16_t acc_h(int64_t a) { return (uint16_t)(a >> 32); }
static inline uint16_t acc_m(int64_t a) { return (uint16_t)(a >> 16); }
static inline uint16_t acc_l(int64_t a) { return (uint16_t)a; }
static inline void set_acc_l(Rsp *r, int i, uint16_t v) { r->acc[i] = (r->acc[i] & ~0xFFFFll) | v; }

static inline uint16_t clamp16(int64_t v) { return (uint16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
/* Signed clamp of acc[47:16]. */
static inline uint16_t clamp_mid(int64_t a) { return clamp16(a >> 16); }
/* acc[15:0] if the accumulator fits in signed 32 bits, else saturate. */
static inline uint16_t clamp_low(int64_t a) {
  int64_t hi = a >> 16;
  if (hi < -32768) return 0x0000;
  if (hi > 32767) return 0xFFFF;
  return acc_l(a);
}
/* Unsigned clamp of acc[47:16] (VMULU / VMACU). */
static inline uint16_t clamp_unsigned(int64_t a) {
  int64_t v = a >> 16;
  if (v < 0) return 0;
  if (v > 32767) return 0xFFFF;
  return (uint16_t)v;
}

/* ------------------------------------------------------------------ */
/* Reciprocal / inverse square root (ROM tables built from their formulas) */

static uint16_t rcp_rom[512], rsq_rom[512];
static bool tables_ready;

static void build_tables(void) {
  for (uint32_t i = 0; i < 512; i++) {
    uint64_t b = (1ull << 34) / (i + 512);
    rcp_rom[i] = (uint16_t)((b + 1) >> 8);
  }
  for (uint32_t i = 0; i < 512; i++) {
    uint64_t a = (i + 512) >> ((i & 1) ? 1 : 0);
    uint64_t b = 1ull << 17;
    while (a * (b + 1) * (b + 1) < (1ull << 44)) b++;
    rsq_rom[i] = (uint16_t)(b >> 1);
  }
  tables_ready = true;
}

static uint32_t clz32(uint32_t v) {
  uint32_t c = 0;
  if (!v) return 32;
  while (!(v & 0x80000000u)) { v <<= 1; c++; }
  return c;
}

static uint32_t reciprocal(int32_t input, bool sqrt) {
  if (input == 0) return 0x7FFFFFFFu;
  if (input == -32768) return 0xFFFF0000u;
  uint32_t mask = (uint32_t)(input >> 31);
  uint32_t data = (uint32_t)input ^ mask;
  if (input > -32768) data -= mask;
  if (data == 0) return 0x7FFFFFFFu;
  uint32_t shift = clz32(data);
  uint32_t index = (uint32_t)((((uint64_t)data << shift) & 0x7FC00000u) >> 22);
  uint32_t result;
  if (!sqrt) {
    result = (0x10000u | rcp_rom[index]) << 14;
    result = (result >> (31 - shift)) ^ mask;
  } else {
    index = (((index | 0x200) & 0x3FE) | (shift & 1)) & 0x1FF;
    result = (0x10000u | rsq_rom[index]) << 14;
    result = (result >> ((31 - shift) >> 1)) ^ mask;
  }
  return result;
}

/* ------------------------------------------------------------------ */
/* Vector operations                                                    */

static void vector_op(N64 *n, uint32_t op) {
  Rsp *r = &n->rsp;
  uint32_t e = (op >> 21) & 15, vt = RT(op), vs = RD(op), vd = SA(op);
  uint16_t s[8], t[8], d[8];
  for (int i = 0; i < 8; i++) { s[i] = r->vr[vs][i]; t[i] = r->vr[vt][sel(e, (uint32_t)i)]; }

  switch (op & 63) {
    case 0x00: /* VMULF */
    case 0x01: /* VMULU */
      for (int i = 0; i < 8; i++) {
        int64_t a = (int64_t)(int16_t)s[i] * (int16_t)t[i] * 2 + 0x8000;
        r->acc[i] = sx48(a);
        d[i] = (op & 1) ? clamp_unsigned(r->acc[i]) : clamp_mid(r->acc[i]);
      }
      break;
    case 0x02: /* VRNDP */
    case 0x0A: /* VRNDN */
      for (int i = 0; i < 8; i++) {
        int64_t p = (int16_t)t[i];
        if (vs & 1) p <<= 16;
        int64_t a = r->acc[i];
        bool apply = (op & 63) == 0x02 ? a >= 0 : a < 0;
        if (apply) a = sx48(a + p);
        r->acc[i] = a;
        d[i] = clamp_mid(a);
      }
      break;
    case 0x03: /* VMULQ */
      for (int i = 0; i < 8; i++) {
        int32_t p = (int16_t)s[i] * (int16_t)t[i];
        if (p < 0) p += 31;
        r->acc[i] = sx48((int64_t)p << 16);
        d[i] = (uint16_t)(clamp16(p >> 1) & ~15);
      }
      break;
    case 0x04: /* VMUDL */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = (int64_t)(((uint32_t)s[i] * t[i]) >> 16);
        d[i] = clamp_low(r->acc[i]);
      }
      break;
    case 0x05: /* VMUDM */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48((int64_t)(int16_t)s[i] * (int64_t)t[i]);
        d[i] = clamp_mid(r->acc[i]);
      }
      break;
    case 0x06: /* VMUDN */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48((int64_t)s[i] * (int16_t)t[i]);
        d[i] = clamp_low(r->acc[i]);
      }
      break;
    case 0x07: /* VMUDH */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48((int64_t)((int16_t)s[i] * (int16_t)t[i]) << 16);
        d[i] = clamp_mid(r->acc[i]);
      }
      break;
    case 0x08: /* VMACF */
    case 0x09: /* VMACU */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48(r->acc[i] + (int64_t)(int16_t)s[i] * (int16_t)t[i] * 2);
        d[i] = (op & 1) ? clamp_unsigned(r->acc[i]) : clamp_mid(r->acc[i]);
      }
      break;
    case 0x0B: /* VMACQ */
      for (int i = 0; i < 8; i++) {
        int32_t p = (int32_t)(r->acc[i] >> 16);
        if (p < 0 && !(p & (1 << 5))) p += 32;
        else if (p >= 32 && !(p & (1 << 5))) p -= 32;
        r->acc[i] = sx48(((int64_t)p << 16) | acc_l(r->acc[i]));
        d[i] = (uint16_t)(clamp16(p >> 1) & ~15);
      }
      break;
    case 0x0C: /* VMADL */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48(r->acc[i] + (int64_t)(((uint32_t)s[i] * t[i]) >> 16));
        d[i] = clamp_low(r->acc[i]);
      }
      break;
    case 0x0D: /* VMADM */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48(r->acc[i] + (int64_t)(int16_t)s[i] * (int64_t)t[i]);
        d[i] = clamp_mid(r->acc[i]);
      }
      break;
    case 0x0E: /* VMADN */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48(r->acc[i] + (int64_t)s[i] * (int16_t)t[i]);
        d[i] = clamp_low(r->acc[i]);
      }
      break;
    case 0x0F: /* VMADH */
      for (int i = 0; i < 8; i++) {
        r->acc[i] = sx48(r->acc[i] + ((int64_t)((int16_t)s[i] * (int16_t)t[i]) << 16));
        d[i] = clamp_mid(r->acc[i]);
      }
      break;
    case 0x10: /* VADD */
      for (int i = 0; i < 8; i++) {
        int32_t v = (int16_t)s[i] + (int16_t)t[i] + ((r->vco >> i) & 1);
        set_acc_l(r, i, (uint16_t)v);
        d[i] = clamp16(v);
      }
      r->vco = 0;
      break;
    case 0x11: /* VSUB */
      for (int i = 0; i < 8; i++) {
        int32_t v = (int16_t)s[i] - (int16_t)t[i] - ((r->vco >> i) & 1);
        set_acc_l(r, i, (uint16_t)v);
        d[i] = clamp16(v);
      }
      r->vco = 0;
      break;
    case 0x13: /* VABS */
      for (int i = 0; i < 8; i++) {
        int16_t a = (int16_t)s[i], b = (int16_t)t[i];
        if (a < 0) {
          set_acc_l(r, i, (uint16_t)-b);
          d[i] = b == -32768 ? 0x7FFF : (uint16_t)-b;
        } else if (a == 0) {
          set_acc_l(r, i, 0);
          d[i] = 0;
        } else {
          set_acc_l(r, i, (uint16_t)b);
          d[i] = (uint16_t)b;
        }
      }
      break;
    case 0x14: /* VADDC */
      r->vco = 0;
      for (int i = 0; i < 8; i++) {
        uint32_t v = (uint32_t)s[i] + t[i];
        set_acc_l(r, i, (uint16_t)v);
        d[i] = (uint16_t)v;
        if (v >> 16) r->vco |= (uint16_t)(1 << i);
      }
      break;
    case 0x15: /* VSUBC */
      r->vco = 0;
      for (int i = 0; i < 8; i++) {
        int32_t v = (int32_t)s[i] - (int32_t)t[i];
        set_acc_l(r, i, (uint16_t)v);
        d[i] = (uint16_t)v;
        if (v < 0) r->vco |= (uint16_t)(1 << i);
        if (v != 0) r->vco |= (uint16_t)(0x100 << i);
      }
      break;
    case 0x1D: /* VSAR */
      for (int i = 0; i < 8; i++) {
        switch (e) {
          case 8: d[i] = acc_h(r->acc[i]); break;
          case 9: d[i] = acc_m(r->acc[i]); break;
          case 10: d[i] = acc_l(r->acc[i]); break;
          default: d[i] = 0; break;
        }
      }
      break;
    case 0x20: /* VLT */
    case 0x21: /* VEQ */
    case 0x22: /* VNE */
    case 0x23: /* VGE */
      r->vcc = 0;
      for (int i = 0; i < 8; i++) {
        int16_t a = (int16_t)s[i], b = (int16_t)t[i];
        bool carry = (r->vco >> i) & 1, ne = (r->vco >> (8 + i)) & 1, c;
        switch (op & 63) {
          case 0x20: c = a < b || (a == b && carry && ne); break;
          case 0x21: c = a == b && !ne; break;
          case 0x22: c = a != b || ne; break;
          default: c = a > b || (a == b && !(carry && ne)); break;
        }
        if (c) r->vcc |= (uint16_t)(1 << i);
        d[i] = c ? s[i] : t[i];
        if ((op & 63) == 0x21 || (op & 63) == 0x22) d[i] = c ? s[i] : t[i];
        set_acc_l(r, i, d[i]);
      }
      r->vco = 0;
      break;
    case 0x24: { /* VCL */
      uint16_t vcc = 0;
      for (int i = 0; i < 8; i++) {
        uint16_t a = s[i], b = t[i];
        bool carry = (r->vco >> i) & 1, ne = (r->vco >> (8 + i)) & 1, ce = (r->vce >> i) & 1;
        bool le = (r->vcc >> i) & 1, ge = (r->vcc >> (8 + i)) & 1;
        if (carry) {
          if (!ne) {
            uint32_t sum = (uint32_t)a + b;
            bool lz = (sum & 0xFFFF) == 0, co = (sum >> 16) != 0;
            le = ce ? (lz || !co) : (lz && !co);
          }
          d[i] = le ? (uint16_t)-b : a;
        } else {
          if (!ne) ge = a >= b;
          d[i] = ge ? b : a;
        }
        if (le) vcc |= (uint16_t)(1 << i);
        if (ge) vcc |= (uint16_t)(0x100 << i);
        set_acc_l(r, i, d[i]);
      }
      r->vcc = vcc;
      r->vco = 0;
      r->vce = 0;
      break;
    }
    case 0x25: { /* VCH */
      uint16_t vcc = 0, vco = 0;
      uint8_t vce = 0;
      for (int i = 0; i < 8; i++) {
        int32_t a = (int16_t)s[i], b = (int16_t)t[i];
        bool sign = (a ^ b) < 0, le, ge, ce = false, eq;
        if (sign) {
          int32_t sum = a + b;
          ge = b < 0;
          le = sum <= 0;
          ce = sum == -1;
          eq = sum == 0 || ce;
          d[i] = le ? (uint16_t)-b : (uint16_t)a;
        } else {
          int32_t diff = a - b;
          le = b < 0;
          ge = diff >= 0;
          eq = diff == 0;
          d[i] = ge ? (uint16_t)b : (uint16_t)a;
        }
        if (le) vcc |= (uint16_t)(1 << i);
        if (ge) vcc |= (uint16_t)(0x100 << i);
        if (sign) vco |= (uint16_t)(1 << i);
        if (!eq) vco |= (uint16_t)(0x100 << i);
        if (ce) vce |= (uint8_t)(1 << i);
        set_acc_l(r, i, d[i]);
      }
      r->vcc = vcc;
      r->vco = vco;
      r->vce = vce;
      break;
    }
    case 0x26: { /* VCR */
      uint16_t vcc = 0;
      for (int i = 0; i < 8; i++) {
        int32_t a = (int16_t)s[i], b = (int16_t)t[i];
        bool sign = (a ^ b) < 0, le, ge;
        if (sign) {
          ge = b < 0;
          le = a + b < 0;
          d[i] = le ? (uint16_t)~b : (uint16_t)a;
        } else {
          le = b < 0;
          ge = a - b >= 0;
          d[i] = ge ? (uint16_t)b : (uint16_t)a;
        }
        if (le) vcc |= (uint16_t)(1 << i);
        if (ge) vcc |= (uint16_t)(0x100 << i);
        set_acc_l(r, i, d[i]);
      }
      r->vcc = vcc;
      r->vco = 0;
      r->vce = 0;
      break;
    }
    case 0x27: /* VMRG */
      for (int i = 0; i < 8; i++) {
        d[i] = ((r->vcc >> i) & 1) ? s[i] : t[i];
        set_acc_l(r, i, d[i]);
      }
      r->vco = 0;
      break;
    case 0x28: for (int i = 0; i < 8; i++) { d[i] = s[i] & t[i]; set_acc_l(r, i, d[i]); } break;               /* VAND */
    case 0x29: for (int i = 0; i < 8; i++) { d[i] = (uint16_t)~(s[i] & t[i]); set_acc_l(r, i, d[i]); } break;  /* VNAND */
    case 0x2A: for (int i = 0; i < 8; i++) { d[i] = s[i] | t[i]; set_acc_l(r, i, d[i]); } break;               /* VOR */
    case 0x2B: for (int i = 0; i < 8; i++) { d[i] = (uint16_t)~(s[i] | t[i]); set_acc_l(r, i, d[i]); } break;  /* VNOR */
    case 0x2C: for (int i = 0; i < 8; i++) { d[i] = s[i] ^ t[i]; set_acc_l(r, i, d[i]); } break;               /* VXOR */
    case 0x2D: for (int i = 0; i < 8; i++) { d[i] = (uint16_t)~(s[i] ^ t[i]); set_acc_l(r, i, d[i]); } break;  /* VNXOR */
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: {
      /* VRCP, VRCPL, VRCPH, VMOV, VRSQ, VRSQL, VRSQH: scalar element ops */
      uint32_t de = vs & 7;
      for (int i = 0; i < 8; i++) set_acc_l(r, i, t[i]);
      memcpy(d, r->vr[vd], sizeof d);
      uint16_t in = r->vr[vt][e & 7];
      switch (op & 63) {
        case 0x30: case 0x34: { /* VRCP / VRSQ */
          uint32_t res = reciprocal((int16_t)in, (op & 63) == 0x34);
          d[de] = (uint16_t)res;
          r->div_out = (int32_t)(res >> 16);
          r->div_dp = false;
          break;
        }
        case 0x31: case 0x35: { /* VRCPL / VRSQL */
          int32_t input = r->div_dp ? (int32_t)((uint32_t)r->div_in << 16 | in) : (int16_t)in;
          uint32_t res = reciprocal(input, (op & 63) == 0x35);
          d[de] = (uint16_t)res;
          r->div_out = (int32_t)(res >> 16);
          r->div_dp = false;
          break;
        }
        case 0x32: case 0x36: /* VRCPH / VRSQH */
          r->div_in = in;
          r->div_dp = true;
          d[de] = (uint16_t)r->div_out;
          break;
        case 0x33: /* VMOV */
          d[de] = r->vr[vt][sel(e, de)];
          break;
      }
      break;
    }
    case 0x37: /* VNOP */
    case 0x3F: /* VNULL */
      return;
    default:
      /* Reserved: the adder still runs into the accumulator, VD reads zero. */
      for (int i = 0; i < 8; i++) {
        set_acc_l(r, i, (uint16_t)(s[i] + t[i]));
        d[i] = 0;
      }
      break;
  }
  memcpy(r->vr[vd], d, sizeof d);
}

/* ------------------------------------------------------------------ */
/* Vector loads and stores (LWC2 / SWC2)                                */

static void vload(N64 *n, uint32_t op) {
  Rsp *r = &n->rsp;
  uint32_t base = RS(op), vt = RT(op), kind = RD(op), e = (op >> 7) & 15;
  int32_t off = (int32_t)(op << 25) >> 25; /* signed 7 bits */
  static const uint8_t scale[12] = {0, 1, 2, 3, 4, 4, 3, 3, 4, 4, 4, 4};
  if (kind > 11) return;
  uint32_t addr = r->r[base] + (uint32_t)(off << scale[kind]);
  switch (kind) {
    case 0: case 1: case 2: case 3: { /* LBV LSV LLV LDV */
      uint32_t size = 1u << kind;
      for (uint32_t i = 0; i < size && e + i < 16; i++) vbw(r, vt, e + i, dm8(n, addr + i));
      break;
    }
    case 4: { /* LQV */
      uint32_t end = (addr & ~15u) + 16;
      for (uint32_t i = 0; addr + i < end && e + i < 16; i++) vbw(r, vt, e + i, dm8(n, addr + i));
      break;
    }
    case 5: { /* LRV */
      uint32_t o = addr & 15, start = addr & ~15u;
      for (uint32_t i = 0; i < o; i++) {
        uint32_t dst = e + (16 - o) + i;
        if (dst < 16) vbw(r, vt, dst, dm8(n, start + i));
      }
      break;
    }
    case 6: case 7: { /* LPV LUV: bytes into the upper bits of each element */
      uint32_t start = addr & ~7u, o = (addr & 7) - e;
      for (uint32_t i = 0; i < 8; i++) {
        uint8_t b = dm8(n, start + ((o + i) & 15));
        r->vr[vt][i] = kind == 6 ? (uint16_t)(b << 8) : (uint16_t)(b << 7);
      }
      break;
    }
    case 8: { /* LHV */
      uint32_t start = addr & ~7u, o = (addr & 7) - e;
      for (uint32_t i = 0; i < 8; i++) r->vr[vt][i] = (uint16_t)(dm8(n, start + ((o + i * 2) & 15)) << 7);
      break;
    }
    case 9: { /* LFV */
      uint32_t start = addr & ~7u, o = (addr & 7) - e;
      uint16_t tmp[8];
      memcpy(tmp, r->vr[vt], sizeof tmp);
      for (uint32_t i = 0; i < 4; i++) {
        tmp[i] = (uint16_t)(dm8(n, start + ((o + i * 4) & 15)) << 7);
        tmp[i + 4] = (uint16_t)(dm8(n, start + ((o + i * 4 + 8) & 15)) << 7);
      }
      for (uint32_t i = e; i < e + 8 && i < 16; i++) vbw(r, vt, i, (uint8_t)(tmp[i >> 1] >> ((i & 1) ? 0 : 8)));
      break;
    }
    case 10: /* LWV: no effect on the RSP */
      break;
    case 11: { /* LTV: transposed load into 8 registers */
      uint32_t vbase = vt & ~7u, start = addr & ~7u, o = (addr & 8);
      for (uint32_t i = 0; i < 8; i++) {
        uint32_t reg = vbase + ((i + (e >> 1)) & 7);
        uint32_t idx = (o + i * 2 + e) & 15;
        uint16_t v = (uint16_t)(dm8(n, start + idx) << 8 | dm8(n, start + ((idx + 1) & 15)));
        r->vr[reg][i] = v;
      }
      break;
    }
  }
}

static void vstore(N64 *n, uint32_t op) {
  Rsp *r = &n->rsp;
  uint32_t base = RS(op), vt = RT(op), kind = RD(op), e = (op >> 7) & 15;
  int32_t off = (int32_t)(op << 25) >> 25;
  static const uint8_t scale[12] = {0, 1, 2, 3, 4, 4, 3, 3, 4, 4, 4, 4};
  if (kind > 11) return;
  uint32_t addr = r->r[base] + (uint32_t)(off << scale[kind]);
  switch (kind) {
    case 0: case 1: case 2: case 3: { /* SBV SSV SLV SDV */
      uint32_t size = 1u << kind;
      for (uint32_t i = 0; i < size; i++) dm8w(n, addr + i, vb(r, vt, e + i));
      break;
    }
    case 4: { /* SQV */
      uint32_t end = (addr & ~15u) + 16;
      for (uint32_t i = 0; addr + i < end; i++) dm8w(n, addr + i, vb(r, vt, e + i));
      break;
    }
    case 5: { /* SRV */
      uint32_t o = addr & 15, start = addr & ~15u;
      for (uint32_t i = 0; i < o; i++) dm8w(n, start + i, vb(r, vt, e + (16 - o) + i));
      break;
    }
    case 6: case 7: { /* SPV SUV */
      for (uint32_t i = 0; i < 8; i++) {
        uint32_t idx = (e + i) & 15;
        uint16_t el = r->vr[vt][idx & 7];
        bool upper = (idx < 8) == (kind == 6);
        dm8w(n, addr + i, upper ? (uint8_t)(el >> 8) : (uint8_t)(el >> 7));
      }
      break;
    }
    case 8: { /* SHV */
      uint32_t start = addr & ~7u, o = addr & 7;
      for (uint32_t i = 0; i < 8; i++) {
        uint32_t idx = (e + i * 2) & 15;
        uint8_t v = (uint8_t)(vb(r, vt, idx) << 1 | vb(r, vt, idx + 1) >> 7);
        dm8w(n, start + ((o + i * 2) & 15), v);
      }
      break;
    }
    case 9: { /* SFV */
      uint32_t start = addr & ~7u, o = addr & 7;
      static const uint8_t lanes[16][4] = {
          {0, 1, 2, 3}, {6, 7, 4, 5}, {1, 2, 3, 0}, {7, 4, 5, 6}, {2, 3, 0, 1}, {4, 5, 6, 7}, {3, 0, 1, 2}, {5, 6, 7, 4},
          {0, 1, 2, 3}, {6, 7, 4, 5}, {1, 2, 3, 0}, {7, 4, 5, 6}, {2, 3, 0, 1}, {4, 5, 6, 7}, {3, 0, 1, 2}, {5, 6, 7, 4}};
      for (uint32_t i = 0; i < 4; i++)
        dm8w(n, start + ((o + i * 4) & 15), (uint8_t)(r->vr[vt][lanes[e][i]] >> 7));
      break;
    }
    case 10: { /* SWV */
      uint32_t start = addr & ~7u, o = addr & 7;
      for (uint32_t i = 0; i < 16; i++) dm8w(n, start + ((o + i) & 15), vb(r, vt, e + i));
      break;
    }
    case 11: { /* STV: transposed store from 8 registers */
      uint32_t vbase = vt & ~7u, start = addr & ~7u, o = addr & 7;
      for (uint32_t i = 0; i < 8; i++) {
        uint32_t reg = vbase + ((i + (e >> 1)) & 7);
        uint16_t v = r->vr[reg][i];
        uint32_t idx = (o + i * 2) & 15;
        dm8w(n, start + idx, (uint8_t)(v >> 8));
        dm8w(n, start + ((idx + 1) & 15), (uint8_t)v);
      }
      break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* COP0: SP and DP registers                                            */

static uint32_t cop0_addr(uint32_t rd) { return rd < 8 ? 0x04040000u + rd * 4 : 0x04100000u + (rd & 7) * 4; }

static void rsp_break(N64 *n) {
  n->sp.status |= 3; /* halted + broke */
  if (n->sp.status & 0x40) n64_raise_mi(n, MI_INTR_SP);
}

/* ------------------------------------------------------------------ */
/* Scalar unit                                                          */

#define W(rr, v) do { if (rr) r->r[rr] = (v); } while (0)

static inline void branch(Rsp *r, bool taken, uint32_t target) {
  if (taken) r->next_pc = target & 0xFFC;
  r->in_delay = true;
}

static void rsp_step(N64 *n) {
  Rsp *r = &n->rsp;
  uint32_t pc = r->pc;
  uint32_t op = rd32be(n->imem + (pc & 0xFFC));
  bool was_delay = r->in_delay;
  r->in_delay = false;
  r->pc = r->next_pc;
  r->next_pc = (r->next_pc + 4) & 0xFFC;
  (void)was_delay;

  uint32_t rs = RS(op), rt = RT(op), rd = RD(op), sa = SA(op);
  uint32_t s = r->r[rs], t = r->r[rt];
  int32_t simm = (int16_t)op;
  uint32_t addr = s + (uint32_t)simm;
  uint32_t btarget = pc + 4 + (uint32_t)(simm << 2);

  switch (op >> 26) {
    case 0x00:
      switch (op & 63) {
        case 0x00: W(rd, t << sa); break;
        case 0x02: W(rd, t >> sa); break;
        case 0x03: W(rd, (uint32_t)((int32_t)t >> sa)); break;
        case 0x04: W(rd, t << (s & 31)); break;
        case 0x06: W(rd, t >> (s & 31)); break;
        case 0x07: W(rd, (uint32_t)((int32_t)t >> (s & 31))); break;
        case 0x08: branch(r, true, s); break;
        case 0x09: { uint32_t link = (pc + 8) & 0xFFC; branch(r, true, s); W(rd, link); break; }
        case 0x0D: rsp_break(n); break;
        case 0x20: case 0x21: W(rd, s + t); break;
        case 0x22: case 0x23: W(rd, s - t); break;
        case 0x24: W(rd, s & t); break;
        case 0x25: W(rd, s | t); break;
        case 0x26: W(rd, s ^ t); break;
        case 0x27: W(rd, ~(s | t)); break;
        case 0x2A: W(rd, (int32_t)s < (int32_t)t); break;
        case 0x2B: W(rd, s < t); break;
        default: break;
      }
      break;
    case 0x01:
      switch (rt) {
        case 0x00: branch(r, (int32_t)s < 0, btarget); break;
        case 0x01: branch(r, (int32_t)s >= 0, btarget); break;
        case 0x10: r->r[31] = (pc + 8) & 0xFFC; branch(r, (int32_t)s < 0, btarget); break;
        case 0x11: r->r[31] = (pc + 8) & 0xFFC; branch(r, (int32_t)s >= 0, btarget); break;
      }
      break;
    case 0x02: branch(r, true, (op & 0x3FF) << 2); break;
    case 0x03: r->r[31] = (pc + 8) & 0xFFC; branch(r, true, (op & 0x3FF) << 2); break;
    case 0x04: branch(r, s == t, btarget); break;
    case 0x05: branch(r, s != t, btarget); break;
    case 0x06: branch(r, (int32_t)s <= 0, btarget); break;
    case 0x07: branch(r, (int32_t)s > 0, btarget); break;
    case 0x08: case 0x09: W(rt, s + (uint32_t)simm); break;
    case 0x0A: W(rt, (int32_t)s < simm); break;
    case 0x0B: W(rt, s < (uint32_t)simm); break;
    case 0x0C: W(rt, s & (uint16_t)op); break;
    case 0x0D: W(rt, s | (uint16_t)op); break;
    case 0x0E: W(rt, s ^ (uint16_t)op); break;
    case 0x0F: W(rt, (uint32_t)(uint16_t)op << 16); break;
    case 0x10: /* COP0 */
      if (rs == 0x00) W(rt, rcp_reg_read(n, cop0_addr(rd & 15)));
      else if (rs == 0x04) rcp_reg_write(n, cop0_addr(rd & 15), t);
      break;
    case 0x12: { /* COP2 */
      if (op & (1u << 25)) { vector_op(n, op); break; }
      uint32_t e = (op >> 7) & 15;
      switch (rs) {
        case 0x00: /* MFC2 */
          W(rt, (uint32_t)(int32_t)(int16_t)(vb(r, rd, e) << 8 | vb(r, rd, e + 1)));
          break;
        case 0x02: { /* CFC2 */
          uint32_t v = (rd & 3) == 0 ? r->vco : (rd & 3) == 1 ? r->vcc : r->vce;
          W(rt, (uint32_t)(int32_t)(int16_t)v);
          break;
        }
        case 0x04: /* MTC2 */
          vbw(r, rd, e, (uint8_t)(t >> 8));
          if (e + 1 < 16) vbw(r, rd, e + 1, (uint8_t)t);
          break;
        case 0x06: /* CTC2 */
          if ((rd & 3) == 0) r->vco = (uint16_t)t;
          else if ((rd & 3) == 1) r->vcc = (uint16_t)t;
          else r->vce = (uint8_t)t;
          break;
      }
      break;
    }
    case 0x20: W(rt, (uint32_t)(int32_t)(int8_t)dm8(n, addr)); break;
    case 0x21: W(rt, (uint32_t)(int32_t)(int16_t)dm16(n, addr)); break;
    case 0x23: case 0x27: W(rt, dm32(n, addr)); break;
    case 0x24: W(rt, dm8(n, addr)); break;
    case 0x25: W(rt, dm16(n, addr)); break;
    case 0x28: dm8w(n, addr, (uint8_t)t); break;
    case 0x29: dm16w(n, addr, (uint16_t)t); break;
    case 0x2B: dm32w(n, addr, t); break;
    case 0x32: vload(n, op); break;
    case 0x3A: vstore(n, op); break;
    default: break;
  }
  r->r[0] = 0;
}

void rsp_run(N64 *n, uint32_t cycles) {
  if (!tables_ready) build_tables();
  for (uint32_t i = 0; i < cycles; i++) {
    if (n->sp.status & 1) return; /* halted */
    rsp_step(n);
    if (n->sp.status & 0x20) { n->sp.status |= 1; return; } /* single step */
  }
}
