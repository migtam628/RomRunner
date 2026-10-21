/*
 * Runs community N64 test cartridges on the core and reports the result.
 *
 *   romtest --r30 <rom>                       Dillon's n64-tests: r30 = -1 pass, >0 failed case
 *   romtest --krom <rom> <FontRed.bin> <FontGreen.bin>
 *                                             krom's tests: count "PASS" / "FAIL" drawn on screen
 *
 * Exit status 0 = pass, 1 = fail, 2 = timeout/bad input.
 * Prints one line: "<name> PASS|FAIL|TIMEOUT <details>"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/n64.h"

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = (uint8_t *)malloc((size_t)n);
  if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
  fclose(f);
  *size = (size_t)n;
  return d;
}

static const char *base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

/* ---- krom: glyph matching ---- */

static const uint8_t *font_red, *font_green;

/* Pixel (x,y) of character c in an 8x8, 32-bit font (256 bytes per char). */
static uint32_t glyph_px(const uint8_t *font, char c, int x, int y) {
  return rd32be(font + (uint8_t)c * 256 + (y * 8 + x) * 4);
}

static bool word_at(N64 *n, const char *word, const uint8_t *font, uint32_t x0, uint32_t y0) {
  uint32_t stride = n->vi.width, base = n->vi.origin & 0xFFFFFF;
  for (int i = 0; word[i]; i++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        uint32_t a = (base + ((y0 + y) * stride + x0 + i * 8 + x) * 4) & (RDRAM_SIZE - 4);
        if (rd32be(n->rdram + a) != glyph_px(font, word[i], x, y)) return false;
      }
  return true;
}

/* ROMTEST_PPM=<file> saves the final screen (for looking at a failure). */
static void dump_screen(N64 *n) {
  const char *path = getenv("ROMTEST_PPM");
  if (!path) return;
  static uint32_t px[640 * 480];
  uint32_t w, h;
  if (getenv("ROMTEST_RAW") && (n->vi.ctrl & 3) == 3 && n->vi.width <= 640) {
    /* The framebuffer as drawn (the tests draw 240 lines), for comparing
     * against the reference screenshots. */
    w = n->vi.width;
    h = 240;
    for (uint32_t i = 0; i < w * h; i++) px[i] = rd32be(n->rdram + ((n->vi.origin + i * 4) & (RDRAM_SIZE - 4))) >> 8;
  } else if (!vi_render(n, px, 640, 480, &w, &h)) {
    return;
  }
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (uint32_t i = 0; i < w * h; i++) {
    uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void count_words(N64 *n, int *pass, int *fail) {
  *pass = *fail = 0;
  if ((n->vi.ctrl & 3) != 3 || n->vi.width < 32) return;
  /* Text can sit at any pixel position; check each one (a glyph's first
   * pixel rejects almost every position at once). */
  uint32_t w = n->vi.width, h = 480;
  for (uint32_t y = 0; y + 8 <= h; y++)
    for (uint32_t x = 0; x + 32 <= w; x++) {
      if (word_at(n, "PASS", font_green, x, y)) { (*pass)++; x += 31; }
      else if (word_at(n, "FAIL", font_red, x, y)) { (*fail)++; x += 31; }
    }
}

int main(int argc, char **argv) {
  bool krom = argc == 5 && !strcmp(argv[1], "--krom");
  bool r30 = argc == 3 && !strcmp(argv[1], "--r30");
  if (!krom && !r30) {
    fprintf(stderr, "usage: romtest --r30 rom | --krom rom FontRed.bin FontGreen.bin\n");
    return 2;
  }
  size_t size, fs;
  uint8_t *data = read_file(argv[2], &size);
  if (!data) { printf("%s TIMEOUT cannot read\n", base_name(argv[2])); return 2; }
  if (krom) {
    font_red = read_file(argv[3], &fs);
    font_green = read_file(argv[4], &fs);
    if (!font_red || !font_green) { printf("%s TIMEOUT no fonts\n", base_name(argv[2])); return 2; }
  }

  N64 *n = (N64 *)calloc(1, sizeof(N64));
  uint8_t *rom = NULL;
  if (!n64_load_rom(n, data, size, &rom)) { printf("%s TIMEOUT not an N64 ROM\n", base_name(argv[2])); return 2; }
  n->pad_connected[0] = true;
  n64_reset(n);

  const char *name = base_name(argv[2]);
  int last_total = -1, stable = 0;
  for (int frame = 1; frame <= 1200; frame++) {
    n64_run_frame(n);
    if (r30) {
      uint64_t v = n->cpu.gpr[30];
      if (v == (uint64_t)-1) { printf("%s PASS\n", name); return 0; }
      if (v != 0) { printf("%s FAIL case %llu\n", name, (unsigned long long)v); return 1; }
    } else if (frame % 20 == 0) {
      int pass, fail;
      count_words(n, &pass, &fail);
      int total = pass + fail;
      stable = (total > 0 && total == last_total) ? stable + 1 : 0;
      last_total = total;
      if (stable >= 3) {
        dump_screen(n);
        printf("%s %s %d pass, %d fail\n", name, fail ? "FAIL" : "PASS", pass, fail);
        return fail ? 1 : 0;
      }
    }
  }
  if (krom) {
    int pass, fail;
    count_words(n, &pass, &fail);
    if (pass + fail > 0) {
      printf("%s %s %d pass, %d fail\n", name, fail ? "FAIL" : "PASS", pass, fail);
      return fail ? 1 : 0;
    }
  }
  dump_screen(n);
  printf("%s TIMEOUT pc=%08x\n", name, (uint32_t)n->cpu.pc);
  return 2;
}
