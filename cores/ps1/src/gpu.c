/* GPU: GP0 command assembly (drawing commands are rasterised in raster.c),
 * VRAM fills/copies/transfers, drawing state (E1-E6), GP1 display control,
 * GPUSTAT and turning VRAM into the displayed picture. */
#include <string.h>

#include "ps1.h"

void raster_polygon(PS1 *p, const uint32_t *cmd);
void raster_line(PS1 *p, const uint32_t *cmd, uint32_t words);
void raster_rect(PS1 *p, const uint32_t *cmd);

static inline uint16_t *px(Gpu *g, uint32_t x, uint32_t y) { return &g->vram[(y & 511) * VRAM_W + (x & 1023)]; }

void gpu_reset(PS1 *p) {
  Gpu *g = &p->gpu;   /* VRAM contents survive a GPU reset; only state clears */
  g->fifo_len = g->cmd_words = 0;
  g->poly_line = false;
  g->xfer_in = g->xfer_out = false;
  g->texpage = g->texwin = 0;
  g->clip_x1 = g->clip_y1 = g->clip_x2 = g->clip_y2 = 0;
  g->off_x = g->off_y = 0;
  g->mask_set = g->mask_check = false;
  g->tex_disable = false;
  g->display_off = true;
  g->dma_dir = 0;
  g->disp_x = g->disp_y = 0;
  g->h_start = 0x200;
  g->h_end = 0x200 + 256 * 10;
  g->v_start = 0x10;
  g->v_end = 0x10 + 240;
  g->hres = g->vres_bit = g->pal = g->color24 = g->interlace = g->hres2 = 0;
  g->irq = false;
}

/* Words in a GP0 command, from its first word. 0 = variable (poly-line). */
static uint32_t gp0_length(uint32_t cmd) {
  uint32_t op = cmd >> 24;
  if (op == 0x02) return 3;
  if (op >= 0x20 && op < 0x40) {
    uint32_t verts = (op & 0x08) ? 4 : 3;
    bool tex = op & 0x04, shade = op & 0x10;
    return 1 + verts * (1 + (tex ? 1 : 0)) + (shade ? verts - 1 : 0);
  }
  if (op >= 0x40 && op < 0x60) {
    if (op & 0x08) return 0;   /* poly-line: until the 0x5xxx5xxx terminator */
    return (op & 0x10) ? 4 : 3;
  }
  if (op >= 0x60 && op < 0x80) {
    uint32_t size = (op >> 3) & 3;
    return 2 + ((op & 0x04) ? 1 : 0) + (size == 0 ? 1 : 0);
  }
  if (op >= 0x80 && op < 0xA0) return 4;
  if (op >= 0xA0 && op < 0xE0) return 3;
  return 1;
}

static void fill(Gpu *g, const uint32_t *c) {
  /* Fill ignores the drawing area, offset and mask; x/w go in 16s. */
  uint32_t col = c[0];
  uint16_t v = (uint16_t)(((col >> 3) & 31) | (((col >> 11) & 31) << 5) | (((col >> 19) & 31) << 10));
  uint32_t x0 = c[1] & 0x3F0, y0 = (c[1] >> 16) & 0x1FF;
  uint32_t w = ((c[2] & 0x3FF) + 15) & ~15u, h = (c[2] >> 16) & 0x1FF;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) *px(g, x0 + x, y0 + y) = v;
}

static void copy_vram(Gpu *g, const uint32_t *c) {
  uint32_t sx = c[1] & 0x3FF, sy = (c[1] >> 16) & 0x1FF;
  uint32_t dx = c[2] & 0x3FF, dy = (c[2] >> 16) & 0x1FF;
  uint32_t w = ((c[3] - 1) & 0x3FF) + 1, h = (((c[3] >> 16) - 1) & 0x1FF) + 1;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) {
      uint16_t v = *px(g, sx + x, sy + y);
      uint16_t *d = px(g, dx + x, dy + y);
      if (g->mask_check && (*d & 0x8000)) continue;
      *d = v | (g->mask_set ? 0x8000 : 0);
    }
}

static void start_transfer(Gpu *g, const uint32_t *c, bool in) {
  g->xfer_x = c[1] & 0x3FF;
  g->xfer_y = (c[1] >> 16) & 0x1FF;
  g->xfer_w = ((c[2] - 1) & 0x3FF) + 1;
  g->xfer_h = (((c[2] >> 16) - 1) & 0x1FF) + 1;
  g->xfer_i = 0;
  g->xfer_n = g->xfer_w * g->xfer_h;
  g->xfer_in = in;
  g->xfer_out = !in;
}

static void transfer_pixel_in(Gpu *g, uint16_t v) {
  if (g->xfer_i >= g->xfer_n) return;
  uint32_t x = g->xfer_x + g->xfer_i % g->xfer_w, y = g->xfer_y + g->xfer_i / g->xfer_w;
  uint16_t *d = px(g, x, y);
  if (!(g->mask_check && (*d & 0x8000))) *d = v | (g->mask_set ? 0x8000 : 0);
  if (++g->xfer_i >= g->xfer_n) g->xfer_in = false;
}

static uint16_t transfer_pixel_out(Gpu *g) {
  if (g->xfer_i >= g->xfer_n) return 0;
  uint32_t x = g->xfer_x + g->xfer_i % g->xfer_w, y = g->xfer_y + g->xfer_i / g->xfer_w;
  uint16_t v = *px(g, x, y);
  if (++g->xfer_i >= g->xfer_n) g->xfer_out = false;
  return v;
}

static void state_command(PS1 *p, uint32_t v) {
  Gpu *g = &p->gpu;
  switch (v >> 24) {
    case 0xE1: g->texpage = v & 0x3FFF; g->tex_disable = (v >> 11) & 1; break;
    case 0xE2: g->texwin = v & 0xFFFFF; break;
    case 0xE3: g->clip_x1 = (int32_t)(v & 0x3FF); g->clip_y1 = (int32_t)((v >> 10) & 0x3FF); break;
    case 0xE4: g->clip_x2 = (int32_t)(v & 0x3FF); g->clip_y2 = (int32_t)((v >> 10) & 0x3FF); break;
    case 0xE5:
      g->off_x = ((int32_t)(v << 21)) >> 21;          /* signed 11-bit */
      g->off_y = ((int32_t)(v << 10)) >> 21;
      break;
    case 0xE6: g->mask_set = v & 1; g->mask_check = (v >> 1) & 1; break;
    default: break;
  }
}

static void execute(PS1 *p) {
  Gpu *g = &p->gpu;
  const uint32_t *c = g->fifo;
  uint32_t op = c[0] >> 24;
  if (op == 0x02) fill(g, c);
  else if (op == 0x1F) { g->irq = true; irq_raise(p, IRQ_GPU); }
  else if (op >= 0x20 && op < 0x40) raster_polygon(p, c);
  else if (op >= 0x40 && op < 0x60) raster_line(p, c, g->fifo_len);
  else if (op >= 0x60 && op < 0x80) raster_rect(p, c);
  else if (op >= 0x80 && op < 0xA0) copy_vram(g, c);
  else if (op >= 0xA0 && op < 0xC0) start_transfer(g, c, true);
  else if (op >= 0xC0 && op < 0xE0) start_transfer(g, c, false);
  else if (op >= 0xE1 && op <= 0xE6) state_command(p, c[0]);
}

void gp0_write(PS1 *p, uint32_t v) {
  Gpu *g = &p->gpu;
  if (g->xfer_in) {
    transfer_pixel_in(g, (uint16_t)v);
    transfer_pixel_in(g, (uint16_t)(v >> 16));
    return;
  }
  if (g->fifo_len == 0) {
    g->cmd_words = gp0_length(v);
    g->poly_line = g->cmd_words == 0;
  }
  if (g->poly_line) {
    /* The terminator (after at least two vertices) ends a poly-line. */
    bool shaded = (g->fifo[0] >> 24) & 0x10;
    uint32_t min = shaded ? 4 : 3;
    if (g->fifo_len >= min && (v & 0xF000F000u) == 0x50005000u) {
      execute(p);
      g->fifo_len = 0;
      g->poly_line = false;
      return;
    }
    if (g->fifo_len < sizeof g->fifo / sizeof g->fifo[0]) g->fifo[g->fifo_len++] = v;
    else {
      /* Long poly-lines: draw what's collected and keep the last vertex. */
      execute(p);
      uint32_t keep = shaded ? 2 : 1;
      g->fifo[1] = g->fifo[g->fifo_len - keep];
      if (shaded) g->fifo[2] = g->fifo[g->fifo_len - 1];
      g->fifo_len = 1 + keep;
      g->fifo[g->fifo_len++] = v;
    }
    return;
  }
  g->fifo[g->fifo_len++] = v;
  if (g->fifo_len >= g->cmd_words) {
    execute(p);
    g->fifo_len = 0;
  }
}

uint32_t gpu_read(PS1 *p) {
  Gpu *g = &p->gpu;
  if (g->xfer_out) {
    uint32_t lo = transfer_pixel_out(g);
    uint32_t hi = transfer_pixel_out(g);
    g->read_latch = lo | hi << 16;
  }
  return g->read_latch;
}

void gp1_write(PS1 *p, uint32_t v) {
  Gpu *g = &p->gpu;
  switch (v >> 24) {
    case 0x00: gpu_reset(p); break;
    case 0x01: g->fifo_len = 0; g->poly_line = false; g->xfer_in = false; break;
    case 0x02: g->irq = false; break;
    case 0x03: g->display_off = v & 1; break;
    case 0x04: g->dma_dir = v & 3; break;
    case 0x05: g->disp_x = v & 0x3FE; g->disp_y = (v >> 10) & 0x1FF; break;
    case 0x06: g->h_start = v & 0xFFF; g->h_end = (v >> 12) & 0xFFF; break;
    case 0x07: g->v_start = v & 0x3FF; g->v_end = (v >> 10) & 0x3FF; break;
    case 0x08:
      g->hres = v & 3;
      g->vres_bit = (v >> 2) & 1;
      g->pal = (v >> 3) & 1;
      g->color24 = (v >> 4) & 1;
      g->interlace = (v >> 5) & 1;
      g->hres2 = (v >> 6) & 1;
      break;
    case 0x09: break;   /* texture disable allowed */
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
      /* GPU info into GPUREAD */
      switch (v & 7) {
        case 2: g->read_latch = g->texwin; break;
        case 3: g->read_latch = (uint32_t)g->clip_x1 | (uint32_t)g->clip_y1 << 10; break;
        case 4: g->read_latch = (uint32_t)g->clip_x2 | (uint32_t)g->clip_y2 << 10; break;
        case 5: g->read_latch = ((uint32_t)g->off_x & 0x7FF) | ((uint32_t)g->off_y & 0x7FF) << 11; break;
        case 7: g->read_latch = 2; break;   /* GPU version */
        default: break;
      }
      break;
    default: break;
  }
}

uint32_t gpu_stat(PS1 *p) {
  Gpu *g = &p->gpu;
  uint32_t s = g->texpage & 0x7FF;
  s |= (uint32_t)g->mask_set << 11 | (uint32_t)g->mask_check << 12;
  s |= (uint32_t)(g->interlace ? g->field : 1) << 13;
  s |= (uint32_t)g->tex_disable << 15;
  s |= g->hres2 << 16 | g->hres << 17 | g->vres_bit << 19 | g->pal << 20 | g->color24 << 21 | g->interlace << 22;
  s |= (uint32_t)g->display_off << 23 | (uint32_t)g->irq << 24;
  /* Always ready: commands run at once. */
  bool ready_cmd = !g->xfer_in;
  bool ready_out = g->xfer_out;
  bool ready_dma = !g->xfer_out;
  s |= (uint32_t)ready_cmd << 26 | (uint32_t)ready_out << 27 | (uint32_t)ready_dma << 28;
  s |= g->dma_dir << 29;
  uint32_t req = g->dma_dir == 1 ? 1 : g->dma_dir == 2 ? ready_dma : g->dma_dir == 3 ? ready_out : 0;
  s |= req << 25;
  /* Bit 31: odd line being drawn (interlace: field; else alternates per line). */
  bool odd = g->interlace && g->vres_bit ? g->field : (p->line & 1);
  if (p->in_vblank) odd = false;
  s |= (uint32_t)odd << 31;
  return s;
}

/* The displayed area as XRGB8888. */
bool gpu_render(PS1 *p, uint32_t *out, uint32_t max_w, uint32_t max_h, uint32_t *w, uint32_t *h) {
  Gpu *g = &p->gpu;
  static const uint32_t widths[4] = {256, 320, 512, 640};
  uint32_t width = g->hres2 ? 368 : widths[g->hres];
  uint32_t height = g->v_end > g->v_start ? g->v_end - g->v_start : 240;
  if (height > 288) height = 288;
  if (g->interlace && g->vres_bit) height *= 2;
  if (width > max_w) width = max_w;
  if (height > max_h) height = max_h;
  *w = width;
  *h = height;
  if (g->display_off) {
    memset(out, 0, (size_t)width * height * 4);
    return true;
  }
  for (uint32_t y = 0; y < height; y++) {
    uint32_t *row = out + y * width;
    uint32_t vy = (g->disp_y + y) & 511;
    if (g->color24) {
      /* 24-bit: 3 bytes per pixel packed across 16-bit VRAM words. */
      const uint8_t *line = (const uint8_t *)&g->vram[vy * VRAM_W];
      for (uint32_t x = 0; x < width; x++) {
        uint32_t b = (g->disp_x * 2 + x * 3) & 2047;
        uint32_t r8 = line[b], g8 = line[(b + 1) & 2047], b8 = line[(b + 2) & 2047];
        row[x] = r8 << 16 | g8 << 8 | b8;
      }
    } else {
      for (uint32_t x = 0; x < width; x++) {
        uint16_t v = *px(g, g->disp_x + x, vy);
        uint32_t r = v & 31, gg = (v >> 5) & 31, b = (v >> 10) & 31;
        row[x] = (r << 3 | r >> 2) << 16 | (gg << 3 | gg >> 2) << 8 | (b << 3 | b >> 2);
      }
    }
  }
  return true;
}
