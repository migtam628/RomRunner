/* Runs a game with scripted input and saves screenshots, for checking how
 * far real games get and how fast they run.
 *
 *   play <rom> <frames> [press=<frame>:<buttons hex>[:<length>]]... [shot=<frame>:<file.ppm>]...
 *        [save=<file>]   load/store the save memory (EEPROM, SRAM or FlashRAM)
 *
 * Buttons use the N64 layout: A 8000, B 4000, Z 2000, Start 1000,
 * D-pad U/D/L/R 0800/0400/0200/0100, L 0020, R 0010, C U/D/L/R 0008/0004/0002/0001.
 * Prints the emulation speed (frames per second of host time). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/n64.h"

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

static void save_ppm(N64 *n, const char *path) {
  static uint32_t px[640 * 576];
  uint32_t w = 0, h = 0;
  if (!vi_render(n, px, 640, 576, &w, &h) || !w || !h) return;
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (uint32_t i = 0; i < w * h; i++) {
    uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static uint8_t *save_mem(N64 *n, size_t *len) {
  switch (n->save_type) {
    case SAVE_EEPROM_4K: *len = 512; return n->eeprom;
    case SAVE_EEPROM_16K: *len = 2048; return n->eeprom;
    case SAVE_SRAM: *len = 0x8000; return n->sram;
    case SAVE_FLASH: *len = 0x20000; return n->sram;
    default: *len = 0; return NULL;
  }
}

#define MAX_EV 256
typedef struct { int frame, len; uint16_t buttons; } Press;
typedef struct { int frame; const char *path; } Shot;

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: play rom frames [press=f:hex[:len]]... [shot=f:file.ppm]... [save=file]\n");
    return 2;
  }
  size_t size;
  uint8_t *data = read_file(argv[1], &size);
  if (!data) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
  int frames = atoi(argv[2]);
  Press press[MAX_EV];
  Shot shot[MAX_EV];
  int np = 0, ns = 0;
  const char *save = NULL;
  for (int i = 3; i < argc; i++) {
    if (!strncmp(argv[i], "press=", 6) && np < MAX_EV) {
      Press p = {0, 6, 0};
      unsigned b = 0;
      sscanf(argv[i] + 6, "%d:%x:%d", &p.frame, &b, &p.len);
      p.buttons = (uint16_t)b;
      press[np++] = p;
    } else if (!strncmp(argv[i], "shot=", 5) && ns < MAX_EV) {
      char *colon = strchr(argv[i] + 5, ':');
      if (!colon) continue;
      shot[ns].frame = atoi(argv[i] + 5);
      shot[ns++].path = colon + 1;
    } else if (!strncmp(argv[i], "save=", 5)) {
      save = argv[i] + 5;
    }
  }

  N64 *n = (N64 *)calloc(1, sizeof(N64));
  uint8_t *rom = NULL;
  if (!n64_load_rom(n, data, size, &rom)) { fprintf(stderr, "not an N64 ROM\n"); return 2; }
  size_t slen;
  uint8_t *smem = save_mem(n, &slen);
  if (save && smem) {
    size_t got;
    uint8_t *old = read_file(save, &got);
    if (old) { memcpy(smem, old, got < slen ? got : slen); free(old); }
  }
  n64_reset(n);
  n->pad_connected[0] = true;
  if (getenv("PLAY_RDP_THREAD")) rdp_thread_start(n);

  struct timespec ts0, ts1;
  timespec_get(&ts0, TIME_UTC);
  for (int f = 0; f < frames; f++) {
    uint16_t b = 0;
    for (int i = 0; i < np; i++)
      if (f >= press[i].frame && f < press[i].frame + press[i].len) b |= press[i].buttons;
    n->pad[0].buttons = b;
    n64_run_frame(n);
    for (int i = 0; i < ns; i++)
      if (shot[i].frame == f) { rdp_thread_wait(n); save_ppm(n, shot[i].path); }
  }
  rdp_thread_stop(n);
  timespec_get(&ts1, TIME_UTC);
  double secs = (double)(ts1.tv_sec - ts0.tv_sec) + (ts1.tv_nsec - ts0.tv_nsec) / 1e9;
  printf("%d frames in %.2f s: %.1f fps (%.0f%% of full speed)\n", frames, secs, frames / secs,
         100.0 * frames / secs / (n->pal ? 50.0 : 60.0));
  if (save && smem && n->save_dirty) {
    FILE *f = fopen(save, "wb");
    if (f) { fwrite(smem, 1, slen, f); fclose(f); printf("save written to %s\n", save); }
  }
  free(n);
  free(rom);
  free(data);
  return 0;
}
