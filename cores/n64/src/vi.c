/* Video interface output: turns the framebuffer the VI is showing into
 * XRGB8888 at the game's source resolution. */
#include "n64.h"

static inline uint32_t c5(uint32_t v) { return (v << 3) | (v >> 2); }

bool vi_render(N64 *n, uint32_t *out, uint32_t max_w, uint32_t max_h, uint32_t *w, uint32_t *h) {
  Vi *v = &n->vi;
  uint32_t type = v->ctrl & 3;
  if (type < 2 || v->width == 0) return false;

  uint32_t hs = (v->h_video >> 16) & 0x3FF, he = v->h_video & 0x3FF;
  uint32_t vs = (v->v_video >> 16) & 0x3FF, ve = v->v_video & 0x3FF;
  uint32_t xs = v->x_scale & 0xFFF, ys = v->y_scale & 0xFFF;
  uint32_t width = he > hs ? (he - hs) * xs / 1024 : 0;
  uint32_t height = ve > vs ? ((ve - vs) / 2) * ys / 1024 : 0;
  if (width == 0) width = v->width;
  if (height == 0) height = v->width > 320 ? 480 : 240;
  if (width > v->width) width = v->width;
  if (width > max_w) width = max_w;
  if (height > max_h) height = max_h;

  uint32_t stride = v->width;
  uint32_t base = v->origin & 0xFFFFFF;
  for (uint32_t y = 0; y < height; y++) {
    uint32_t *row = out + y * width;
    if (type == 2) {
      for (uint32_t x = 0; x < width; x++) {
        uint32_t a = (base + (y * stride + x) * 2) & (RDRAM_SIZE - 2);
        uint16_t p = rd16be(n->rdram + a);
        row[x] = c5((p >> 11) & 31) << 16 | c5((p >> 6) & 31) << 8 | c5((p >> 1) & 31);
      }
    } else {
      for (uint32_t x = 0; x < width; x++) {
        uint32_t a = (base + (y * stride + x) * 4) & (RDRAM_SIZE - 4);
        row[x] = rd32be(n->rdram + a) >> 8;
      }
    }
  }
  *w = width;
  *h = height;
  return true;
}
