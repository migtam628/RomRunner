// RomRunner core bridge — a minimal libretro frontend.
// One core and one game at a time. All state is global on purpose: the app
// only ever runs a single emulator instance.

#include "romrunner_core.h"
#include "third_party/libretro.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <mmsystem.h>
  typedef HMODULE rr_lib_t;
  typedef HANDLE rr_thread_t;
  typedef CRITICAL_SECTION rr_mutex_t;
#else
  #include <dlfcn.h>
  #include <pthread.h>
  #include <time.h>
  #include <unistd.h>
  typedef void* rr_lib_t;
  typedef pthread_t rr_thread_t;
  typedef pthread_mutex_t rr_mutex_t;
#endif

#if defined(__ANDROID__)
  #include <android/log.h>
#endif

#ifndef RR_NO_AUDIO
  #define MINIAUDIO_IMPLEMENTATION
  #define MA_NO_DECODING
  #define MA_NO_ENCODING
  #define MA_NO_GENERATION
  #define MA_NO_RESOURCE_MANAGER
  #define MA_NO_NODE_GRAPH
  #define MA_NO_ENGINE
  #include "third_party/miniaudio.h"
#endif

/* ------------------------------------------------------------------ */
/* Platform helpers                                                    */
/* ------------------------------------------------------------------ */

static void rr_log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#if defined(__ANDROID__)
  __android_log_vprint(ANDROID_LOG_INFO, "RomRunner", fmt, ap);
#else
  vfprintf(stderr, fmt, ap);
#endif
  va_end(ap);
}

static void mutex_init(rr_mutex_t* m) {
#if defined(_WIN32)
  InitializeCriticalSection(m);
#else
  pthread_mutex_init(m, NULL);
#endif
}
static void mutex_lock(rr_mutex_t* m) {
#if defined(_WIN32)
  EnterCriticalSection(m);
#else
  pthread_mutex_lock(m);
#endif
}
static void mutex_unlock(rr_mutex_t* m) {
#if defined(_WIN32)
  LeaveCriticalSection(m);
#else
  pthread_mutex_unlock(m);
#endif
}

static double now_seconds(void) {
#if defined(_WIN32)
  static LARGE_INTEGER freq;
  LARGE_INTEGER t;
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t);
  return (double)t.QuadPart / (double)freq.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static void sleep_ms(int ms) {
#if defined(_WIN32)
  Sleep(ms);
#else
  usleep((useconds_t)ms * 1000);
#endif
}

static FILE* rr_fopen(const char* path, const char* mode) {
#if defined(_WIN32)
  wchar_t wpath[MAX_PATH * 2], wmode[8];
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH * 2);
  MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
  return _wfopen(wpath, wmode);
#else
  return fopen(path, mode);
#endif
}

static rr_lib_t lib_open(const char* path) {
#if defined(_WIN32)
  wchar_t wpath[MAX_PATH * 2];
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH * 2);
  return LoadLibraryW(wpath);
#else
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
static void* lib_sym(rr_lib_t lib, const char* name) {
#if defined(_WIN32)
  return (void*)GetProcAddress(lib, name);
#else
  return dlsym(lib, name);
#endif
}
static void lib_close(rr_lib_t lib) {
#if defined(_WIN32)
  FreeLibrary(lib);
#else
  dlclose(lib);
#endif
}

static unsigned char* read_file(const char* path, size_t* size) {
  FILE* f = rr_fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len < 0) { fclose(f); return NULL; }
  unsigned char* buf = (unsigned char*)malloc(len > 0 ? (size_t)len : 1);
  if (buf && len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf) *size = (size_t)len;
  return buf;
}

static int write_file(const char* path, const void* data, size_t size) {
  FILE* f = rr_fopen(path, "wb");
  if (!f) return -1;
  size_t n = fwrite(data, 1, size, f);
  fclose(f);
  return n == size ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
  void (*init)(void);
  void (*deinit)(void);
  unsigned (*api_version)(void);
  void (*get_system_info)(struct retro_system_info*);
  void (*get_system_av_info)(struct retro_system_av_info*);
  void (*set_environment)(retro_environment_t);
  void (*set_video_refresh)(retro_video_refresh_t);
  void (*set_audio_sample)(retro_audio_sample_t);
  void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*set_input_poll)(retro_input_poll_t);
  void (*set_input_state)(retro_input_state_t);
  void (*set_controller_port_device)(unsigned, unsigned);
  void (*reset)(void);
  void (*run)(void);
  size_t (*serialize_size)(void);
  bool (*serialize)(void*, size_t);
  bool (*unserialize)(const void*, size_t);
  bool (*load_game)(const struct retro_game_info*);
  void (*unload_game)(void);
  void* (*get_memory_data)(unsigned);
  size_t (*get_memory_size)(unsigned);
} core_api_t;

#define MAX_OPTIONS 256
typedef struct { char* key; char* value; bool user_set; } option_t;

static struct {
  rr_lib_t lib;
  core_api_t api;
  bool core_inited;
  bool game_loaded;
  char core_name[128];

  char system_dir[1024];
  char save_dir[1024];
  char sram_path[1600];

  unsigned char* rom_data;   // kept alive while the game is loaded
  char rom_path[1024];

  struct retro_system_av_info av;
  enum retro_pixel_format pixel_format;
  struct retro_frame_time_callback frame_time_cb;
  bool has_frame_time_cb;

  option_t options[MAX_OPTIONS];
  int option_count;
  bool options_dirty;

  // Video: back buffer written on the emu thread, front copied to Dart.
  rr_mutex_t frame_mutex;
  uint8_t* back;
  uint8_t* front;
  size_t frame_cap;
  int front_w, front_h;
  int64_t frame_counter;

  // Input
  volatile uint32_t buttons[4];

  // Loop
  rr_mutex_t core_mutex;
  rr_thread_t thread;
  volatile int running;
  volatile int paused;
  volatile int fast_forward;
  volatile int audio_enabled;
  volatile int av_changed;
  bool thread_started;

  char error[512];
  bool mutexes_ready;

#ifndef RR_NO_AUDIO
  ma_device device;
  ma_pcm_rb ring;
  bool audio_ready;
#endif
} g;

static void set_error(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g.error, sizeof g.error, fmt, ap);
  va_end(ap);
  rr_log("[romrunner] error: %s\n", g.error);
}

static void ensure_mutexes(void) {
  if (g.mutexes_ready) return;
  mutex_init(&g.frame_mutex);
  mutex_init(&g.core_mutex);
  g.mutexes_ready = true;
  g.audio_enabled = 1;
}

/* ------------------------------------------------------------------ */
/* Core options                                                        */
/* ------------------------------------------------------------------ */

static option_t* find_option(const char* key) {
  for (int i = 0; i < g.option_count; i++)
    if (strcmp(g.options[i].key, key) == 0) return &g.options[i];
  return NULL;
}

static void put_option(const char* key, const char* value, bool user) {
  option_t* o = find_option(key);
  if (o) {
    if (o->user_set && !user) return;  // never overwrite the user's choice
    free(o->value);
  } else {
    if (g.option_count >= MAX_OPTIONS) return;
    o = &g.options[g.option_count++];
    o->key = strdup(key);
    o->user_set = false;
  }
  o->value = strdup(value);
  o->user_set = o->user_set || user;
  g.options_dirty = true;
}

static void clear_core_defaults(void) {
  // Keep user-set options across games; drop core defaults.
  int w = 0;
  for (int i = 0; i < g.option_count; i++) {
    if (g.options[i].user_set) { g.options[w++] = g.options[i]; }
    else { free(g.options[i].key); free(g.options[i].value); }
  }
  g.option_count = w;
}

// "Description; first|second|third" -> default = "first"
static void register_variable(const struct retro_variable* v) {
  const char* semi = strchr(v->value, ';');
  if (!semi) return;
  semi++;
  while (*semi == ' ') semi++;
  const char* bar = strchr(semi, '|');
  size_t len = bar ? (size_t)(bar - semi) : strlen(semi);
  char buf[256];
  if (len >= sizeof buf) len = sizeof buf - 1;
  memcpy(buf, semi, len);
  buf[len] = 0;
  put_option(v->key, buf, false);
}

/* ------------------------------------------------------------------ */
/* libretro callbacks                                                  */
/* ------------------------------------------------------------------ */

static void RETRO_CALLCONV core_log(enum retro_log_level level, const char* fmt, ...) {
  if (level < RETRO_LOG_WARN) return;
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rr_log("[core] %s", buf);
}

static bool RETRO_CALLCONV env_cb(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool*)data = true;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      enum retro_pixel_format fmt = *(const enum retro_pixel_format*)data;
      if (fmt > RETRO_PIXEL_FORMAT_RGB565) return false;
      g.pixel_format = fmt;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char**)data = g.system_dir[0] ? g.system_dir : NULL;
      return g.system_dir[0] != 0;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char**)data = g.save_dir[0] ? g.save_dir : NULL;
      return g.save_dir[0] != 0;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback*)data)->log = core_log;
      return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
      const struct retro_variable* v = (const struct retro_variable*)data;
      for (; v && v->key; v++) register_variable(v);
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* v = (struct retro_variable*)data;
      option_t* o = find_option(v->key);
      v->value = o ? o->value : NULL;
      return o != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool*)data = g.options_dirty;
      g.options_dirty = false;
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      return false;  // forces cores onto SET_VARIABLES, which we parse
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
      g.frame_time_cb = *(const struct retro_frame_time_callback*)data;
      g.has_frame_time_cb = true;
      return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      g.av = *(const struct retro_system_av_info*)data;
      g.av_changed = 1;
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
      g.av.geometry = *(const struct retro_game_geometry*)data;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      if (data) *(int*)data = 3;
      return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
      if (data) *(bool*)data = g.fast_forward != 0;
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      *(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
      return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
      const struct retro_message* m = (const struct retro_message*)data;
      if (m && m->msg) rr_log("[core message] %s\n", m->msg);
      return true;
    }
    default:
      return false;  // HW rendering, VFS, rumble, etc. are not offered
  }
}

static void convert_frame(const void* data, unsigned w, unsigned h, size_t pitch) {
  size_t need = (size_t)w * h * 4;
  if (need > g.frame_cap) {
    mutex_lock(&g.frame_mutex);
    free(g.back);
    free(g.front);
    g.back = (uint8_t*)calloc(1, need);
    g.front = (uint8_t*)calloc(1, need);
    g.frame_cap = g.back && g.front ? need : 0;
    mutex_unlock(&g.frame_mutex);
    if (!g.frame_cap) return;
  }
  uint8_t* out = g.back;
  for (unsigned y = 0; y < h; y++) {
    const uint8_t* row = (const uint8_t*)data + y * pitch;
    uint8_t* o = out + (size_t)y * w * 4;
    switch (g.pixel_format) {
      case RETRO_PIXEL_FORMAT_XRGB8888: {
        const uint32_t* p = (const uint32_t*)row;
        for (unsigned x = 0; x < w; x++) {
          uint32_t c = p[x];
          o[0] = (c >> 16) & 0xFF; o[1] = (c >> 8) & 0xFF; o[2] = c & 0xFF; o[3] = 0xFF;
          o += 4;
        }
        break;
      }
      case RETRO_PIXEL_FORMAT_RGB565: {
        const uint16_t* p = (const uint16_t*)row;
        for (unsigned x = 0; x < w; x++) {
          uint16_t c = p[x];
          uint8_t r = (c >> 11) & 0x1F, gg = (c >> 5) & 0x3F, b = c & 0x1F;
          o[0] = (r << 3) | (r >> 2); o[1] = (gg << 2) | (gg >> 4); o[2] = (b << 3) | (b >> 2); o[3] = 0xFF;
          o += 4;
        }
        break;
      }
      default: {  // 0RGB1555
        const uint16_t* p = (const uint16_t*)row;
        for (unsigned x = 0; x < w; x++) {
          uint16_t c = p[x];
          uint8_t r = (c >> 10) & 0x1F, gg = (c >> 5) & 0x1F, b = c & 0x1F;
          o[0] = (r << 3) | (r >> 2); o[1] = (gg << 3) | (gg >> 2); o[2] = (b << 3) | (b >> 2); o[3] = 0xFF;
          o += 4;
        }
        break;
      }
    }
  }
  mutex_lock(&g.frame_mutex);
  uint8_t* t = g.front; g.front = g.back; g.back = t;
  g.front_w = (int)w;
  g.front_h = (int)h;
  g.frame_counter++;
  mutex_unlock(&g.frame_mutex);
}

static void RETRO_CALLCONV video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (!data || data == RETRO_HW_FRAME_BUFFER_VALID || !w || !h) return;  // dupe frame
  convert_frame(data, w, h, pitch);
}

static size_t push_audio(const int16_t* frames, size_t count) {
#ifndef RR_NO_AUDIO
  if (!g.audio_ready || !g.audio_enabled || g.fast_forward) return count;
  size_t done = 0;
  while (done < count) {
    ma_uint32 n = (ma_uint32)(count - done);
    void* dst;
    if (ma_pcm_rb_acquire_write(&g.ring, &n, &dst) != MA_SUCCESS || n == 0) break;  // full: drop
    memcpy(dst, frames + done * 2, (size_t)n * 4);
    ma_pcm_rb_commit_write(&g.ring, n);
    done += n;
  }
#else
  (void)frames;
#endif
  return count;
}

static void RETRO_CALLCONV audio_sample_cb(int16_t l, int16_t r) {
  int16_t f[2] = {l, r};
  push_audio(f, 1);
}

static size_t RETRO_CALLCONV audio_batch_cb(const int16_t* data, size_t frames) {
  return push_audio(data, frames);
}

static void RETRO_CALLCONV input_poll_cb(void) {}

static int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)index;
  if (port >= 4) return 0;
  if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
  uint32_t b = g.buttons[port];
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)(b & 0xFFFF);
  if (id > 15) return 0;
  return (b >> id) & 1;
}

/* ------------------------------------------------------------------ */
/* Audio device                                                        */
/* ------------------------------------------------------------------ */

#ifndef RR_NO_AUDIO
static void ma_callback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
  (void)dev; (void)in;
  int16_t* o = (int16_t*)out;
  ma_uint32 left = frames;
  while (left > 0) {
    ma_uint32 n = left;
    void* src;
    if (ma_pcm_rb_acquire_read(&g.ring, &n, &src) != MA_SUCCESS || n == 0) break;
    memcpy(o, src, (size_t)n * 4);
    ma_pcm_rb_commit_read(&g.ring, n);
    o += n * 2;
    left -= n;
  }
  if (left) memset(o, 0, (size_t)left * 4);  // underrun -> silence
}

static void audio_stop(void) {
  if (!g.audio_ready) return;
  ma_device_uninit(&g.device);
  ma_pcm_rb_uninit(&g.ring);
  g.audio_ready = false;
}

static bool audio_start(void) {
  audio_stop();
  ma_uint32 rate = (ma_uint32)(g.av.timing.sample_rate + 0.5);
  if (rate < 8000) rate = 44100;
  if (ma_pcm_rb_init(ma_format_s16, 2, rate / 2, NULL, NULL, &g.ring) != MA_SUCCESS) return false;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_s16;
  cfg.playback.channels = 2;
  cfg.sampleRate = rate;  // miniaudio resamples to the hardware rate
  cfg.dataCallback = ma_callback;
  cfg.periodSizeInMilliseconds = 10;
  if (ma_device_init(NULL, &cfg, &g.device) != MA_SUCCESS) {
    ma_pcm_rb_uninit(&g.ring);
    rr_log("[romrunner] no audio device; using timer pacing\n");
    return false;
  }
  if (ma_device_start(&g.device) != MA_SUCCESS) {
    ma_device_uninit(&g.device);
    ma_pcm_rb_uninit(&g.ring);
    return false;
  }
  g.audio_ready = true;
  return true;
}

static ma_uint32 audio_buffered(void) {
  return g.audio_ready ? ma_pcm_rb_available_read(&g.ring) : 0;
}
#endif

/* ------------------------------------------------------------------ */
/* Emulation loop                                                      */
/* ------------------------------------------------------------------ */

static void run_one_frame(void) {
  mutex_lock(&g.core_mutex);
  if (g.has_frame_time_cb && g.frame_time_cb.callback)
    g.frame_time_cb.callback(g.frame_time_cb.reference);
  g.api.run();
  mutex_unlock(&g.core_mutex);
}

static void loop_body(void) {
  double fps = g.av.timing.fps > 1 ? g.av.timing.fps : 60.0;
  double frame_time = 1.0 / fps;
  double next = now_seconds();
  double last_sram = next;

  while (g.running) {
    if (g.av_changed) {
      g.av_changed = 0;
      fps = g.av.timing.fps > 1 ? g.av.timing.fps : 60.0;
      frame_time = 1.0 / fps;
#ifndef RR_NO_AUDIO
      if (g.audio_ready) audio_start();
#endif
    }
    if (g.paused) {
      sleep_ms(10);
      next = now_seconds();
      continue;
    }
    if (g.fast_forward) {
      // Up to ~4x speed; audio is muted while fast-forwarding.
      for (int i = 0; i < 4 && g.running; i++) run_one_frame();
      next += frame_time;
    } else {
#ifndef RR_NO_AUDIO
      if (g.audio_ready && g.audio_enabled) {
        // Audio-paced: keep ~3 frames of sound queued, run when below.
        ma_uint32 target = (ma_uint32)(g.av.timing.sample_rate * frame_time * 3);
        while (g.running && !g.paused && audio_buffered() > target) sleep_ms(1);
        run_one_frame();
        next = now_seconds();
        goto after_frame;
      }
#endif
      run_one_frame();
      next += frame_time;
    }
    {
      double wait = next - now_seconds();
      if (wait > 0) sleep_ms((int)(wait * 1000));
      else if (wait < -0.25) next = now_seconds();  // fell behind; resync
    }
#ifndef RR_NO_AUDIO
  after_frame:
#endif
    if (now_seconds() - last_sram > 10.0) {
      rr_flush_sram();
      last_sram = now_seconds();
    }
  }
}

#if defined(_WIN32)
static DWORD WINAPI thread_main(LPVOID arg) {
  (void)arg;
  timeBeginPeriod(1);
  loop_body();
  timeEndPeriod(1);
  return 0;
}
#else
static void* thread_main(void* arg) {
  (void)arg;
  loop_body();
  return NULL;
}
#endif

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void rr_set_directories(const char* system_dir, const char* save_dir) {
  ensure_mutexes();
  snprintf(g.system_dir, sizeof g.system_dir, "%s", system_dir ? system_dir : "");
  snprintf(g.save_dir, sizeof g.save_dir, "%s", save_dir ? save_dir : "");
}

void rr_set_option(const char* key, const char* value) {
  ensure_mutexes();
  if (!key || !value) return;
  mutex_lock(&g.core_mutex);
  put_option(key, value, true);
  mutex_unlock(&g.core_mutex);
}

#define LOAD_SYM(field, name)                                         \
  do {                                                                \
    *(void**)(&g.api.field) = lib_sym(g.lib, name);                   \
    if (!g.api.field) { set_error("core is missing %s", name); goto fail; } \
  } while (0)

int rr_load_core(const char* core_path) {
  ensure_mutexes();
  rr_unload();
  g.lib = lib_open(core_path);
  if (!g.lib) {
#if defined(_WIN32)
    set_error("could not load core %s (error %lu)", core_path, GetLastError());
#else
    set_error("could not load core %s: %s", core_path, dlerror());
#endif
    return -1;
  }
  memset(&g.api, 0, sizeof g.api);
  LOAD_SYM(init, "retro_init");
  LOAD_SYM(deinit, "retro_deinit");
  LOAD_SYM(api_version, "retro_api_version");
  LOAD_SYM(get_system_info, "retro_get_system_info");
  LOAD_SYM(get_system_av_info, "retro_get_system_av_info");
  LOAD_SYM(set_environment, "retro_set_environment");
  LOAD_SYM(set_video_refresh, "retro_set_video_refresh");
  LOAD_SYM(set_audio_sample, "retro_set_audio_sample");
  LOAD_SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
  LOAD_SYM(set_input_poll, "retro_set_input_poll");
  LOAD_SYM(set_input_state, "retro_set_input_state");
  LOAD_SYM(set_controller_port_device, "retro_set_controller_port_device");
  LOAD_SYM(reset, "retro_reset");
  LOAD_SYM(run, "retro_run");
  LOAD_SYM(serialize_size, "retro_serialize_size");
  LOAD_SYM(serialize, "retro_serialize");
  LOAD_SYM(unserialize, "retro_unserialize");
  LOAD_SYM(load_game, "retro_load_game");
  LOAD_SYM(unload_game, "retro_unload_game");
  LOAD_SYM(get_memory_data, "retro_get_memory_data");
  LOAD_SYM(get_memory_size, "retro_get_memory_size");

  if (g.api.api_version() != RETRO_API_VERSION) {
    set_error("core uses libretro API %u, expected %u", g.api.api_version(), RETRO_API_VERSION);
    goto fail;
  }

  clear_core_defaults();
  g.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
  g.has_frame_time_cb = false;
  g.api.set_environment(env_cb);
  g.api.set_video_refresh(video_cb);
  g.api.set_audio_sample(audio_sample_cb);
  g.api.set_audio_sample_batch(audio_batch_cb);
  g.api.set_input_poll(input_poll_cb);
  g.api.set_input_state(input_state_cb);
  g.api.init();
  g.core_inited = true;

  struct retro_system_info info = {0};
  g.api.get_system_info(&info);
  snprintf(g.core_name, sizeof g.core_name, "%s %s",
           info.library_name ? info.library_name : "core",
           info.library_version ? info.library_version : "");
  return 0;

fail:
  lib_close(g.lib);
  g.lib = NULL;
  return -1;
}

static void build_sram_path(const char* rom_path) {
  const char* base = rom_path;
  for (const char* p = rom_path; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  char name[512];
  snprintf(name, sizeof name, "%s", base);
  char* dot = strrchr(name, '.');
  if (dot) *dot = 0;
  if (g.save_dir[0])
    snprintf(g.sram_path, sizeof g.sram_path, "%s/%s.srm", g.save_dir, name);
  else
    g.sram_path[0] = 0;
}

int rr_load_game(const char* rom_path) {
  if (!g.core_inited) { set_error("no core loaded"); return -1; }
  if (g.game_loaded) { set_error("a game is already loaded"); return -1; }

  struct retro_system_info info = {0};
  g.api.get_system_info(&info);

  struct retro_game_info game = {0};
  game.path = rom_path;
  if (!info.need_fullpath) {
    size_t size = 0;
    g.rom_data = read_file(rom_path, &size);
    if (!g.rom_data) { set_error("could not read %s", rom_path); return -1; }
    game.data = g.rom_data;
    game.size = size;
  }
  snprintf(g.rom_path, sizeof g.rom_path, "%s", rom_path);

  if (!g.api.load_game(&game)) {
    set_error("core rejected the game (wrong system, bad dump or missing BIOS)");
    free(g.rom_data);
    g.rom_data = NULL;
    return -1;
  }
  g.game_loaded = true;
  g.api.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
  g.api.set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
  g.api.get_system_av_info(&g.av);

  build_sram_path(rom_path);
  if (g.sram_path[0]) {
    size_t want = g.api.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void* mem = g.api.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t got = 0;
    unsigned char* data = want && mem ? read_file(g.sram_path, &got) : NULL;
    if (data) {
      memcpy(mem, data, got < want ? got : want);
      free(data);
    }
  }
  memset((void*)g.buttons, 0, sizeof g.buttons);
  return 0;
}

int rr_start(void) {
  if (!g.game_loaded) { set_error("no game loaded"); return -1; }
  if (g.thread_started) return 0;
#ifndef RR_NO_AUDIO
  if (g.audio_enabled) audio_start();
#endif
  g.running = 1;
  g.paused = 0;
#if defined(_WIN32)
  g.thread = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
  if (!g.thread) { g.running = 0; set_error("could not start emulation thread"); return -1; }
#else
  if (pthread_create(&g.thread, NULL, thread_main, NULL) != 0) {
    g.running = 0;
    set_error("could not start emulation thread");
    return -1;
  }
#endif
  g.thread_started = true;
  return 0;
}

static void stop_thread(void) {
  if (!g.thread_started) return;
  g.running = 0;
#if defined(_WIN32)
  WaitForSingleObject(g.thread, INFINITE);
  CloseHandle(g.thread);
#else
  pthread_join(g.thread, NULL);
#endif
  g.thread_started = false;
#ifndef RR_NO_AUDIO
  audio_stop();
#endif
}

void rr_set_paused(int paused) { g.paused = paused; }
void rr_set_fast_forward(int enabled) { g.fast_forward = enabled; }
void rr_set_audio_enabled(int enabled) { ensure_mutexes(); g.audio_enabled = enabled; }

void rr_reset(void) {
  if (!g.game_loaded) return;
  mutex_lock(&g.core_mutex);
  g.api.reset();
  mutex_unlock(&g.core_mutex);
}

void rr_set_button(int port, int id, int pressed) {
  if (port < 0 || port > 3 || id < 0 || id > 15) return;
  if (pressed) g.buttons[port] |= (1u << id);
  else g.buttons[port] &= ~(1u << id);
}

void rr_set_buttons(int port, uint32_t mask) {
  if (port < 0 || port > 3) return;
  g.buttons[port] = mask & 0xFFFF;
}

int64_t rr_copy_frame(uint8_t* dst, int32_t capacity, int32_t* width, int32_t* height) {
  if (!g.mutexes_ready) return -1;
  mutex_lock(&g.frame_mutex);
  int64_t counter = g.frame_counter;
  if (counter == 0 || !g.front) { mutex_unlock(&g.frame_mutex); return -1; }
  size_t need = (size_t)g.front_w * g.front_h * 4;
  if (width) *width = g.front_w;
  if (height) *height = g.front_h;
  if (dst && (size_t)capacity >= need) memcpy(dst, g.front, need);
  else counter = -2;  // caller buffer too small; width/height tell it the size
  mutex_unlock(&g.frame_mutex);
  return counter;
}

double rr_get_fps(void) { return g.av.timing.fps; }
double rr_get_sample_rate(void) { return g.av.timing.sample_rate; }
double rr_get_aspect_ratio(void) {
  float a = g.av.geometry.aspect_ratio;
  if (a > 0) return a;
  if (g.av.geometry.base_height) return (double)g.av.geometry.base_width / g.av.geometry.base_height;
  return 0;
}
int32_t rr_get_max_width(void) { return (int32_t)g.av.geometry.max_width; }
int32_t rr_get_max_height(void) { return (int32_t)g.av.geometry.max_height; }

int rr_save_state(const char* path) {
  if (!g.game_loaded) { set_error("no game loaded"); return -1; }
  mutex_lock(&g.core_mutex);
  size_t size = g.api.serialize_size();
  void* buf = size ? malloc(size) : NULL;
  bool ok = buf && g.api.serialize(buf, size);
  mutex_unlock(&g.core_mutex);
  int rc = -1;
  if (ok) rc = write_file(path, buf, size);
  free(buf);
  if (rc != 0) set_error(ok ? "could not write %s" : "core cannot save state", path);
  return rc;
}

int rr_load_state(const char* path) {
  if (!g.game_loaded) { set_error("no game loaded"); return -1; }
  size_t size = 0;
  unsigned char* buf = read_file(path, &size);
  if (!buf) { set_error("no state at %s", path); return -1; }
  mutex_lock(&g.core_mutex);
  bool ok = g.api.unserialize(buf, size);
  mutex_unlock(&g.core_mutex);
  free(buf);
  if (!ok) { set_error("core rejected the save state"); return -1; }
  return 0;
}

int rr_flush_sram(void) {
  if (!g.game_loaded || !g.sram_path[0]) return 0;
  size_t size = g.api.get_memory_size(RETRO_MEMORY_SAVE_RAM);
  void* mem = g.api.get_memory_data(RETRO_MEMORY_SAVE_RAM);
  if (!size || !mem) return 0;
  return write_file(g.sram_path, mem, size);
}

void rr_unload(void) {
  if (!g.mutexes_ready) return;
  stop_thread();
  if (g.game_loaded) {
    rr_flush_sram();
    g.api.unload_game();
    g.game_loaded = false;
  }
  free(g.rom_data);
  g.rom_data = NULL;
  if (g.core_inited) {
    g.api.deinit();
    g.core_inited = false;
  }
  if (g.lib) {
    lib_close(g.lib);
    g.lib = NULL;
  }
  mutex_lock(&g.frame_mutex);
  g.frame_counter = 0;
  mutex_unlock(&g.frame_mutex);
  memset(&g.av, 0, sizeof g.av);
  g.core_name[0] = 0;
}

const char* rr_last_error(void) { return g.error; }
const char* rr_core_name(void) { return g.core_name; }

int rr_run_frames(int count) {
  if (!g.game_loaded) { set_error("no game loaded"); return -1; }
  for (int i = 0; i < count; i++) run_one_frame();
  return 0;
}
