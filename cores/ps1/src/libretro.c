/* libretro API for the RomRunner PS1 core, so the app's bridge loads it
 * like any other core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "ps1.h"

static PS1 *g_p;
static uint8_t *g_bios, *g_exe;
static uint32_t g_frame[640 * 576];

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;

/* BIOS images tried in the system folder, most compatible first. */
static const char *const bios_names[] = {
    "scph5501.bin", "scph1001.bin", "scph7001.bin", "scph7003.bin", "scph5500.bin", "scph5502.bin",
    "scph7502.bin", "scph101.bin", "psxonpsp660.bin", "ps1_rom.bin", "openbios.bin", NULL,
};

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
  memset(info, 0, sizeof *info);
  info->library_name = "RomRunner PS1";
  info->library_version = "0.1";
  info->valid_extensions = "exe|psexe|psx";
  info->need_fullpath = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof *info);
  info->geometry.base_width = 320;
  info->geometry.base_height = 240;
  info->geometry.max_width = 640;
  info->geometry.max_height = 576;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  bool pal = g_p && g_p->pal;
  info->timing.fps = pal ? 49.76 : 59.29;
  info->timing.sample_rate = 44100.0;
}

RETRO_API void retro_set_environment(retro_environment_t cb) {
  env_cb = cb;
  bool no_game = false;
  cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }
RETRO_API void retro_init(void) {}
RETRO_API void retro_deinit(void) {}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
  if (g_p && port < 2) g_p->pad[port].connected = device != RETRO_DEVICE_NONE;
}

static uint8_t *load_bios(void) {
  const char *dir = NULL;
  if (!env_cb || !env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir) return NULL;
  char path[1024];
  for (int i = 0; bios_names[i]; i++) {
    snprintf(path, sizeof path, "%s/%s", dir, bios_names[i]);
    FILE *f = fopen(path, "rb");
    if (!f) continue;
    uint8_t *b = (uint8_t *)malloc(BIOS_SIZE);
    size_t n = b ? fread(b, 1, BIOS_SIZE, f) : 0;
    fclose(f);
    if (n == BIOS_SIZE) return b;
    free(b);
  }
  return NULL;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
  if (!game || !game->data) return false;
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (env_cb) env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  g_p = (PS1 *)calloc(1, sizeof(PS1));
  if (!g_p) return false;
  g_exe = (uint8_t *)malloc(game->size);
  if (!g_exe) { free(g_p); g_p = NULL; return false; }
  memcpy(g_exe, game->data, game->size);
  if (!ps1_load_exe(g_p, g_exe, game->size)) {
    free(g_exe); free(g_p);
    g_exe = NULL; g_p = NULL;
    return false;
  }
  g_bios = load_bios();
  if (g_bios) ps1_load_bios(g_p, g_bios, BIOS_SIZE);
  g_p->pad[0].connected = true;
  ps1_reset(g_p);
  return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
  (void)type; (void)info; (void)num;
  return false;
}

RETRO_API void retro_unload_game(void) {
  free(g_p); free(g_bios); free(g_exe);
  g_p = NULL; g_bios = NULL; g_exe = NULL;
}

RETRO_API unsigned retro_get_region(void) { return (g_p && g_p->pal) ? RETRO_REGION_PAL : RETRO_REGION_NTSC; }
RETRO_API void retro_reset(void) { if (g_p) ps1_reset(g_p); }

/* PS1 pad bits: Select L3 R3 Start Up Right Down Left, L2 R2 L1 R1 Triangle Circle Cross Square. */
static void read_input(void) {
  if (poll_cb) poll_cb();
  if (!input_cb) return;
  static const struct { unsigned id; uint16_t bit; } map[] = {
      {RETRO_DEVICE_ID_JOYPAD_SELECT, 1 << 0}, {RETRO_DEVICE_ID_JOYPAD_L3, 1 << 1},
      {RETRO_DEVICE_ID_JOYPAD_R3, 1 << 2},     {RETRO_DEVICE_ID_JOYPAD_START, 1 << 3},
      {RETRO_DEVICE_ID_JOYPAD_UP, 1 << 4},     {RETRO_DEVICE_ID_JOYPAD_RIGHT, 1 << 5},
      {RETRO_DEVICE_ID_JOYPAD_DOWN, 1 << 6},   {RETRO_DEVICE_ID_JOYPAD_LEFT, 1 << 7},
      {RETRO_DEVICE_ID_JOYPAD_L2, 1 << 8},     {RETRO_DEVICE_ID_JOYPAD_R2, 1 << 9},
      {RETRO_DEVICE_ID_JOYPAD_L, 1 << 10},     {RETRO_DEVICE_ID_JOYPAD_R, 1 << 11},
      {RETRO_DEVICE_ID_JOYPAD_X, 1 << 12},     {RETRO_DEVICE_ID_JOYPAD_A, 1 << 13},
      {RETRO_DEVICE_ID_JOYPAD_B, 1 << 14},     {RETRO_DEVICE_ID_JOYPAD_Y, 1 << 15},
  };
  for (unsigned port = 0; port < 2; port++) {
    uint16_t b = 0;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
      if (input_cb(port, RETRO_DEVICE_JOYPAD, 0, map[i].id)) b |= map[i].bit;
    g_p->pad[port].buttons = b;
  }
}

RETRO_API void retro_run(void) {
  if (!g_p) return;
  read_input();
  ps1_run_frame(g_p);
  uint32_t w = 0, h = 0;
  gpu_render(g_p, g_frame, 640, 576, &w, &h);
  if (video_cb) video_cb(g_frame, w, h, (size_t)w * 4);
  /* Silence until the SPU (stage 4): 735 stereo frames per video frame. */
  static int16_t silence[2 * 900];
  if (audio_batch_cb) audio_batch_cb(silence, g_p->pal ? 886 : 744);
}

/* Save states: the whole machine is one struct; pointers at its end are left out. */
RETRO_API size_t retro_serialize_size(void) { return offsetof(PS1, bios); }
RETRO_API bool retro_serialize(void *data, size_t size) {
  if (!g_p || size < offsetof(PS1, bios)) return false;
  memcpy(data, g_p, offsetof(PS1, bios));
  return true;
}
RETRO_API bool retro_unserialize(const void *data, size_t size) {
  if (!g_p || size < offsetof(PS1, bios)) return false;
  memcpy(g_p, data, offsetof(PS1, bios));
  return true;
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API void *retro_get_memory_data(unsigned id) { return (g_p && id == RETRO_MEMORY_SYSTEM_RAM) ? g_p->ram : NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { return (g_p && id == RETRO_MEMORY_SYSTEM_RAM) ? RAM_SIZE : 0; }
