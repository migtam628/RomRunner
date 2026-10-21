/*
 * VR4300 floating-point unit (COP1): single/double precision arithmetic,
 * conversions, compares, and the FR=0 / FR=1 register models.
 */
#include <fenv.h>
#include <math.h>
#include <string.h>

#include "n64.h"

void cpu_fpu_exception(N64 *n);
void cpu_reserved(N64 *n);

#define FT(op) (((op) >> 16) & 31)
#define FS(op) (((op) >> 11) & 31)
#define FD(op) (((op) >> 6) & 31)

#define FCR31_C (1u << 23)

/* ---- register file ---- */

static inline bool fr(N64 *n) { return (n->cpu.cp0[CP0_STATUS] & ST_FR) != 0; }

static uint32_t get32(N64 *n, uint32_t r) {
  if (fr(n)) return (uint32_t)n->cpu.fgr[r];
  uint64_t v = n->cpu.fgr[r & ~1u];
  return (r & 1) ? (uint32_t)(v >> 32) : (uint32_t)v;
}

static void set32(N64 *n, uint32_t r, uint32_t v) {
  if (fr(n)) {
    n->cpu.fgr[r] = (n->cpu.fgr[r] & 0xFFFFFFFF00000000ull) | v;
    return;
  }
  uint64_t *p = &n->cpu.fgr[r & ~1u];
  if (r & 1) *p = (*p & 0xFFFFFFFFull) | (uint64_t)v << 32;
  else *p = (*p & 0xFFFFFFFF00000000ull) | v;
}

static uint64_t get64(N64 *n, uint32_t r) { return n->cpu.fgr[fr(n) ? r : (r & ~1u)]; }
static void set64(N64 *n, uint32_t r, uint64_t v) { n->cpu.fgr[fr(n) ? r : (r & ~1u)] = v; }

static float getf(N64 *n, uint32_t r) { uint32_t b = get32(n, r); float f; memcpy(&f, &b, 4); return f; }
static double getd(N64 *n, uint32_t r) { uint64_t b = get64(n, r); double d; memcpy(&d, &b, 8); return d; }
static void setf(N64 *n, uint32_t r, float f) { uint32_t b; memcpy(&b, &f, 4); set32(n, r, b); }
static void setd(N64 *n, uint32_t r, double d) { uint64_t b; memcpy(&b, &d, 8); set64(n, r, b); }

void fpu_lwc1(N64 *n, uint32_t ft, uint32_t v) { set32(n, ft, v); }
void fpu_ldc1(N64 *n, uint32_t ft, uint64_t v) { set64(n, ft, v); }
uint32_t fpu_swc1(N64 *n, uint32_t ft) { return get32(n, ft); }
uint64_t fpu_sdc1(N64 *n, uint32_t ft) { return get64(n, ft); }

/* ---- rounding ---- */

static int host_mode(uint32_t fcr31) {
  switch (fcr31 & 3) {
    case 1: return FE_TOWARDZERO;
    case 2: return FE_UPWARD;
    case 3: return FE_DOWNWARD;
    default: return FE_TONEAREST;
  }
}

/* Integer conversion with an explicit rounding style. */
typedef enum { R_CURRENT, R_NEAREST, R_TRUNC, R_CEIL, R_FLOOR } Round;

static double round_as(double v, Round r, uint32_t fcr31) {
  switch (r) {
    case R_TRUNC: return trunc(v);
    case R_CEIL: return ceil(v);
    case R_FLOOR: return floor(v);
    case R_NEAREST: {
      int old = fegetround();
      fesetround(FE_TONEAREST);
      double x = nearbyint(v);
      fesetround(old);
      return x;
    }
    default: {
      int old = fegetround();
      fesetround(host_mode(fcr31));
      double x = nearbyint(v);
      fesetround(old);
      return x;
    }
  }
}

/* Out-of-range and NaN conversions raise "unimplemented operation" on the
 * VR4300; games don't rely on it, so produce the saturated value it would
 * otherwise write and set the cause bit. */
static int32_t to_w(N64 *n, double v, Round r) {
  double x = round_as(v, r, n->cpu.fcr31);
  if (isnan(x) || x >= 2147483648.0 || x < -2147483648.0) {
    n->cpu.fcr31 |= 1u << 17;
    return INT32_MAX;
  }
  if (x != v) n->cpu.fcr31 |= (1u << 12) | (1u << 2); /* inexact cause + flag */
  return (int32_t)x;
}

static int64_t to_l(N64 *n, double v, Round r) {
  double x = round_as(v, r, n->cpu.fcr31);
  if (isnan(x) || x >= 9223372036854775808.0 || x < -9223372036854775808.0) {
    n->cpu.fcr31 |= 1u << 17;
    return INT64_MAX;
  }
  if (x != v) n->cpu.fcr31 |= (1u << 12) | (1u << 2);
  return (int64_t)x;
}

/* ---- compares ---- */

static void compare(N64 *n, double a, double b, uint32_t cond) {
  bool unordered = isnan(a) || isnan(b);
  bool r = false;
  if (unordered) r = (cond & 1) != 0;
  else r = ((cond & 4) && a < b) || ((cond & 2) && a == b);
  if (r) n->cpu.fcr31 |= FCR31_C;
  else n->cpu.fcr31 &= ~FCR31_C;
}

/* ---- arithmetic in the guest rounding mode ---- */

#define WITH_MODE(stmt)                         \
  do {                                          \
    int old_ = fegetround();                    \
    fesetround(host_mode(n->cpu.fcr31));        \
    stmt;                                       \
    fesetround(old_);                           \
  } while (0)

static void op_s(N64 *n, uint32_t op) {
  uint32_t fs = FS(op), ft = FT(op), fd = FD(op);
  float a = getf(n, fs), b = getf(n, ft);
  switch (op & 63) {
    case 0x00: WITH_MODE(setf(n, fd, a + b)); break;
    case 0x01: WITH_MODE(setf(n, fd, a - b)); break;
    case 0x02: WITH_MODE(setf(n, fd, a * b)); break;
    case 0x03: WITH_MODE(setf(n, fd, a / b)); break;
    case 0x04: WITH_MODE(setf(n, fd, sqrtf(a))); break;
    case 0x05: setf(n, fd, fabsf(a)); break;
    case 0x06: set32(n, fd, get32(n, fs)); break;   /* MOV keeps the exact bits */
    case 0x07: setf(n, fd, -a); break;
    case 0x08: set64(n, fd, (uint64_t)to_l(n, a, R_NEAREST)); break;
    case 0x09: set64(n, fd, (uint64_t)to_l(n, a, R_TRUNC)); break;
    case 0x0A: set64(n, fd, (uint64_t)to_l(n, a, R_CEIL)); break;
    case 0x0B: set64(n, fd, (uint64_t)to_l(n, a, R_FLOOR)); break;
    case 0x0C: set32(n, fd, (uint32_t)to_w(n, a, R_NEAREST)); break;
    case 0x0D: set32(n, fd, (uint32_t)to_w(n, a, R_TRUNC)); break;
    case 0x0E: set32(n, fd, (uint32_t)to_w(n, a, R_CEIL)); break;
    case 0x0F: set32(n, fd, (uint32_t)to_w(n, a, R_FLOOR)); break;
    case 0x21: setd(n, fd, (double)a); break;                                   /* CVT.D.S */
    case 0x24: set32(n, fd, (uint32_t)to_w(n, a, R_CURRENT)); break;            /* CVT.W.S */
    case 0x25: set64(n, fd, (uint64_t)to_l(n, a, R_CURRENT)); break;            /* CVT.L.S */
    default:
      if ((op & 63) >= 0x30) compare(n, a, b, op & 15);
      else cpu_reserved(n);
  }
}

static void op_d(N64 *n, uint32_t op) {
  uint32_t fs = FS(op), ft = FT(op), fd = FD(op);
  double a = getd(n, fs), b = getd(n, ft);
  switch (op & 63) {
    case 0x00: WITH_MODE(setd(n, fd, a + b)); break;
    case 0x01: WITH_MODE(setd(n, fd, a - b)); break;
    case 0x02: WITH_MODE(setd(n, fd, a * b)); break;
    case 0x03: WITH_MODE(setd(n, fd, a / b)); break;
    case 0x04: WITH_MODE(setd(n, fd, sqrt(a))); break;
    case 0x05: setd(n, fd, fabs(a)); break;
    case 0x06: set64(n, fd, get64(n, fs)); break;
    case 0x07: setd(n, fd, -a); break;
    case 0x08: set64(n, fd, (uint64_t)to_l(n, a, R_NEAREST)); break;
    case 0x09: set64(n, fd, (uint64_t)to_l(n, a, R_TRUNC)); break;
    case 0x0A: set64(n, fd, (uint64_t)to_l(n, a, R_CEIL)); break;
    case 0x0B: set64(n, fd, (uint64_t)to_l(n, a, R_FLOOR)); break;
    case 0x0C: set32(n, fd, (uint32_t)to_w(n, a, R_NEAREST)); break;
    case 0x0D: set32(n, fd, (uint32_t)to_w(n, a, R_TRUNC)); break;
    case 0x0E: set32(n, fd, (uint32_t)to_w(n, a, R_CEIL)); break;
    case 0x0F: set32(n, fd, (uint32_t)to_w(n, a, R_FLOOR)); break;
    case 0x20: WITH_MODE(setf(n, fd, (float)a)); break;                         /* CVT.S.D */
    case 0x24: set32(n, fd, (uint32_t)to_w(n, a, R_CURRENT)); break;            /* CVT.W.D */
    case 0x25: set64(n, fd, (uint64_t)to_l(n, a, R_CURRENT)); break;            /* CVT.L.D */
    default:
      if ((op & 63) >= 0x30) compare(n, a, b, op & 15);
      else cpu_reserved(n);
  }
}

static void op_w(N64 *n, uint32_t op) {
  int32_t v = (int32_t)get32(n, FS(op));
  switch (op & 63) {
    case 0x20: WITH_MODE(setf(n, FD(op), (float)v)); break;   /* CVT.S.W */
    case 0x21: setd(n, FD(op), (double)v); break;             /* CVT.D.W */
    default: cpu_reserved(n);
  }
}

static void op_l(N64 *n, uint32_t op) {
  int64_t v = (int64_t)get64(n, FS(op));
  switch (op & 63) {
    case 0x20: WITH_MODE(setf(n, FD(op), (float)v)); break;   /* CVT.S.L */
    case 0x21: WITH_MODE(setd(n, FD(op), (double)v)); break;  /* CVT.D.L */
    default: cpu_reserved(n);
  }
}

void fpu_cop1(N64 *n, uint32_t op) {
  Cpu *c = &n->cpu;
  uint32_t rt = FT(op), fs = FS(op);
  switch ((op >> 21) & 31) {
    case 0x00: if (rt) c->gpr[rt] = sext32(get32(n, fs)); return;        /* MFC1 */
    case 0x01: if (rt) c->gpr[rt] = get64(n, fs); return;                /* DMFC1 */
    case 0x02: /* CFC1 */
      if (rt) c->gpr[rt] = sext32(fs == 31 ? c->fcr31 : (fs == 0 ? 0x00000A00u : 0));
      return;
    case 0x04: set32(n, fs, (uint32_t)c->gpr[rt]); return;              /* MTC1 */
    case 0x05: set64(n, fs, c->gpr[rt]); return;                        /* DMTC1 */
    case 0x06: /* CTC1 */
      if (fs == 31) {
        c->fcr31 = (uint32_t)c->gpr[rt] & 0x0183FFFFu;
        uint32_t cause = (c->fcr31 >> 12) & 0x3F, enables = (c->fcr31 >> 7) & 0x1F;
        if ((cause & enables) || (cause & 0x20)) cpu_fpu_exception(n);
      }
      return;
    case 0x10: c->fcr31 &= ~0x0003F000u; op_s(n, op); return;
    case 0x11: c->fcr31 &= ~0x0003F000u; op_d(n, op); return;
    case 0x14: op_w(n, op); return;
    case 0x15: op_l(n, op); return;
    default: cpu_reserved(n);
  }
}
