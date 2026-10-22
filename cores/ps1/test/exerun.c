/* Runs a PS-EXE (test programs, homebrew) and reports what it printed.
 *
 *   exerun <program.exe> <frames> [bios.bin] [screenshot.ppm]
 *
 * Without a BIOS image the core answers console-output BIOS calls itself.
 * Prints the tty output, then "frames N, pc X". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ps1.h"

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  uint8_t *d = (uint8_t *)malloc((size_t)n);
  *size = fread(d, 1, (size_t)n, f);
  fclose(f);
  return d;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: exerun program.exe frames [bios.bin] [screen.ppm]\n");
    return 2;
  }
  size_t exe_size, bios_size = 0;
  uint8_t *exe = read_file(argv[1], &exe_size);
  if (!exe) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
  uint8_t *bios = argc > 3 && strcmp(argv[3], "-") ? read_file(argv[3], &bios_size) : NULL;

  PS1 *p = (PS1 *)calloc(1, sizeof(PS1));
  if (!ps1_load_exe(p, exe, exe_size)) { fprintf(stderr, "not a PS-EXE\n"); return 2; }
  if (bios && !ps1_load_bios(p, bios, bios_size)) { fprintf(stderr, "BIOS must be 512 KB\n"); return 2; }
  p->pad[0].connected = true;
  ps1_reset(p);
  int frames = atoi(argv[2]);
  for (int f = 0; f < frames; f++) ps1_run_frame(p);
  fwrite(p->tty, 1, p->tty_len, stdout);
  {
    static uint32_t tmp[640 * 576];
    uint32_t w = 0, h = 0;
    gpu_render(p, tmp, 640, 576, &w, &h);
    printf("\ndisplay %u %u %u %u\n", p->gpu.disp_x, p->gpu.disp_y, w, h);
  }
  printf("frames %d, pc %08x\n", frames, p->cpu.pc);

  /* RRPS1_VRAM=<file.ppm>: the whole 1024x512 VRAM (for the GPU tests'
   * reference images). */
  const char *vpath = getenv("RRPS1_VRAM");
  if (vpath) {
    FILE *f = fopen(vpath, "wb");
    if (f) {
      fprintf(f, "P6\n%d %d\n255\n", VRAM_W, VRAM_H);
      for (int i = 0; i < VRAM_W * VRAM_H; i++) {
        uint16_t v = p->gpu.vram[i];
        uint8_t rgb[3] = {(uint8_t)((v & 31) << 3), (uint8_t)(((v >> 5) & 31) << 3), (uint8_t)(((v >> 10) & 31) << 3)};
        fwrite(rgb, 1, 3, f);
      }
      fclose(f);
    }
  }
  if (argc > 4) {
    static uint32_t px[640 * 576];
    uint32_t w, h;
    gpu_render(p, px, 640, 576, &w, &h);
    FILE *f = fopen(argv[4], "wb");
    if (f) {
      fprintf(f, "P6\n%u %u\n255\n", w, h);
      for (uint32_t i = 0; i < w * h; i++) {
        uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
        fwrite(rgb, 1, 3, f);
      }
      fclose(f);
    }
  }
  free(p);
  free(exe);
  free(bios);
  return 0;
}
