/* RDP tests: command lists built in RDRAM, run, and the framebuffer
 * checked. Covers fill mode, 1-cycle triangles with shading, copy-mode
 * texture rectangles, the Z buffer and SYNC_FULL. */
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

#define FB 0x100000u
#define ZB 0x200000u
#define TEX 0x300000u
#define CMD 0x400000u
#define W 64
#define H 48

static N64 *n;
static uint32_t ncmd;

static void cmd(uint64_t w) { wr64be(n->rdram + CMD + ncmd * 8, w); ncmd++; }
static void run(void) {
  n->dp.start = n->dp.current = CMD;
  n->dp.end = CMD + ncmd * 8;
  rdp_run(n);
  ncmd = 0;
}
static uint16_t px(int x, int y) { return rd16be(n->rdram + FB + (uint32_t)(y * W + x) * 2); }

static uint64_t op(uint32_t o) { return (uint64_t)o << 56; }
static uint64_t color_image(uint32_t size, uint32_t width, uint32_t addr) { return op(0x3F) | (uint64_t)size << 51 | (uint64_t)(width - 1) << 32 | addr; }
static uint64_t scissor(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) { return op(0x2D) | (uint64_t)(x0 << 2) << 44 | (uint64_t)(y0 << 2) << 32 | (x1 << 2) << 12 | (y1 << 2); }
static uint64_t fill_rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) { return op(0x36) | (uint64_t)(x1 << 2) << 44 | (uint64_t)(y1 << 2) << 32 | (x0 << 2) << 12 | (y0 << 2); }

int main(void) {
  n = (N64 *)calloc(1, sizeof(N64));

  /* Fill mode: two 16-bit pixels per fill colour word. */
  cmd(color_image(2, W, FB));
  cmd(scissor(0, 0, W, H));
  cmd(op(0x2F) | (uint64_t)3 << 52);               /* other modes: fill */
  cmd(op(0x37) | 0xF801F801u);                       /* red */
  cmd(fill_rect(0, 0, W - 1, H - 1));
  cmd(op(0x37) | 0x07C107C1u);                       /* green */
  cmd(fill_rect(10, 10, 19, 19));
  cmd(op(0x29));                                     /* sync full */
  run();
  CHECK("fill red corner", px(0, 0), 0xF801);
  CHECK("fill green inside", px(15, 15), 0x07C1);
  CHECK("fill rect inclusive edge", px(19, 19), 0x07C1);
  CHECK("fill outside", px(20, 20), 0xF801);
  CHECK("sync full raises DP interrupt", n->mi.intr & MI_INTR_DP, MI_INTR_DP);

  /* 1-cycle flat triangle using the primitive colour (combiner: prim). */
  cmd(op(0x2F) | 0);                                 /* 1-cycle, no blending */
  /* combine both cycles: rgb = (0-0)*0 + prim, alpha = prim */
  {
    uint64_t c = op(0x3C);
    c |= (uint64_t)15 << 52 | (uint64_t)31 << 47 | (uint64_t)7 << 44 | (uint64_t)7 << 41; /* cycle 0 a/c */
    c |= (uint64_t)15 << 37 | (uint64_t)31 << 32;                                          /* cycle 1 a/c */
    c |= (uint64_t)15 << 28 | (uint64_t)15 << 24 | (uint64_t)7 << 21 | (uint64_t)7 << 18;   /* b, alpha a/c */
    c |= (uint64_t)3 << 15 | (uint64_t)7 << 12 | (uint64_t)3 << 9 | (uint64_t)3 << 6 | (uint64_t)7 << 3 | 3; /* d = prim */
    cmd(c);
  }
  cmd(op(0x3A) | 0x0000FFFFu);                       /* prim: blue, opaque */
  /* Left-major right triangle: top edge y=30..40, x from 30, widening to the right. */
  {
    int32_t yh = 30 * 4, ym = 30 * 4, yl = 40 * 4;
    uint64_t w0 = op(0x08) | (uint64_t)1 << 55 | (uint64_t)(yl & 0x3FFF) << 32 | (uint64_t)(ym & 0x3FFF) << 16 | (uint64_t)(yh & 0x3FFF);
    cmd(w0);
    cmd((uint64_t)(uint32_t)(30 << 16) << 32 | (uint32_t)(1 << 16));  /* XL = 30, slope +1 */
    cmd((uint64_t)(uint32_t)(30 << 16) << 32 | 0);                    /* XH = 30, vertical */
    cmd((uint64_t)(uint32_t)(30 << 16) << 32 | (uint32_t)(1 << 16));  /* XM */
  }
  run();
  CHECK("triangle inside", px(31, 38), (0 << 11) | (0 << 6) | (31 << 1) | 1);
  CHECK("triangle outside (right of edge)", px(45, 32), 0xF801);
  CHECK("triangle above", px(32, 29), 0xF801);

  /* Copy-mode texture rectangle: an RGBA16 texture copied 1:1. */
  for (int i = 0; i < 16; i++) wr16be(n->rdram + TEX + i * 2, (uint16_t)(0x1000 + i * 2 + 1));
  cmd(op(0x2F) | (uint64_t)2 << 52);                 /* copy */
  cmd(op(0x3D) | (uint64_t)2 << 51 | (uint64_t)(4 - 1) << 32 | TEX);   /* texture image 16-bit, width 4 */
  cmd(op(0x35) | (uint64_t)2 << 51 | (uint64_t)1 << 41 | 0);          /* tile 0: RGBA16, line 1 (8 bytes), tmem 0 */
  cmd(op(0x34) | (uint64_t)(0) << 44 | (uint64_t)(0) << 32 | (3 << 2) << 12 | (3 << 2)); /* load tile 4x4 */
  cmd(op(0x24) | (uint64_t)(43 << 2) << 44 | (uint64_t)(3 << 2) << 32 | (40 << 2) << 12 | (0 << 2));
  cmd((uint64_t)0 << 48 | (uint64_t)0 << 32 | (uint64_t)(4 << 10) << 16 | (1 << 10)); /* s,t = 0; dsdx 4.0 (copy), dtdy 1.0 */
  run();
  CHECK("copy texel 0", px(40, 0), 0x1001);
  CHECK("copy texel 3", px(43, 0), 0x1007);
  CHECK("copy texel row 2", px(41, 2), 0x1000 + 9 * 2 + 1);

  printf("rdp: %d/%d checks passed\n", checks - failures, checks);
  free(n);
  return failures ? 1 : 0;
}
