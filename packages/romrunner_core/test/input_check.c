// End-to-end input check through the app's bridge: runs a ROM twice, once
// idle and once holding a button, and fails unless the picture differs.
// Catches input getting lost anywhere between rr_set_buttons and the game.
//
//   input_check <core> <rom> <button mask hex> [frames] [option=value]...
#include "../src/romrunner_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char frame_a[1024 * 1024 * 4], frame_b[1024 * 1024 * 4];

static void run(const char *core, const char *rom, unsigned mask, int frames, unsigned char *out, int *w, int *h,
                int nopt, char **opts) {
  rr_set_directories(".", ".");
  rr_set_audio_enabled(0);
  for (int i = 0; i < nopt; i++) {
    char kv[256];
    snprintf(kv, sizeof kv, "%s", opts[i]);
    char *eq = strchr(kv, '=');
    if (eq) { *eq = 0; rr_set_option(kv, eq + 1); }
  }
  if (rr_load_core(core) || rr_load_game(rom)) { fprintf(stderr, "%s\n", rr_last_error()); exit(2); }
  for (int f = 0; f < frames; f++) {
    rr_set_buttons(0, f >= 30 ? mask : 0);   // press once the game is running
    rr_run_frames(1);
  }
  rr_copy_frame(out, (int32_t)sizeof frame_a, w, h);
  rr_unload();
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: input_check core rom mask [frames] [opt=val]...\n"); return 2; }
  unsigned mask = (unsigned)strtoul(argv[3], NULL, 16);
  int frames = argc > 4 ? atoi(argv[4]) : 240;
  int nopt = argc > 5 ? argc - 5 : 0;
  int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
  run(argv[1], argv[2], 0, frames, frame_a, &w1, &h1, nopt, argv + 5);
  run(argv[1], argv[2], mask, frames, frame_b, &w2, &h2, nopt, argv + 5);
  if (w1 != w2 || h1 != h2 || w1 <= 0) { printf("FAIL: no comparable frames\n"); return 1; }
  long diff = 0;
  for (long i = 0; i < (long)w1 * h1 * 4; i++) diff += frame_a[i] != frame_b[i];
  printf("%s: holding %x changed %ld bytes of the picture\n", diff ? "PASS" : "FAIL", mask, diff);
  return diff ? 0 : 1;
}
