/*
 * RDP: the N64's rasteriser, drawn in software.
 *
 * Commands come from RDRAM (or DMEM over the XBUS) between DPC_CURRENT and
 * DPC_END. Triangles are walked edge by edge with shade, texture and depth
 * attributes interpolated per pixel; texture rectangles, fill rectangles,
 * TMEM loads, texture sampling in every format, the colour combiner, the
 * blender, the Z buffer and 16/32-bit framebuffers are all handled here.
 *
 * Simplifications for now (documented so they can be revisited): one sample
 * per pixel (no coverage / antialiasing), no dithering, no LOD selection
 * (mipmapped textures use the tile given), no YUV textures.
 */
#include <string.h>

#include "n64.h"

/* ------------------------------------------------------------------ */
/* Bits of the Set Other Modes word                                     */

#define OM(r, bit) (((r)->other >> (bit)) & 1)
#define CYCLE_TYPE(r) (((r)->other >> 52) & 3)
enum { CYC_1 = 0, CYC_2 = 1, CYC_COPY = 2, CYC_FILL = 3 };
#define PERSP(r) OM(r, 51)
#define EN_TLUT(r) OM(r, 47)
#define TLUT_IA(r) OM(r, 46)
#define BILERP(r) OM(r, 45)
#define FORCE_BLEND(r) OM(r, 14)
#define ALPHA_CVG_SEL(r) OM(r, 13)
#define Z_MODE(r) (((r)->other >> 10) & 3)
#define IMAGE_READ(r) OM(r, 6)
#define Z_UPDATE(r) OM(r, 5)
#define Z_COMPARE(r) OM(r, 4)
#define Z_SOURCE_PRIM(r) OM(r, 2)
#define ALPHA_COMPARE(r) OM(r, 0)

typedef struct { int32_t r, g, b, a; } Col;

static inline Col unpack32(uint32_t v) { return (Col){(int32_t)(v >> 24), (int32_t)(v >> 16 & 0xFF), (int32_t)(v >> 8 & 0xFF), (int32_t)(v & 0xFF)}; }
static inline int32_t clamp255(int32_t v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline int32_t c5to8(int32_t v) { return (v << 3) | (v >> 2); }

/* ------------------------------------------------------------------ */
/* Memory helpers                                                       */

static inline uint8_t rd8(N64 *n, uint32_t a) { return n->rdram[a & (RDRAM_SIZE - 1)]; }
static inline uint16_t rd16(N64 *n, uint32_t a) { return rd16be(n->rdram + (a & (RDRAM_SIZE - 2))); }
static inline uint32_t rd32(N64 *n, uint32_t a) { return rd32be(n->rdram + (a & (RDRAM_SIZE - 4))); }
static inline void wr16(N64 *n, uint32_t a, uint16_t v) { wr16be(n->rdram + (a & (RDRAM_SIZE - 2)), v); }
static inline void wr32(N64 *n, uint32_t a, uint32_t v) { wr32be(n->rdram + (a & (RDRAM_SIZE - 4)), v); }
static inline uint8_t tm8(Rdp *r, uint32_t a) { return r->tmem[a & 0xFFF]; }
static inline uint16_t tm16(Rdp *r, uint32_t a) { return (uint16_t)(r->tmem[a & 0xFFE] << 8 | r->tmem[(a & 0xFFE) + 1]); }

/* ------------------------------------------------------------------ */
/* Z buffer: 18-bit depth stored as 14 bits (3-bit exponent, 11-bit mantissa) + 2 bits of dz */

static const uint32_t z_base[8] = {0x00000, 0x20000, 0x30000, 0x38000, 0x3C000, 0x3E000, 0x3F000, 0x3F800};
static const uint32_t z_shift[8] = {6, 5, 4, 3, 2, 1, 0, 0};

static uint32_t z_decompress(uint16_t v) {
  uint32_t e = v >> 13, m = (v >> 2) & 0x7FF;
  return z_base[e] + (m << z_shift[e]);
}

static uint16_t z_compress(uint32_t z) {
  z &= 0x3FFFF;
  uint32_t e = 0;
  while (e < 7 && z >= z_base[e + 1]) e++;
  uint32_t m = ((z - z_base[e]) >> z_shift[e]) & 0x7FF;
  return (uint16_t)((e << 13) | (m << 2));
}

/* ------------------------------------------------------------------ */
/* Textures                                                             */

static Col tlut(Rdp *r, uint32_t idx) {
  uint16_t v = tm16(r, 0x800 + (idx & 0xFF) * 8);
  if (TLUT_IA(r)) {
    int32_t i = v >> 8, a = v & 0xFF;
    return (Col){i, i, i, a};
  }
  return (Col){c5to8(v >> 11), c5to8((v >> 6) & 31), c5to8((v >> 1) & 31), (v & 1) ? 255 : 0};
}

/* One texel at integer (s, t) relative to the tile. */
static Col fetch(Rdp *r, const RdpTile *tl, int32_t s, int32_t t) {
  uint32_t row = tl->tmem * 8 + (uint32_t)t * tl->line * 8;
  uint32_t x = (t & 1) ? 4 : 0;  /* odd rows have their 32-bit words swapped */
  bool use_tlut = EN_TLUT(r);
  switch (tl->size) {
    case 0: { /* 4 bpp */
      uint8_t b = tm8(r, (row + (uint32_t)s / 2) ^ x);
      uint32_t v = (s & 1) ? (b & 15) : (b >> 4);
      if (use_tlut) return tlut(r, tl->palette << 4 | v);
      if (tl->format == 3) { /* IA4: 3-bit intensity, 1-bit alpha */
        int32_t i = (int32_t)(v >> 1);
        i = (i << 5) | (i << 2) | (i >> 1);
        return (Col){i, i, i, (v & 1) ? 255 : 0};
      }
      int32_t i = (int32_t)(v * 17); /* I4 (and CI4 without a TLUT) */
      return (Col){i, i, i, i};
    }
    case 1: { /* 8 bpp */
      uint8_t v = tm8(r, (row + (uint32_t)s) ^ x);
      if (use_tlut) return tlut(r, v);
      if (tl->format == 3) { int32_t i = (v >> 4) * 17; return (Col){i, i, i, (v & 15) * 17}; } /* IA8 */
      return (Col){v, v, v, v}; /* I8 */
    }
    case 2: { /* 16 bpp */
      uint16_t v = tm16(r, (row + (uint32_t)s * 2) ^ x);
      if (use_tlut) return tlut(r, v >> 8);
      if (tl->format == 3) return (Col){v >> 8, v >> 8, v >> 8, v & 0xFF}; /* IA16 */
      if (tl->format == 1) return (Col){128, 128, 128, 255};             /* YUV: not yet */
      return (Col){c5to8(v >> 11), c5to8((v >> 6) & 31), c5to8((v >> 1) & 31), (v & 1) ? 255 : 0};
    }
    default: { /* 32 bpp RGBA: red/green in low TMEM, blue/alpha in high TMEM */
      uint32_t a = ((row + (uint32_t)s * 2) ^ x) & 0x7FF;
      uint16_t hi = tm16(r, a), lo = tm16(r, a | 0x800);
      return (Col){hi >> 8, hi & 0xFF, lo >> 8, lo & 0xFF};
    }
  }
}

static int32_t wrap(int32_t c, uint32_t mask, bool mirror, bool clamp, int32_t max) {
  if (clamp || !mask) {
    if (c < 0) c = 0;
    if (c > max) c = max;
  }
  if (mask) {
    if (mirror && ((c >> mask) & 1)) c = ~c;
    c &= (1 << mask) - 1;
  }
  return c;
}

static int32_t shift_coord(int32_t c, uint32_t shift) {
  if (shift < 11) return c >> shift;
  return c << (16 - shift);
}

/* Sample a tile at (s, t) in s10.5 texel coordinates. */
static Col sample(Rdp *r, uint32_t tile, int32_t s, int32_t t, bool filter) {
  const RdpTile *tl = &r->tile[tile & 7];
  s = shift_coord(s, tl->shift_s) - (int32_t)(tl->sl << 3);
  t = shift_coord(t, tl->shift_t) - (int32_t)(tl->tl << 3);
  int32_t maxs = (int32_t)((tl->sh - tl->sl) >> 2), maxt = (int32_t)((tl->th - tl->tl) >> 2);
  bool cs = tl->cs, ct = tl->ct;
  int32_t si = s >> 5, ti = t >> 5;
  if (!filter) {
    return fetch(r, tl, wrap(si, tl->mask_s, tl->ms, cs, maxs), wrap(ti, tl->mask_t, tl->mt, ct, maxt));
  }
  int32_t fs = s & 31, ft = t & 31;
  int32_t s0 = wrap(si, tl->mask_s, tl->ms, cs, maxs), s1 = wrap(si + 1, tl->mask_s, tl->ms, cs, maxs);
  int32_t t0 = wrap(ti, tl->mask_t, tl->mt, ct, maxt), t1 = wrap(ti + 1, tl->mask_t, tl->mt, ct, maxt);
  /* The RDP's 3-point filter: pick the triangle of the texel square the
   * sample falls in. */
  Col out;
  if (fs + ft < 32) {
    Col c0 = fetch(r, tl, s0, t0), c1 = fetch(r, tl, s1, t0), c2 = fetch(r, tl, s0, t1);
    out.r = c0.r + ((c1.r - c0.r) * fs + (c2.r - c0.r) * ft + 16) / 32;
    out.g = c0.g + ((c1.g - c0.g) * fs + (c2.g - c0.g) * ft + 16) / 32;
    out.b = c0.b + ((c1.b - c0.b) * fs + (c2.b - c0.b) * ft + 16) / 32;
    out.a = c0.a + ((c1.a - c0.a) * fs + (c2.a - c0.a) * ft + 16) / 32;
  } else {
    Col c3 = fetch(r, tl, s1, t1), c1 = fetch(r, tl, s1, t0), c2 = fetch(r, tl, s0, t1);
    int32_t gs = 32 - fs, gt = 32 - ft;
    out.r = c3.r + ((c2.r - c3.r) * gs + (c1.r - c3.r) * gt + 16) / 32;
    out.g = c3.g + ((c2.g - c3.g) * gs + (c1.g - c3.g) * gt + 16) / 32;
    out.b = c3.b + ((c2.b - c3.b) * gs + (c1.b - c3.b) * gt + 16) / 32;
    out.a = c3.a + ((c2.a - c3.a) * gs + (c1.a - c3.a) * gt + 16) / 32;
  }
  out.r = clamp255(out.r); out.g = clamp255(out.g); out.b = clamp255(out.b); out.a = clamp255(out.a);
  return out;
}

/* ------------------------------------------------------------------ */
/* Colour combiner: (A - B) * C + D for RGB and alpha, one or two cycles */

typedef struct {
  Col combined, tex0, tex1, shade, prim, env;
  int32_t lod_frac, prim_lod_frac;
} CcIn;

static int32_t cc_a(const CcIn *in, uint32_t sel, int ch) {
  const int32_t *c[] = {&in->combined.r, &in->tex0.r, &in->tex1.r, &in->prim.r, &in->shade.r, &in->env.r};
  if (sel < 6) return c[sel][ch];
  if (sel == 6) return 256;
  if (sel == 7) return 128; /* noise (constant mid value for now) */
  return 0;
}
static int32_t cc_b(const CcIn *in, uint32_t sel, int ch, const Rdp *r) {
  const int32_t *c[] = {&in->combined.r, &in->tex0.r, &in->tex1.r, &in->prim.r, &in->shade.r, &in->env.r};
  if (sel < 6) return c[sel][ch];
  if (sel == 6) return (int32_t)((ch == 0 ? r->key_r : ch == 1 ? r->key_g : r->key_b) >> 16 & 0xFF); /* key centre */
  if (sel == 7) return (int32_t)(r->convert[4]); /* K4 */
  return 0;
}
static int32_t cc_c(const CcIn *in, uint32_t sel, int ch, const Rdp *r) {
  const int32_t *c[] = {&in->combined.r, &in->tex0.r, &in->tex1.r, &in->prim.r, &in->shade.r, &in->env.r};
  if (sel < 6) return c[sel][ch];
  switch (sel) {
    case 6: return (int32_t)((ch == 0 ? r->key_r : ch == 1 ? r->key_g : r->key_b) & 0xFF); /* key scale */
    case 7: return in->combined.a;
    case 8: return in->tex0.a;
    case 9: return in->tex1.a;
    case 10: return in->prim.a;
    case 11: return in->shade.a;
    case 12: return in->env.a;
    case 13: return in->lod_frac;
    case 14: return in->prim_lod_frac;
    case 15: return (int32_t)r->convert[5]; /* K5 */
    default: return 0;
  }
}
static int32_t cc_d(const CcIn *in, uint32_t sel, int ch) {
  const int32_t *c[] = {&in->combined.r, &in->tex0.r, &in->tex1.r, &in->prim.r, &in->shade.r, &in->env.r};
  if (sel < 6) return c[sel][ch];
  if (sel == 6) return 255;
  return 0;
}
static int32_t ca_abd(const CcIn *in, uint32_t sel) {
  switch (sel) {
    case 0: return in->combined.a;
    case 1: return in->tex0.a;
    case 2: return in->tex1.a;
    case 3: return in->prim.a;
    case 4: return in->shade.a;
    case 5: return in->env.a;
    case 6: return 255;
    default: return 0;
  }
}
static int32_t ca_c(const CcIn *in, uint32_t sel) {
  switch (sel) {
    case 0: return in->lod_frac;
    case 1: return in->tex0.a;
    case 2: return in->tex1.a;
    case 3: return in->prim.a;
    case 4: return in->shade.a;
    case 5: return in->env.a;
    case 6: return in->prim_lod_frac;
    default: return 0;
  }
}

static inline int32_t cc_mix(int32_t a, int32_t b, int32_t c, int32_t d) {
  /* "1.0" from the A input reads as 256; others are 0..255. The result is
   * 9 bits: 0x100-0x17F saturate to 255, 0x180-0x1FF (negative) to 0. */
  int32_t v = (((a - b) * c + (d << 8) + 0x80) >> 8) & 0x1FF;
  if (v & 0x100) v = (v & 0x80) ? 0 : 255;
  return v;
}

static Col combine_cycle(const Rdp *r, const CcIn *in, int cycle) {
  uint64_t m = r->combine;
  uint32_t sa, sb, mu, ad, saa, sba, mua, ada;
  if (cycle == 0) {
    sa = (uint32_t)(m >> 52) & 15; mu = (uint32_t)(m >> 47) & 31; saa = (uint32_t)(m >> 44) & 7; mua = (uint32_t)(m >> 41) & 7;
    sb = (uint32_t)(m >> 28) & 15; ad = (uint32_t)(m >> 15) & 7; sba = (uint32_t)(m >> 12) & 7; ada = (uint32_t)(m >> 9) & 7;
  } else {
    sa = (uint32_t)(m >> 37) & 15; mu = (uint32_t)(m >> 32) & 31; saa = (uint32_t)(m >> 21) & 7; mua = (uint32_t)(m >> 18) & 7;
    sb = (uint32_t)(m >> 24) & 15; ad = (uint32_t)(m >> 6) & 7; sba = (uint32_t)(m >> 3) & 7; ada = (uint32_t)m & 7;
  }
  Col o;
  int32_t *out[] = {&o.r, &o.g, &o.b};
  for (int ch = 0; ch < 3; ch++) {
    int32_t a = cc_a(in, sa, ch);
    *out[ch] = cc_mix(a == 256 ? 256 : a, cc_b(in, sb, ch, r), cc_c(in, mu, ch, r), cc_d(in, ad, ch));
  }
  o.a = cc_mix(ca_abd(in, saa), ca_abd(in, sba), ca_c(in, mua), ca_abd(in, ada));
  return o;
}

/* ------------------------------------------------------------------ */
/* Blender                                                              */

static Col blend_cycle(const Rdp *r, int cycle, Col pixel, Col memory, int32_t shade_alpha, bool apply) {
  uint32_t lo = (uint32_t)r->other;
  int sh = cycle == 0 ? 0 : 2;
  uint32_t m1a = (lo >> (30 - sh)) & 3, m1b = (lo >> (26 - sh)) & 3, m2a = (lo >> (22 - sh)) & 3, m2b = (lo >> (18 - sh)) & 3;
  Col fog = unpack32(r->fog), bl = unpack32(r->blend);
  Col P = m1a == 0 ? pixel : m1a == 1 ? memory : m1a == 2 ? bl : fog;
  Col M = m2a == 0 ? pixel : m2a == 1 ? memory : m2a == 2 ? bl : fog;
  if (!apply) return P;
  int32_t a = m1b == 0 ? pixel.a : m1b == 1 ? fog.a : m1b == 2 ? shade_alpha : 0;
  int32_t a5 = a >> 3, b5;
  switch (m2b) {
    case 0: b5 = 31 - a5; break;          /* 1 - A */
    case 1: b5 = memory.a >> 3; break;    /* memory coverage */
    case 2: b5 = 31; break;               /* 1 */
    default: b5 = -1; break;              /* 0 */
  }
  Col o;
  o.r = clamp255((P.r * a5 + M.r * (b5 + 1)) >> 5);
  o.g = clamp255((P.g * a5 + M.g * (b5 + 1)) >> 5);
  o.b = clamp255((P.b * a5 + M.b * (b5 + 1)) >> 5);
  o.a = pixel.a;
  return o;
}

/* ------------------------------------------------------------------ */
/* Framebuffer                                                          */

static Col fb_read(N64 *n, int x, int y) {
  Rdp *r = &n->rdp;
  uint32_t i = (uint32_t)y * r->ci_width + (uint32_t)x;
  if (r->ci_size == 3) return unpack32(rd32(n, r->ci_addr + i * 4));
  if (r->ci_size == 2) {
    uint16_t v = rd16(n, r->ci_addr + i * 2);
    return (Col){c5to8(v >> 11), c5to8((v >> 6) & 31), c5to8((v >> 1) & 31), (v & 1) ? 255 : 0};
  }
  uint8_t v = rd8(n, r->ci_addr + i);
  return (Col){v, v, v, 255};
}

static void fb_write(N64 *n, int x, int y, Col c) {
  Rdp *r = &n->rdp;
  uint32_t i = (uint32_t)y * r->ci_width + (uint32_t)x;
  if (r->ci_size == 3) {
    wr32(n, r->ci_addr + i * 4, (uint32_t)c.r << 24 | (uint32_t)c.g << 16 | (uint32_t)c.b << 8 | 0xE0);
  } else if (r->ci_size == 2) {
    wr16(n, r->ci_addr + i * 2, (uint16_t)((c.r >> 3) << 11 | (c.g >> 3) << 6 | (c.b >> 3) << 1 | 1));
  } else {
    n->rdram[(r->ci_addr + i) & (RDRAM_SIZE - 1)] = (uint8_t)c.r;
  }
}

static inline bool in_scissor(const Rdp *r, int x, int y) {
  return x >= (int)(r->sc_xh >> 2) && x < (int)((r->sc_xl + 3) >> 2) && y >= (int)(r->sc_yh >> 2) && y < (int)((r->sc_yl + 3) >> 2);
}

/* Fill mode: write the fill colour (two 16-bit pixels per 32-bit word). */
static void fill_pixel(N64 *n, int x, int y) {
  Rdp *r = &n->rdp;
  uint32_t i = (uint32_t)y * r->ci_width + (uint32_t)x;
  if (r->ci_size == 3) wr32(n, r->ci_addr + i * 4, r->fill);
  else if (r->ci_size == 2) wr16(n, r->ci_addr + i * 2, (uint16_t)((x & 1) ? r->fill : r->fill >> 16));
  else n->rdram[(r->ci_addr + i) & (RDRAM_SIZE - 1)] = (uint8_t)(r->fill >> (8 * (3 - (x & 3))));
}

/* ------------------------------------------------------------------ */
/* The per-pixel pipeline for 1- and 2-cycle modes                      */

typedef struct {
  bool textured, zbuf;
  uint32_t tile;
} Prim;

static void shade_pixel(N64 *n, const Prim *p, int x, int y, Col shade, int32_t s, int32_t t, uint32_t z) {
  Rdp *r = &n->rdp;
  uint32_t zaddr = r->zi_addr + ((uint32_t)y * r->ci_width + (uint32_t)x) * 2;
  if (Z_SOURCE_PRIM(r)) z = (r->prim_z & 0x7FFF) << 3;
  if (p->zbuf && Z_COMPARE(r)) {
    uint32_t old = z_decompress(rd16(n, zaddr));
    bool pass;
    if (Z_MODE(r) == 3) pass = (z > old ? z - old : old - z) <= 0x400; /* decal */
    else pass = z < old;
    if (!pass) return;
  }

  CcIn in;
  memset(&in, 0, sizeof in);
  in.shade = shade;
  in.prim = unpack32(r->prim);
  in.env = unpack32(r->env);
  in.prim_lod_frac = (int32_t)r->prim_lod_frac;
  /* Without LOD calculation the LOD fraction reads as 1.0 (texrects in
   * krom's tests multiply texture alpha by it and show the texture). */
  in.lod_frac = OM(r, 48) ? 0 : 255;
  if (p->textured) {
    bool filter = BILERP(r);
    in.tex0 = sample(r, p->tile, s, t, filter);
    in.tex1 = CYCLE_TYPE(r) == CYC_2 ? sample(r, p->tile + 1, s, t, filter) : in.tex0;
  }
  /* 1-cycle mode runs the combiner's second-cycle settings. */
  Col c;
  if (CYCLE_TYPE(r) == CYC_2) {
    c = combine_cycle(r, &in, 0);
    in.combined = c;
    c = combine_cycle(r, &in, 1);
  } else {
    c = combine_cycle(r, &in, 1);
  }

  if (ALPHA_COMPARE(r) && c.a < (int32_t)(r->blend & 0xFF)) return;
  if (ALPHA_CVG_SEL(r) && !FORCE_BLEND(r) && c.a == 0) return;

  Col mem = IMAGE_READ(r) ? fb_read(n, x, y) : (Col){0, 0, 0, 0};
  Col out;
  if (CYCLE_TYPE(r) == CYC_2) {
    Col first = blend_cycle(r, 0, c, mem, shade.a, true);
    first.a = c.a;
    out = blend_cycle(r, 1, first, mem, shade.a, FORCE_BLEND(r));
  } else {
    out = blend_cycle(r, 0, c, mem, shade.a, FORCE_BLEND(r));
  }
  fb_write(n, x, y, out);
  if (p->zbuf && Z_UPDATE(r)) wr16(n, zaddr, z_compress(z));
}

/* ------------------------------------------------------------------ */
/* Rectangles                                                           */

static void fill_rect(N64 *n, uint64_t c) {
  Rdp *r = &n->rdp;
  uint32_t xl = (uint32_t)(c >> 44) & 0xFFF, yl = (uint32_t)(c >> 32) & 0xFFF;
  uint32_t xh = (uint32_t)(c >> 12) & 0xFFF, yh = (uint32_t)c & 0xFFF;
  int cyc = (int)CYCLE_TYPE(r);
  bool inclusive = cyc == CYC_FILL || cyc == CYC_COPY;
  int x0 = (int)(xh >> 2), y0 = (int)(yh >> 2);
  int x1 = inclusive ? (int)(xl >> 2) : (int)((xl + 3) >> 2) - 1;
  int y1 = inclusive ? (int)(yl >> 2) : (int)((yl + 3) >> 2) - 1;
  Prim p = {false, false, 0};
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      if (!in_scissor(r, x, y)) continue;
      if (cyc == CYC_FILL) fill_pixel(n, x, y);
      else if (cyc != CYC_COPY) shade_pixel(n, &p, x, y, (Col){0, 0, 0, 0}, 0, 0, (r->prim_z & 0x7FFF) << 3);
    }
}

static void tex_rect(N64 *n, const uint64_t *c, bool flip) {
  Rdp *r = &n->rdp;
  uint64_t w0 = c[0], w1 = c[1];
  uint32_t xl = (uint32_t)(w0 >> 44) & 0xFFF, yl = (uint32_t)(w0 >> 32) & 0xFFF;
  uint32_t tile = (uint32_t)(w0 >> 24) & 7;
  uint32_t xh = (uint32_t)(w0 >> 12) & 0xFFF, yh = (uint32_t)w0 & 0xFFF;
  int32_t s0 = (int16_t)(w1 >> 48), t0 = (int16_t)(w1 >> 32);
  int32_t dsdx = (int16_t)(w1 >> 16), dtdy = (int16_t)w1;
  int cyc = (int)CYCLE_TYPE(r);
  bool copy = cyc == CYC_COPY;
  if (copy) dsdx /= 4; /* copy mode advances four texels per clock */
  int x0 = (int)(xh >> 2), y0 = (int)(yh >> 2);
  int x1 = copy || cyc == CYC_FILL ? (int)(xl >> 2) : (int)((xl + 3) >> 2) - 1;
  int y1 = copy || cyc == CYC_FILL ? (int)(yl >> 2) : (int)((yl + 3) >> 2) - 1;
  Prim p = {true, false, tile};
  const RdpTile *tl = &r->tile[tile];
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      if (!in_scissor(r, x, y)) continue;
      int32_t dx = x - x0, dy = y - y0;
      /* s, t are s10.5; dsdx, dtdy are s5.10 */
      int32_t s = flip ? s0 + ((dsdx * dy) >> 5) : s0 + ((dsdx * dx) >> 5);
      int32_t t = flip ? t0 + ((dtdy * dx) >> 5) : t0 + ((dtdy * dy) >> 5);
      if (copy) {
        /* Texels go to the framebuffer unchanged. */
        int32_t ss = shift_coord(s, tl->shift_s) - (int32_t)(tl->sl << 3);
        int32_t tt = shift_coord(t, tl->shift_t) - (int32_t)(tl->tl << 3);
        int32_t si = wrap(ss >> 5, tl->mask_s, tl->ms, tl->cs, (int32_t)((tl->sh - tl->sl) >> 2));
        int32_t ti = wrap(tt >> 5, tl->mask_t, tl->mt, tl->ct, (int32_t)((tl->th - tl->tl) >> 2));
        uint32_t i = (uint32_t)y * r->ci_width + (uint32_t)x;
        if (r->ci_size == 2 && tl->size == 2 && !EN_TLUT(r)) {
          uint32_t row = tl->tmem * 8 + (uint32_t)ti * tl->line * 8;
          uint16_t v = tm16(r, (row + (uint32_t)si * 2) ^ ((ti & 1) ? 4 : 0));
          if (ALPHA_COMPARE(r) && !(v & 1)) continue;
          wr16(n, r->ci_addr + i * 2, v);
        } else {
          Col col = fetch(r, tl, si, ti);
          if (ALPHA_COMPARE(r) && col.a == 0) continue;
          fb_write(n, x, y, col);
        }
        continue;
      }
      shade_pixel(n, &p, x, y, (Col){0, 0, 0, 0}, s, t, (r->prim_z & 0x7FFF) << 3);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Triangles                                                            */

static inline int32_t lane16(uint64_t w, int lane) { return (int32_t)(uint16_t)(w >> (48 - 16 * lane)); }
static inline int32_t fixed(uint64_t wi, uint64_t wf, int lane) {
  return (int32_t)((uint32_t)lane16(wi, lane) << 16 | (uint32_t)lane16(wf, lane));
}
static inline int32_t sext14(uint32_t v) { return (int32_t)(v << 18) >> 18; }

typedef struct { int32_t v, dx, de, dy; } Attr;

static inline int64_t attr_at(const Attr *a, int64_t de_q, int64_t dx_fx) {
  /* de_q: distance down the major edge in quarter lines; dx_fx: s15.16 pixels */
  return (int64_t)a->v + (int64_t)a->de * de_q / 4 + ((int64_t)a->dx * dx_fx >> 16);
}

static void triangle(N64 *n, const uint64_t *c, bool shade, bool tex, bool zb) {
  Rdp *r = &n->rdp;
  uint64_t w0 = c[0];
  bool lft = (w0 >> 55) & 1;
  uint32_t tile = (uint32_t)(w0 >> 48) & 7;
  int32_t yl = sext14((uint32_t)(w0 >> 32) & 0x3FFF), ym = sext14((uint32_t)(w0 >> 16) & 0x3FFF), yh = sext14((uint32_t)w0 & 0x3FFF);
  int32_t xl = (int32_t)(c[1] >> 32), dxldy = (int32_t)c[1];
  int32_t xh = (int32_t)(c[2] >> 32), dxhdy = (int32_t)c[2];
  int32_t xm = (int32_t)(c[3] >> 32), dxmdy = (int32_t)c[3];
  int idx = 4;

  Attr col[4], st[3], z = {0, 0, 0, 0};
  memset(col, 0, sizeof col);
  memset(st, 0, sizeof st);
  if (shade) {
    for (int i = 0; i < 4; i++) {
      col[i].v = fixed(c[idx + 0], c[idx + 2], i);
      col[i].dx = fixed(c[idx + 1], c[idx + 3], i);
      col[i].de = fixed(c[idx + 4], c[idx + 6], i);
      col[i].dy = fixed(c[idx + 5], c[idx + 7], i);
    }
    idx += 8;
  }
  if (tex) {
    for (int i = 0; i < 3; i++) {
      st[i].v = fixed(c[idx + 0], c[idx + 2], i);
      st[i].dx = fixed(c[idx + 1], c[idx + 3], i);
      st[i].de = fixed(c[idx + 4], c[idx + 6], i);
      st[i].dy = fixed(c[idx + 5], c[idx + 7], i);
    }
    idx += 8;
  }
  if (zb) {
    z.v = (int32_t)(c[idx] >> 32);
    z.dx = (int32_t)c[idx];
    z.de = (int32_t)(c[idx + 1] >> 32);
    z.dy = (int32_t)c[idx + 1];
  }

  int cyc = (int)CYCLE_TYPE(r);
  Prim p = {tex, zb, tile};
  int32_t ytop = yh & ~3;                       /* edges are given from this scanline */
  int32_t ystart = yh > (int32_t)r->sc_yh ? yh : (int32_t)r->sc_yh;
  int32_t yend = yl < (int32_t)r->sc_yl ? yl : (int32_t)r->sc_yl;
  for (int32_t Y = ystart >> 2; Y * 4 < yend; Y++) {
    int32_t sy = Y * 4 + 2;                      /* sample at the scanline centre */
    if (sy < yh || sy >= yl) continue;
    int64_t dq = sy - ytop;                      /* quarter lines from the top */
    int64_t xa = (int64_t)xh + (int64_t)dxhdy * dq / 4;
    int64_t xb = sy < ym ? (int64_t)xm + (int64_t)dxmdy * dq / 4 : (int64_t)xl + (int64_t)dxldy * (sy - ym) / 4;
    /* Bit 55 ("left major") set: the major edge (XH) is on the left. */
    int64_t left = lft ? xa : xb, right = lft ? xb : xa;
    if (left > right) continue;
    int xs = (int)((left + 0x8000) >> 16), xe = (int)((right + 0x8000) >> 16); /* pixel centres inside */
    for (int x = xs; x < xe; x++) {
      if (!in_scissor(r, x, Y)) continue;
      if (cyc == CYC_FILL) { fill_pixel(n, x, Y); continue; }
      int64_t dxp = ((int64_t)x << 16) + 0x8000 - xa;  /* from the major edge, s15.16 */
      Col sh = {0, 0, 0, 0};
      if (shade) {
        int32_t v[4];
        for (int i = 0; i < 4; i++) v[i] = clamp255((int32_t)(attr_at(&col[i], dq, dxp) >> 16));
        sh = (Col){v[0], v[1], v[2], v[3]};
      }
      int32_t s = 0, t = 0;
      if (tex) {
        int64_t S = attr_at(&st[0], dq, dxp), T = attr_at(&st[1], dq, dxp), W = attr_at(&st[2], dq, dxp);
        if (PERSP(r)) {
          int64_t w = W >> 16;
          if (w <= 0) w = 1;
          s = (int32_t)(((S >> 16) * 0x8000) / w);
          t = (int32_t)(((T >> 16) * 0x8000) / w);
        } else {
          s = (int32_t)(S >> 16);
          t = (int32_t)(T >> 16);
        }
      }
      uint32_t zv = 0;
      if (zb) {
        int64_t Z = attr_at(&z, dq, dxp) >> 13;
        zv = Z < 0 ? 0 : Z > 0x3FFFF ? 0x3FFFF : (uint32_t)Z;
      }
      if (cyc == CYC_COPY) {
        Col cc = sample(r, tile, s, t, false);
        if (ALPHA_COMPARE(r) && cc.a == 0) continue;
        fb_write(n, x, Y, cc);
        continue;
      }
      shade_pixel(n, &p, x, Y, sh, s, t, zv);
    }
  }
}

/* ------------------------------------------------------------------ */
/* TMEM loads                                                           */

static void set_tile_size(Rdp *r, uint64_t c) {
  RdpTile *t = &r->tile[(c >> 24) & 7];
  t->sl = (uint32_t)(c >> 44) & 0xFFF;
  t->tl = (uint32_t)(c >> 32) & 0xFFF;
  t->sh = (uint32_t)(c >> 12) & 0xFFF;
  t->th = (uint32_t)c & 0xFFF;
}

static void load_block(N64 *n, uint64_t c) {
  Rdp *r = &n->rdp;
  RdpTile *t = &r->tile[(c >> 24) & 7];
  uint32_t sl = (uint32_t)(c >> 44) & 0xFFF, tl = (uint32_t)(c >> 32) & 0xFFF;
  uint32_t sh = (uint32_t)(c >> 12) & 0xFFF, dxt = (uint32_t)c & 0xFFF;
  t->sl = sl; t->tl = tl; t->sh = sh; t->th = dxt;
  uint32_t texels = sh - sl + 1;
  uint32_t bytes = (texels << r->ti_size) >> 1;
  uint32_t src = r->ti_addr + (((tl * r->ti_width + sl) << r->ti_size) >> 1);
  uint32_t words = (bytes + 7) / 8;
  uint32_t base = t->tmem * 8;
  bool split = r->ti_size == 3;
  uint32_t counter = 0;
  for (uint32_t w = 0; w < words; w++) {
    uint32_t swap = ((counter >> 11) & 1) ? 4 : 0;   /* odd lines: swap 32-bit halves */
    counter += dxt;
    if (split) {
      /* Two RGBA32 texels: red/green to low TMEM, blue/alpha to high TMEM. */
      for (int k = 0; k < 2; k++) {
        uint32_t px = rd32(n, src + w * 8 + k * 4);
        uint32_t a = ((base + w * 4 + k * 2) ^ swap) & 0x7FF;
        r->tmem[a] = (uint8_t)(px >> 24); r->tmem[a + 1] = (uint8_t)(px >> 16);
        r->tmem[a | 0x800] = (uint8_t)(px >> 8); r->tmem[(a | 0x800) + 1] = (uint8_t)px;
      }
    } else {
      for (uint32_t b = 0; b < 8; b++) r->tmem[((base + w * 8 + b) ^ swap) & 0xFFF] = rd8(n, src + w * 8 + b);
    }
  }
}

static void load_tile(N64 *n, uint64_t c) {
  Rdp *r = &n->rdp;
  set_tile_size(r, c);
  RdpTile *t = &r->tile[(c >> 24) & 7];
  uint32_t s0 = t->sl >> 2, t0 = t->tl >> 2, s1 = t->sh >> 2, t1 = t->th >> 2;
  uint32_t base = t->tmem * 8, line = t->line * 8;
  for (uint32_t y = t0; y <= t1; y++) {
    uint32_t swap = ((y - t0) & 1) ? 4 : 0;
    uint32_t row = base + (y - t0) * line;
    for (uint32_t x = s0; x <= s1; x++) {
      uint32_t i = y * r->ti_width + x;
      uint32_t dx = x - s0;
      switch (r->ti_size) {
        case 0: /* 4 bpp: copy whole bytes */
          if (!(dx & 1)) r->tmem[((row + dx / 2) ^ swap) & 0xFFF] = rd8(n, r->ti_addr + i / 2);
          break;
        case 1:
          r->tmem[((row + dx) ^ swap) & 0xFFF] = rd8(n, r->ti_addr + i);
          break;
        case 2: {
          uint16_t v = rd16(n, r->ti_addr + i * 2);
          uint32_t a = ((row + dx * 2) ^ swap) & 0xFFE;
          r->tmem[a] = (uint8_t)(v >> 8); r->tmem[a + 1] = (uint8_t)v;
          break;
        }
        default: {
          uint32_t px = rd32(n, r->ti_addr + i * 4);
          uint32_t a = ((row + dx * 2) ^ swap) & 0x7FE;
          r->tmem[a] = (uint8_t)(px >> 24); r->tmem[a + 1] = (uint8_t)(px >> 16);
          r->tmem[a | 0x800] = (uint8_t)(px >> 8); r->tmem[(a | 0x800) + 1] = (uint8_t)px;
          break;
        }
      }
    }
  }
}

static void load_tlut(N64 *n, uint64_t c) {
  Rdp *r = &n->rdp;
  RdpTile *t = &r->tile[(c >> 24) & 7];
  uint32_t s0 = ((uint32_t)(c >> 44) & 0xFFF) >> 2, tl = ((uint32_t)(c >> 32) & 0xFFF) >> 2;
  uint32_t s1 = ((uint32_t)(c >> 12) & 0xFFF) >> 2;
  uint32_t base = t->tmem * 8;
  for (uint32_t i = s0; i <= s1 && i - s0 < 256; i++) {
    uint16_t v = rd16(n, r->ti_addr + (tl * r->ti_width + i) * 2);
    uint32_t a = base + (i - s0) * 8;
    for (int k = 0; k < 4; k++) { /* palette entries are stored four times */
      r->tmem[(a + k * 2) & 0xFFF] = (uint8_t)(v >> 8);
      r->tmem[(a + k * 2 + 1) & 0xFFF] = (uint8_t)v;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Command decoder                                                      */

static uint32_t cmd_words(uint32_t op) {
  if (op >= 0x08 && op <= 0x0F) return 4 + ((op & 4) ? 8 : 0) + ((op & 2) ? 8 : 0) + ((op & 1) ? 2 : 0);
  if (op == 0x24 || op == 0x25) return 2;
  return 1;
}

static void execute(N64 *n, const uint64_t *c) {
  Rdp *r = &n->rdp;
  uint64_t w = c[0];
  uint32_t op = (uint32_t)(w >> 56) & 0x3F;
  uint32_t lo = (uint32_t)w;
  switch (op) {
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
      triangle(n, c, (op & 4) != 0, (op & 2) != 0, (op & 1) != 0);
      break;
    case 0x24: tex_rect(n, c, false); break;
    case 0x25: tex_rect(n, c, true); break;
    case 0x26: case 0x27: case 0x28: break;                     /* sync load / pipe / tile */
    case 0x29: n64_raise_mi(n, MI_INTR_DP); break;              /* sync full */
    case 0x2A: r->key_g = (uint32_t)(w >> 32) & 0xFFF; r->key_b = lo; break;
    case 0x2B: r->key_r = lo; break;
    case 0x2C:
      for (int i = 0; i < 6; i++) r->convert[i] = (uint32_t)(w >> (45 - 9 * i)) & 0x1FF;
      break;
    case 0x2D:
      r->sc_xh = (uint32_t)(w >> 44) & 0xFFF; r->sc_yh = (uint32_t)(w >> 32) & 0xFFF;
      r->sc_field = (lo >> 25) & 1; r->sc_odd = (lo >> 24) & 1;
      r->sc_xl = (lo >> 12) & 0xFFF; r->sc_yl = lo & 0xFFF;
      break;
    case 0x2E: r->prim_z = lo >> 16; r->prim_dz = lo & 0xFFFF; break;
    case 0x2F: r->other = w; break;
    case 0x30: load_tlut(n, w); break;
    case 0x32: set_tile_size(r, w); break;
    case 0x33: load_block(n, w); break;
    case 0x34: load_tile(n, w); break;
    case 0x35: {
      RdpTile *t = &r->tile[(lo >> 24) & 7];
      t->format = (uint32_t)(w >> 53) & 7;
      t->size = (uint32_t)(w >> 51) & 3;
      t->line = (uint32_t)(w >> 41) & 0x1FF;
      t->tmem = (uint32_t)(w >> 32) & 0x1FF;
      t->palette = (lo >> 20) & 15;
      t->ct = (lo >> 19) & 1; t->mt = (lo >> 18) & 1;
      t->mask_t = (lo >> 14) & 15; t->shift_t = (lo >> 10) & 15;
      t->cs = (lo >> 9) & 1; t->ms = (lo >> 8) & 1;
      t->mask_s = (lo >> 4) & 15; t->shift_s = lo & 15;
      break;
    }
    case 0x36: fill_rect(n, w); break;
    case 0x37: r->fill = lo; break;
    case 0x38: r->fog = lo; break;
    case 0x39: r->blend = lo; break;
    case 0x3A: r->prim = lo; r->prim_min_level = (uint32_t)(w >> 40) & 31; r->prim_lod_frac = (uint32_t)(w >> 32) & 0xFF; break;
    case 0x3B: r->env = lo; break;
    case 0x3C: r->combine = w; break;
    case 0x3D:
      r->ti_format = (uint32_t)(w >> 53) & 7; r->ti_size = (uint32_t)(w >> 51) & 3;
      r->ti_width = ((uint32_t)(w >> 32) & 0x3FF) + 1; r->ti_addr = lo & 0x3FFFFFF;
      break;
    case 0x3E: r->zi_addr = lo & 0x3FFFFFF; break;
    case 0x3F:
      r->ci_format = (uint32_t)(w >> 53) & 7; r->ci_size = (uint32_t)(w >> 51) & 3;
      r->ci_width = ((uint32_t)(w >> 32) & 0x3FF) + 1; r->ci_addr = lo & 0x3FFFFFF;
      break;
    default: break; /* NOP and unused */
  }
}

void rdp_run(N64 *n) {
  Rdp *r = &n->rdp;
  bool xbus = (n->dp.status & 1) != 0;  /* commands in DMEM */
  int guard = 1 << 20;
  while (n->dp.current < n->dp.end && guard--) {
    uint32_t a = n->dp.current;
    uint64_t w = xbus ? rd64be(n->dmem + (a & 0xFF8)) : rd64be(n->rdram + (a & (RDRAM_SIZE - 8)));
    n->dp.current += 8;
    if (r->cmd_len < sizeof r->cmd / sizeof r->cmd[0]) r->cmd[r->cmd_len++] = w;
    uint32_t op = (uint32_t)(r->cmd[0] >> 56) & 0x3F;
    if (r->cmd_len >= cmd_words(op)) {
      execute(n, r->cmd);
      r->cmd_len = 0;
    }
  }
  n->dp.status |= 0x80; /* buffer ready */
}
