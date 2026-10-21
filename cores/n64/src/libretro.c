/* libretro API for the RomRunner N64 core, so the app's bridge loads it
 * like any other core. */
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "n64.h"

#define MAX_W 640
#define MAX_H 480

static N64 *g_n;
static uint8_t *g_rom;
static uint32_t g_fb[MAX_W * MAX_H];
static uint32_t g_w = 320, g_h = 240;

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;

RETRO_API void retro_set_environment(retro_environment_t cb) {
  env_cb = cb;
  static const struct retro_variable vars[] = {
      {"rrn64_dpad_stick", "D-pad moves the analog stick; on|off"},
      {NULL, NULL},
  };
  cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
  memset(info, 0, sizeof *info);
  info->library_name = "RomRunner N64";
  info->library_version = "0.1 (stage 1)";
  info->valid_extensions = "n64|z64|v64";
  info->need_fullpath = false;
  info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof *info);
  info->geometry.base_width = 320;
  info->geometry.base_height = 240;
  info->geometry.max_width = MAX_W;
  info->geometry.max_height = MAX_H;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  info->timing.fps = (g_n && g_n->pal) ? 50.0 : 60.0;
  info->timing.sample_rate = AUDIO_RATE;
}

RETRO_API void retro_init(void) {}
RETRO_API void retro_deinit(void) {}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
  if (g_n && port < 4) g_n->pad_connected[port] = device != RETRO_DEVICE_NONE;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
  if (!game || !game->data) return false;
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (env_cb) env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  g_n = (N64 *)calloc(1, sizeof(N64));
  if (!g_n) return false;
  if (!n64_load_rom(g_n, (const uint8_t *)game->data, game->size, &g_rom)) {
    free(g_n);
    g_n = NULL;
    return false;
  }
  g_n->pad_connected[0] = true;
  n64_reset(g_n);
  return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
  (void)type; (void)info; (void)num;
  return false;
}

RETRO_API void retro_unload_game(void) {
  free(g_n);
  free(g_rom);
  g_n = NULL;
  g_rom = NULL;
}

RETRO_API unsigned retro_get_region(void) { return (g_n && g_n->pal) ? RETRO_REGION_PAL : RETRO_REGION_NTSC; }

RETRO_API void retro_reset(void) { if (g_n) n64_reset(g_n); }

/* N64 controller bits */
enum {
  N_A = 0x8000, N_B = 0x4000, N_Z = 0x2000, N_START = 0x1000, N_DU = 0x0800, N_DD = 0x0400, N_DL = 0x0200, N_DR = 0x0100,
  N_L = 0x0020, N_R = 0x0010, N_CU = 0x0008, N_CD = 0x0004, N_CL = 0x0002, N_CR = 0x0001
};

static bool dpad_stick(void) {
  struct retro_variable v = {"rrn64_dpad_stick", NULL};
  if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) return strcmp(v.value, "off") != 0;
  return true;
}

static void read_input(void) {
  if (poll_cb) poll_cb();
  if (!input_cb) return;
  bool stick_from_dpad = dpad_stick();
  for (unsigned p = 0; p < 4; p++) {
    if (!g_n->pad_connected[p]) continue;
#define B(id) input_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_##id)
    uint16_t b = 0;
    if (B(B)) b |= N_A;
    if (B(Y)) b |= N_B;
    if (B(L2) || B(R2)) b |= N_Z;
    if (B(START)) b |= N_START;
    if (B(L)) b |= N_L;
    if (B(R)) b |= N_R;
    if (B(X)) b |= N_CU;
    if (B(A)) b |= N_CR;
    if (B(SELECT)) b |= N_CD;
    if (B(L3)) b |= N_CL;
    bool du = B(UP), dd = B(DOWN), dl = B(LEFT), dr = B(RIGHT);
#undef B
    int lx = input_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
    int ly = input_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
    int rx = input_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
    int ry = input_cb(p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
    if (rx > 16000) b |= N_CR;
    if (rx < -16000) b |= N_CL;
    if (ry > 16000) b |= N_CD;
    if (ry < -16000) b |= N_CU;
    int x = lx * 80 / 32767, y = -ly * 80 / 32767;  /* N64 sticks reach about +-80 */
    if (stick_from_dpad && lx == 0 && ly == 0) {
      if (dl) x = -80;
      if (dr) x = 80;
      if (du) y = 80;
      if (dd) y = -80;
    } else {
      if (du) b |= N_DU;
      if (dd) b |= N_DD;
      if (dl) b |= N_DL;
      if (dr) b |= N_DR;
    }
    g_n->pad[p].buttons = b;
    g_n->pad[p].x = (int8_t)(x < -128 ? -128 : x > 127 ? 127 : x);
    g_n->pad[p].y = (int8_t)(y < -128 ? -128 : y > 127 ? 127 : y);
  }
}

RETRO_API void retro_run(void) {
  if (!g_n) return;
  read_input();
  g_n->audio_frames = 0;
  n64_run_frame(g_n);

  uint32_t w, h;
  if (vi_render(g_n, g_fb, MAX_W, MAX_H, &w, &h)) {
    g_w = w;
    g_h = h;
  } else {
    memset(g_fb, 0, sizeof g_fb);
  }
  if (video_cb) video_cb(g_fb, g_w, g_h, g_w * sizeof(uint32_t));
  if (audio_batch_cb && g_n->audio_frames) audio_batch_cb(g_n->audio_buf, g_n->audio_frames);
}

/* Save states: the whole machine is one struct; the cartridge pointer at
 * its end is left out. */
RETRO_API size_t retro_serialize_size(void) { return offsetof(N64, rom); }

RETRO_API bool retro_serialize(void *data, size_t size) {
  if (!g_n || size < offsetof(N64, rom)) return false;
  memcpy(data, g_n, offsetof(N64, rom));
  return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size) {
  if (!g_n || size < offsetof(N64, rom)) return false;
  memcpy(g_n, data, offsetof(N64, rom));
  return true;
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

RETRO_API void *retro_get_memory_data(unsigned id) {
  if (!g_n) return NULL;
  if (id == RETRO_MEMORY_SAVE_RAM) {
    switch (g_n->save_type) {
      case SAVE_EEPROM_4K: case SAVE_EEPROM_16K: return g_n->eeprom;
      case SAVE_SRAM: case SAVE_FLASH: return g_n->sram;
      default: return NULL;
    }
  }
  if (id == RETRO_MEMORY_SYSTEM_RAM) return g_n->rdram;
  return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id) {
  if (!g_n) return 0;
  if (id == RETRO_MEMORY_SAVE_RAM) {
    switch (g_n->save_type) {
      case SAVE_EEPROM_4K: return 512;
      case SAVE_EEPROM_16K: return 2048;
      case SAVE_SRAM: return 0x8000;
      case SAVE_FLASH: return 0x20000;
      default: return 0;
    }
  }
  if (id == RETRO_MEMORY_SYSTEM_RAM) return RDRAM_SIZE;
  return 0;
}

/* For tests: direct access to the machine. */
RETRO_API N64 *rrn64_machine(void) { return g_n; }
