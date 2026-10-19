// Headless smoke test: loads a core and ROM, runs frames, presses Start,
// saves/loads a state and writes the final frame as a PPM image.
// Usage: headless <core> <rom> <out.ppm> [frames]
// Core options: RR_OPT="pcsx_rearmed_drc=disabled" headless ...
#include "../src/romrunner_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: %s core rom out.ppm [frames]\n", argv[0]); return 2; }
  int frames = argc > 4 ? atoi(argv[4]) : 300;
  rr_set_directories(".", ".");
  rr_set_audio_enabled(0);
  // Optional core options: RR_OPT="key=value;key2=value2"
  if (getenv("RR_OPT")) {
    char* opts = strdup(getenv("RR_OPT"));
    for (char* kv = strtok(opts, ";"); kv; kv = strtok(NULL, ";")) {
      char* eq = strchr(kv, '=');
      if (eq) { *eq = 0; rr_set_option(kv, eq + 1); }
    }
    free(opts);
  }
  if (rr_load_core(argv[1]) != 0) { fprintf(stderr, "core: %s\n", rr_last_error()); return 1; }
  printf("core: %s\n", rr_core_name());
  if (rr_load_game(argv[2]) != 0) { fprintf(stderr, "game: %s\n", rr_last_error()); return 1; }
  printf("fps %.3f, rate %.1f, aspect %.3f, max %dx%d\n", rr_get_fps(), rr_get_sample_rate(),
         rr_get_aspect_ratio(), rr_get_max_width(), rr_get_max_height());
  for (int i = 0; i < frames; i++) {
    rr_set_button(0, RR_BTN_START, (i / 30) % 2);   // tap Start periodically
    rr_run_frames(1);
  }
  if (rr_save_state("test.state") != 0) { fprintf(stderr, "save: %s\n", rr_last_error()); return 1; }
  rr_run_frames(30);
  if (rr_load_state("test.state") != 0) { fprintf(stderr, "load: %s\n", rr_last_error()); return 1; }
  rr_run_frames(1);
  int w = 0, h = 0;
  static unsigned char buf[1024 * 1024 * 4];
  long long n = rr_copy_frame(buf, sizeof buf, &w, &h);
  printf("frames produced: %lld, last %dx%d\n", n, w, h);
  if (n <= 0) return 1;
  FILE* f = fopen(argv[3], "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  long nonblack = 0;
  for (int i = 0; i < w * h; i++) {
    fwrite(buf + i * 4, 1, 3, f);
    if (buf[i*4] | buf[i*4+1] | buf[i*4+2]) nonblack++;
  }
  fclose(f);
  printf("non-black pixels: %ld of %d\n", nonblack, w * h);
  rr_unload();
  return 0;
}
