/* RSP tests: small vector programs run from IMEM, results read from DMEM.
 * Expected values follow the documented VU behaviour (and, for VRCP, the
 * value real hardware produces in krom's test). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/n64.h"

static int failures, checks;
#define CHECK(name, got, want)                                                                  \
  do {                                                                                          \
    uint64_t g_ = (uint64_t)(got), w_ = (uint64_t)(want);                                       \
    checks++;                                                                                   \
    if (g_ != w_) { failures++; printf("FAIL %-36s got %llx want %llx\n", name, (unsigned long long)g_, (unsigned long long)w_); } \
  } while (0)

#define VOP(fn, vd, vs, vt, e) (0x12u << 26 | 1u << 25 | (uint32_t)(e) << 21 | (uint32_t)(vt) << 16 | (uint32_t)(vs) << 11 | (uint32_t)(vd) << 6 | (fn))
#define LQV(vt, off, base) (0x32u << 26 | (uint32_t)(base) << 21 | (uint32_t)(vt) << 16 | 4u << 11 | ((uint32_t)(off) & 0x7F))
#define SQV(vt, off, base) (0x3Au << 26 | (uint32_t)(base) << 21 | (uint32_t)(vt) << 16 | 4u << 11 | ((uint32_t)(off) & 0x7F))
#define CFC2(rt, rd) (0x12u << 26 | 2u << 21 | (uint32_t)(rt) << 16 | (uint32_t)(rd) << 11)
#define SH(rt, off, base) (0x29u << 26 | (uint32_t)(base) << 21 | (uint32_t)(rt) << 16 | ((uint32_t)(off) & 0xFFFF))
#define BREAK 0x0000000Du
enum { VMULF = 0x00, VMUDH = 0x07, VMACF = 0x08, VADD = 0x10, VSUB = 0x11, VADDC = 0x14, VSAR = 0x1D, VLT = 0x20, VCH = 0x25, VMRG = 0x27, VRCP = 0x30, VMOV = 0x33 };

static N64 *n;
static uint32_t pc;

static void begin(void) { memset(n, 0, sizeof *n); pc = 0; }
static void emit(uint32_t w) { wr32be(n->imem + pc, w); pc += 4; }
static void quad(uint32_t at, const uint16_t v[8]) { for (int i = 0; i < 8; i++) wr16be(n->dmem + at + i * 2, v[i]); }
static uint16_t dm(uint32_t at) { return rd16be(n->dmem + at); }

static void run(void) {
  emit(BREAK);
  n->sp.status = 0x40;          /* running, interrupt on break */
  n->rsp.pc = 0;
  n->rsp.next_pc = 4;
  rsp_run(n, 10000);
}

int main(void) {
  n = (N64 *)calloc(1, sizeof(N64));

  /* VMULF: 0.5 * 0.5 = 0.25; -1 * -1 clamps to 0x7FFF. */
  begin();
  quad(0x00, (const uint16_t[8]){0x4000, 0x8000, 0x0001, 0xC000, 0, 0, 0, 0});
  quad(0x10, (const uint16_t[8]){0x4000, 0x8000, 0x0001, 0x4000, 0, 0, 0, 0});
  emit(LQV(1, 0, 0)); emit(LQV(2, 1, 0));
  emit(VOP(VMULF, 3, 1, 2, 0));
  emit(SQV(3, 2, 0));
  run();
  CHECK("halted after break", n->sp.status & 3, 3);
  CHECK("sp interrupt on break", n->mi.intr & MI_INTR_SP, MI_INTR_SP);
  CHECK("vmulf 0.5*0.5", dm(0x20), 0x2000);
  CHECK("vmulf -1*-1 clamps", dm(0x22), 0x7FFF);
  CHECK("vmulf rounds small product", dm(0x24), 0x0000);
  CHECK("vmulf -0.5*0.5", dm(0x26), 0xE000);

  /* VADD clamps; VADDC sets carry; VADD then consumes it. */
  begin();
  quad(0x00, (const uint16_t[8]){0x7FFF, 0x8000, 0xFFFF, 1, 2, 3, 4, 5});
  quad(0x10, (const uint16_t[8]){0x0001, 0xFFFF, 0x0001, 1, 1, 1, 1, 1});
  emit(LQV(1, 0, 0)); emit(LQV(2, 1, 0));
  emit(VOP(VADD, 3, 1, 2, 0));
  emit(SQV(3, 2, 0));
  emit(VOP(VADDC, 4, 1, 2, 0));
  emit(SQV(4, 3, 0));
  emit(CFC2(5, 0));
  emit(SH(5, 0x40, 0));
  emit(VOP(VADD, 6, 0, 0, 0));        /* 0 + 0 + carry */
  emit(SQV(6, 5, 0));
  run();
  CHECK("vadd clamps high", dm(0x20), 0x7FFF);
  CHECK("vadd clamps low", dm(0x22), 0x8000);
  CHECK("vadd -1+1", dm(0x24), 0);
  CHECK("vaddc wraps", dm(0x30), 0x8000);
  CHECK("vaddc carry bits", dm(0x40), 0x06);   /* lanes 1 and 2 carried */
  CHECK("vadd uses carry", dm(0x52) | dm(0x54) << 4, 0x11);

  /* Broadcast element selector and VMOV. */
  begin();
  quad(0x00, (const uint16_t[8]){10, 20, 30, 40, 50, 60, 70, 80});
  emit(LQV(1, 0, 0));
  emit(VOP(VADD, 2, 0, 1, 8 + 3));     /* 0 + v1[3] in every lane */
  emit(SQV(2, 1, 0));
  emit(VOP(VMOV, 3, 5, 1, 8 + 7));     /* v3[5] = v1[7] */
  emit(SQV(3, 2, 0));
  run();
  CHECK("broadcast lane 0", dm(0x10), 40);
  CHECK("broadcast lane 7", dm(0x1E), 40);
  CHECK("vmov element", dm(0x2A), 80);

  /* Multiply-accumulate: VMUDH then VMACF, read back with VSAR. */
  begin();
  quad(0x00, (const uint16_t[8]){3, 0x7FFF, 0, 0, 0, 0, 0, 0});
  quad(0x10, (const uint16_t[8]){4, 0x7FFF, 0, 0, 0, 0, 0, 0});
  emit(LQV(1, 0, 0)); emit(LQV(2, 1, 0));
  emit(VOP(VMUDH, 3, 1, 2, 0));        /* acc = 12 << 16 */
  emit(SQV(3, 2, 0));
  emit(VOP(VSAR, 4, 0, 0, 9));         /* acc middle */
  emit(SQV(4, 3, 0));
  emit(VOP(VMACF, 5, 1, 2, 0));        /* acc += 3*4*2 */
  emit(VOP(VSAR, 6, 0, 0, 10));        /* acc low */
  emit(SQV(6, 4, 0));
  run();
  CHECK("vmudh", dm(0x20), 12);
  CHECK("vmudh clamps", dm(0x22), 0x7FFF);
  CHECK("vsar mid", dm(0x30), 12);
  CHECK("vmacf adds into low", dm(0x40), 24);

  /* VRCP: 1/17 (hardware result from krom's test: low half 0x8400). */
  begin();
  quad(0x10, (const uint16_t[8]){17, 0, 0, 0, 0, 0, 0, 0});
  emit(LQV(1, 1, 0));
  emit(VOP(VRCP, 2, 0, 1, 0));
  emit(SQV(2, 2, 0));
  run();
  CHECK("vrcp 17", dm(0x20), 0x8400);

  /* VCH + VMRG: clip compare, then merge on VCC. */
  begin();
  quad(0x00, (const uint16_t[8]){5, 0xFFFB, 100, 0, 0, 0, 0, 0});
  quad(0x10, (const uint16_t[8]){10, 10, 10, 0, 0, 0, 0, 0});
  emit(LQV(1, 0, 0)); emit(LQV(2, 1, 0));
  emit(VOP(VCH, 3, 1, 2, 0));
  emit(SQV(3, 2, 0));
  emit(VOP(VLT, 4, 1, 2, 0));
  emit(VOP(VMRG, 5, 2, 1, 0));
  emit(SQV(5, 3, 0));
  run();
  CHECK("vch inside range", dm(0x20), 5);
  CHECK("vch -5 vs 10 stays", dm(0x22), 0xFFFB);
  CHECK("vch clips 100 to 10", dm(0x24), 10);
  CHECK("vmrg picks by vcc", dm(0x30), 10);

  printf("rsp: %d/%d checks passed\n", checks - failures, checks);
  free(n);
  return failures ? 1 : 0;
}
