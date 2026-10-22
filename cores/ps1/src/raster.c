/* GPU rasteriser: polygons, lines and rectangles into VRAM (PSX-SPX,
 * "GPU Render Polygon/Line/Rectangle Commands").
 *
 * Vertices are signed 11-bit, plus the drawing offset (E5), clipped to the
 * drawing area (E3/E4, inclusive). Colours are 24-bit; VRAM is 15-bit
 * (5:5:5, bit 15 = mask). Textures come from the texture page (4-bit and
 * 8-bit through a CLUT, or 15-bit direct), wrapped by the texture window;
 * texel 0x0000 is transparent. Semi-transparency (B = VRAM, F = new):
 * 0 B/2+F/2, 1 B+F, 2 B-F, 3 B+F/4; for textures only where the texel has
 * bit 15. Gouraud and modulated-texture polygons and lines are dithered
 * when E1 bit 9 is set; rectangles never are. */
#include <stdlib.h>

#include "ps1.h"

typedef struct {
  int32_t x, y;
  int32_t r, g, b;
  int32_t u, v;
} Vert;

typedef struct {
  bool textured, raw, semi, dither, gouraud;
  uint32_t texpage;        /* bits 0-8 (x base, y base, blend mode, depth) */
  uint32_t clut_x, clut_y;
  uint32_t mode;           /* semi-transparency mode */
} Style;

static const int8_t dither_table[4][4] = {
    {-4, 0, -3, 1},
    {2, -2, 3, -1},
    {-3, 1, -4, 0},
    {3, -1, 2, -2},
};

static inline int32_t sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }
static inline uint16_t *vram(Gpu *g, int32_t x, int32_t y) { return &g->vram[(y & 511) * VRAM_W + (x & 1023)]; }

static inline bool clipped(const Gpu *g, int32_t x, int32_t y) {
  return x < g->clip_x1 || x > g->clip_x2 || y < g->clip_y1 || y > g->clip_y2;
}

/* ---- textures ---- */

static uint16_t texel(Gpu *g, const Style *s, uint32_t u, uint32_t v) {
  /* Texture window: replace the masked bits with the offset. */
  uint32_t mx = (g->texwin & 31) * 8, my = ((g->texwin >> 5) & 31) * 8;
  uint32_t ox = ((g->texwin >> 10) & 31) * 8, oy = ((g->texwin >> 15) & 31) * 8;
  u = ((u & ~mx) | (ox & mx)) & 0xFF;
  v = ((v & ~my) | (oy & my)) & 0xFF;
  uint32_t bx = (s->texpage & 0xF) * 64, by = ((s->texpage >> 4) & 1) * 256;
  switch ((s->texpage >> 7) & 3) {
    case 0: {   /* 4-bit: four texels per VRAM word */
      uint16_t w = *vram(g, (int32_t)(bx + u / 4), (int32_t)(by + v));
      return g->clut_cache[(w >> ((u & 3) * 4)) & 0xF];
    }
    case 1: {   /* 8-bit */
      uint16_t w = *vram(g, (int32_t)(bx + u / 2), (int32_t)(by + v));
      return g->clut_cache[(w >> ((u & 1) * 8)) & 0xFF];
    }
    default:    /* 15-bit direct (mode 3 behaves like 2) */
      return *vram(g, (int32_t)(bx + u), (int32_t)(by + v));
  }
}

/* Load the palette into the CLUT cache unless it's already there: same
 * position, and at least as many entries as this depth needs (16 for
 * 4-bit, 256 for 8-bit). Drawing state changes (E1) don't touch the cache;
 * GP0(01) empties it. Matches ps1-tests' clut-cache reference. */
static void load_clut(Gpu *g, const Style *s) {
  if (!s->textured) return;
  uint32_t depth = (s->texpage >> 7) & 3;
  if (depth >= 2) return;
  uint32_t need = depth == 0 ? 16 : 256;
  if (g->clut_valid && g->clut_tag_x == s->clut_x && g->clut_tag_y == s->clut_y && g->clut_tag_depth >= need) return;
  for (uint32_t i = 0; i < need; i++) g->clut_cache[i] = *vram(g, (int32_t)(s->clut_x + i), (int32_t)s->clut_y);
  g->clut_tag_x = s->clut_x;
  g->clut_tag_y = s->clut_y;
  g->clut_tag_depth = need;   /* entries loaded */
  g->clut_valid = true;
}

/* ---- pixel output ---- */

static void plot(Gpu *g, const Style *s, int32_t x, int32_t y, int32_t r, int32_t gg, int32_t b, uint32_t u, uint32_t v) {
  uint16_t *d = vram(g, x, y);
  if (g->mask_check && (*d & 0x8000)) return;
  bool semi = s->semi;
  uint16_t mask_bit = 0;
  int32_t R, G, B;   /* 8-bit before dithering */
  if (s->textured) {
    uint16_t t = texel(g, s, u, v);
    if (t == 0) return;   /* transparent */
    semi = semi && (t & 0x8000);
    mask_bit = t & 0x8000;
    int32_t tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
    if (s->raw) {
      R = tr; G = tg; B = tb;
    } else {
      /* Modulation: 128 is 1.0. */
      R = (tr * r) >> 7; G = (tg * gg) >> 7; B = (tb * b) >> 7;
    }
  } else {
    R = r; G = gg; B = b;
  }
  if (s->dither && !(s->textured && s->raw)) {
    int32_t dd = dither_table[y & 3][x & 3];
    R += dd; G += dd; B += dd;
  }
  R = R < 0 ? 0 : R > 255 ? 255 : R;
  G = G < 0 ? 0 : G > 255 ? 255 : G;
  B = B < 0 ? 0 : B > 255 ? 255 : B;
  int32_t r5 = R >> 3, g5 = G >> 3, b5 = B >> 3;
  if (semi) {
    int32_t br = *d & 31, bg = (*d >> 5) & 31, bb = (*d >> 10) & 31;
    switch (s->mode) {
      case 0: r5 = (br + r5) >> 1; g5 = (bg + g5) >> 1; b5 = (bb + b5) >> 1; break;
      case 1: r5 = br + r5; g5 = bg + g5; b5 = bb + b5; break;
      case 2: r5 = br - r5; g5 = bg - g5; b5 = bb - b5; break;
      default: r5 = br + (r5 >> 2); g5 = bg + (g5 >> 2); b5 = bb + (b5 >> 2); break;
    }
    r5 = r5 < 0 ? 0 : r5 > 31 ? 31 : r5;
    g5 = g5 < 0 ? 0 : g5 > 31 ? 31 : g5;
    b5 = b5 < 0 ? 0 : b5 > 31 ? 31 : b5;
  }
  if (g->mask_set) mask_bit = 0x8000;
  *d = (uint16_t)(r5 | g5 << 5 | b5 << 10) | mask_bit;
}

/* ---- polygons ---- */

/* Attributes step across the triangle the way the GPU does it: fixed-point
 * gradients per pixel in x and y (FRAC fractional bits, truncated), added
 * to the first vertex's value. */
#define FRAC 12

static void triangle(Gpu *g, const Style *s, Vert a, Vert b, Vert c) {
  /* Too big for the hardware: skipped entirely. */
  int32_t minx = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
  int32_t maxx = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
  int32_t miny = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
  int32_t maxy = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
  if (maxx - minx > 1023 || maxy - miny > 511) return;

  int64_t area = (int64_t)(b.x - a.x) * (c.y - a.y) - (int64_t)(c.x - a.x) * (b.y - a.y);
  if (area == 0) return;
  if (area < 0) { Vert t = b; b = c; c = t; area = -area; }

  if (minx < g->clip_x1) minx = g->clip_x1;
  if (maxx > g->clip_x2) maxx = g->clip_x2;
  if (miny < g->clip_y1) miny = g->clip_y1;
  if (maxy > g->clip_y2) maxy = g->clip_y2;
  if (minx > maxx || miny > maxy) return;

  /* Gradients: dA/dx and dA/dy from the plane through the three vertices. */
  int32_t va[3][5] = {{a.r, a.g, a.b, a.u, a.v}, {b.r, b.g, b.b, b.u, b.v}, {c.r, c.g, c.b, c.u, c.v}};
  int64_t dx[5], dy[5], base[5];
  const int64_t bias = 1 << (FRAC - 1);   /* half a step: values round to nearest */
  for (int k = 0; k < 5; k++) {
    int64_t d1 = va[1][k] - va[0][k], d2 = va[2][k] - va[0][k];
    dx[k] = ((d1 * (c.y - a.y) - d2 * (b.y - a.y)) << FRAC) / area;
    dy[k] = ((d2 * (b.x - a.x) - d1 * (c.x - a.x)) << FRAC) / area;
    base[k] = ((int64_t)va[0][k] << FRAC) + bias;
  }

  /* Edge functions. A pixel exactly on an edge is drawn when the edge is
   * a right-hand or bottom one in this orientation (A > 0, or horizontal
   * with B > 0), so polygons sharing an edge don't overlap. This rule and
   * the rounding above match the console pixel for pixel in ps1-tests'
   * triangle, quad and uv-interpolation references. */
  const Vert *v[3] = {&a, &b, &c};
  int64_t A[3], B[3], C[3];
  bool tl[3];
  for (int i = 0; i < 3; i++) {
    const Vert *p0 = v[(i + 1) % 3], *p1 = v[(i + 2) % 3];
    A[i] = p0->y - p1->y;
    B[i] = p1->x - p0->x;
    C[i] = (int64_t)p0->x * p1->y - (int64_t)p0->y * p1->x;
    tl[i] = (A[i] == 0 && B[i] > 0) || A[i] > 0;
  }
  for (int32_t y = miny; y <= maxy; y++) {
    for (int32_t x = minx; x <= maxx; x++) {
      bool in = true;
      for (int i = 0; i < 3 && in; i++) {
        int64_t w = A[i] * x + B[i] * y + C[i];
        if (w < 0 || (w == 0 && !tl[i])) in = false;
      }
      if (!in) continue;
      int32_t at[5];
      for (int k = 0; k < 5; k++) at[k] = (int32_t)((base[k] + dx[k] * (x - a.x) + dy[k] * (y - a.y)) >> FRAC);
      int32_t r = s->gouraud ? at[0] : a.r, gg = s->gouraud ? at[1] : a.g, bb = s->gouraud ? at[2] : a.b;
      plot(g, s, x, y, r, gg, bb, (uint32_t)at[3], (uint32_t)at[4]);
    }
  }
}

static Vert vertex(const Gpu *g, uint32_t xy) {
  Vert v = {0};
  v.x = sext11(xy) + g->off_x;
  v.y = sext11(xy >> 16) + g->off_y;
  return v;
}

static void set_color(Vert *v, uint32_t c) {
  v->r = (int32_t)(c & 0xFF);
  v->g = (int32_t)((c >> 8) & 0xFF);
  v->b = (int32_t)((c >> 16) & 0xFF);
}

void raster_polygon(PS1 *p, const uint32_t *cmd) {
  Gpu *g = &p->gpu;
  uint32_t op = cmd[0] >> 24;
  bool gouraud = op & 0x10, quad = op & 0x08, tex = op & 0x04, semi = op & 0x02, raw = op & 0x01;
  int n = quad ? 4 : 3;
  Vert v[4];
  Style s = {0};
  uint32_t i = 0;
  uint32_t color = cmd[i++] & 0xFFFFFF;
  for (int k = 0; k < n; k++) {
    if (k > 0 && gouraud) color = cmd[i++] & 0xFFFFFF;
    v[k] = vertex(g, cmd[i++]);
    set_color(&v[k], color);
    if (tex) {
      uint32_t uv = cmd[i++];
      v[k].u = (int32_t)(uv & 0xFF);
      v[k].v = (int32_t)((uv >> 8) & 0xFF);
      if (k == 0) { s.clut_x = ((uv >> 16) & 0x3F) * 16; s.clut_y = (uv >> 22) & 0x1FF; }
      if (k == 1) {
        /* The polygon's texture page also becomes the current one. */
        uint32_t tp = uv >> 16;
        g->texpage = (g->texpage & ~0x9FFu) | (tp & 0x9FF);
      }
    }
  }
  s.textured = tex && !g->tex_disable;
  s.raw = raw;
  s.semi = semi;
  s.gouraud = gouraud;
  s.texpage = g->texpage;
  s.mode = (g->texpage >> 5) & 3;
  s.dither = ((g->texpage >> 9) & 1) && (gouraud || (tex && !raw));
  if (!tex) s.raw = false;
  load_clut(g, &s);
  triangle(g, &s, v[0], v[1], v[2]);
  if (quad) triangle(g, &s, v[1], v[2], v[3]);
  p->cpu.cycles += 64;
}

/* ---- lines ---- */

/* i * d / steps rounded to nearest, ties toward +inf (up) or -inf. */
static int32_t lerp_round(int32_t i, int32_t d, int32_t steps, bool ties_up) {
  int64_t n = 2 * (int64_t)i * d + (ties_up ? steps : steps - 1), den = 2 * (int64_t)steps;
  /* floor division */
  int64_t q = n / den;
  if ((n % den != 0) && ((n < 0) != (den < 0))) q--;
  return (int32_t)q;
}

/* Lines step one pixel at a time along the longer axis. The other axis is
 * i * d / steps rounded to nearest, with ties going left in x and down in
 * y; colours step in 12-bit fixed point from half a step. Both ends are
 * drawn. Matches the console pixel for pixel in ps1-tests' lines test. */
static void line(Gpu *g, const Style *s, Vert a, Vert b) {
  int32_t dx = b.x - a.x, dy = b.y - a.y;
  if (abs(dx) > 1023 || abs(dy) > 511) return;
  int32_t steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  int64_t cr = ((int64_t)a.r << 12) + 0x800, cg = ((int64_t)a.g << 12) + 0x800, cb = ((int64_t)a.b << 12) + 0x800;
  int64_t dr = steps ? ((int64_t)(b.r - a.r) << 12) / steps : 0;
  int64_t dg = steps ? ((int64_t)(b.g - a.g) << 12) / steps : 0;
  int64_t db = steps ? ((int64_t)(b.b - a.b) << 12) / steps : 0;
  for (int32_t i = 0; i <= steps; i++) {
    int32_t px = a.x + (steps ? lerp_round(i, dx, steps, false) : 0);
    int32_t py = a.y + (steps ? lerp_round(i, dy, steps, true) : 0);
    if (!clipped(g, px, py)) {
      int32_t r = a.r, gg = a.g, bb = a.b;
      if (s->gouraud) { r = (int32_t)(cr >> 12); gg = (int32_t)(cg >> 12); bb = (int32_t)(cb >> 12); }
      plot(g, s, px, py, r, gg, bb, 0, 0);
    }
    cr += dr; cg += dg; cb += db;
  }
}

void raster_line(PS1 *p, const uint32_t *cmd, uint32_t words) {
  Gpu *g = &p->gpu;
  uint32_t op = cmd[0] >> 24;
  bool gouraud = op & 0x10, poly = op & 0x08;
  Style s = {0};
  s.semi = op & 0x02;
  s.gouraud = gouraud;
  s.mode = (g->texpage >> 5) & 3;
  s.dither = (g->texpage >> 9) & 1;   /* lines dither even when flat-shaded */
  uint32_t stride = gouraud ? 2 : 1;
  uint32_t count = poly ? (words - (gouraud ? 0 : 1)) / stride : 2;
  if (!poly) count = 2;
  Vert prev = {0};
  for (uint32_t k = 0; k < count; k++) {
    uint32_t ci = gouraud ? k * 2 : 0, vi = gouraud ? k * 2 + 1 : k + 1;
    if (vi >= words) break;
    Vert v = vertex(g, cmd[vi]);
    set_color(&v, cmd[ci] & 0xFFFFFF);
    if (k > 0) line(g, &s, prev, v);
    prev = v;
  }
  p->cpu.cycles += 16 * count;
}

/* ---- rectangles ---- */

void raster_rect(PS1 *p, const uint32_t *cmd) {
  Gpu *g = &p->gpu;
  uint32_t op = cmd[0] >> 24;
  bool tex = op & 0x04;
  Style s = {0};
  s.semi = op & 0x02;
  s.raw = op & 0x01;
  s.textured = tex && !g->tex_disable;
  s.texpage = g->texpage;
  s.mode = (g->texpage >> 5) & 3;
  uint32_t i = 1;
  Vert o = vertex(g, cmd[i++]);
  int32_t r = (int32_t)(cmd[0] & 0xFF), gg = (int32_t)((cmd[0] >> 8) & 0xFF), b = (int32_t)((cmd[0] >> 16) & 0xFF);
  uint32_t u0 = 0, v0 = 0;
  if (tex) {
    uint32_t uv = cmd[i++];
    u0 = uv & 0xFF;
    v0 = (uv >> 8) & 0xFF;
    s.clut_x = ((uv >> 16) & 0x3F) * 16;
    s.clut_y = (uv >> 22) & 0x1FF;
  }
  int32_t w, h;
  switch ((op >> 3) & 3) {
    case 0: w = (int32_t)(cmd[i] & 0x3FF); h = (int32_t)((cmd[i] >> 16) & 0x1FF); break;
    case 1: w = h = 1; break;
    case 2: w = h = 8; break;
    default: w = h = 16; break;
  }
  load_clut(g, &s);
  bool flip_x = (g->texpage >> 12) & 1, flip_y = (g->texpage >> 13) & 1;
  for (int32_t y = 0; y < h; y++) {
    int32_t py = o.y + y;
    for (int32_t x = 0; x < w; x++) {
      int32_t px = o.x + x;
      if (clipped(g, px, py)) continue;
      /* X-flip starts one texel to the right (u0 + 1, u0, u0 - 1, ...) on
       * the console; Y-flip doesn't (ps1-tests texture-flip reference). */
      uint32_t u = (flip_x ? u0 + 1 - (uint32_t)x : u0 + (uint32_t)x) & 0xFF;
      uint32_t v = (flip_y ? v0 - (uint32_t)y : v0 + (uint32_t)y) & 0xFF;
      plot(g, &s, px, py, r, gg, b, u, v);
    }
  }
  p->cpu.cycles += (uint64_t)(w * h) / 2;
}
