// RomRunner core bridge: hosts one libretro core and exposes a small C API
// that Dart calls through FFI. The emulation loop, audio and pixel
// conversion all run natively; Dart only polls frames and sends input.
#ifndef ROMRUNNER_CORE_H
#define ROMRUNNER_CORE_H

#include <stdint.h>

#if defined(_WIN32)
#define RR_EXPORT __declspec(dllexport)
#else
#define RR_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Joypad button ids (same numbering as RETRO_DEVICE_ID_JOYPAD_*).
enum {
  RR_BTN_B = 0, RR_BTN_Y = 1, RR_BTN_SELECT = 2, RR_BTN_START = 3,
  RR_BTN_UP = 4, RR_BTN_DOWN = 5, RR_BTN_LEFT = 6, RR_BTN_RIGHT = 7,
  RR_BTN_A = 8, RR_BTN_X = 9, RR_BTN_L = 10, RR_BTN_R = 11,
  RR_BTN_L2 = 12, RR_BTN_R2 = 13, RR_BTN_L3 = 14, RR_BTN_R3 = 15
};

// Directories for BIOS files (system) and battery saves. Call before load.
RR_EXPORT void rr_set_directories(const char* system_dir, const char* save_dir);

// Set a core option ("fceumm_region" = "NTSC"). Takes effect on next read.
RR_EXPORT void rr_set_option(const char* key, const char* value);

// Load a libretro core (path or, on Android, bare soname). 0 on success.
RR_EXPORT int rr_load_core(const char* core_path);

// Load a game. 0 on success. Reads battery save if present.
RR_EXPORT int rr_load_game(const char* rom_path);

// Start the emulation thread (and audio). 0 on success.
RR_EXPORT int rr_start(void);

RR_EXPORT void rr_set_paused(int paused);
RR_EXPORT void rr_set_fast_forward(int enabled);
RR_EXPORT void rr_set_audio_enabled(int enabled);
RR_EXPORT void rr_reset(void);

// port 0..3, id = RR_BTN_*, pressed 0/1.
RR_EXPORT void rr_set_button(int port, int id, int pressed);
// Set all 16 buttons of a port at once as a bitmask (bit n = button n).
RR_EXPORT void rr_set_buttons(int port, uint32_t mask);

// Touchscreen / pointer for port 0, in libretro coordinates: -32767..32767
// across the whole frame (top-left = -32767,-32767). Used by DS games.
RR_EXPORT void rr_set_pointer(int32_t x, int32_t y, int pressed);

// Copy the latest frame as RGBA8888 into dst (capacity in bytes).
// Returns the frame counter (increments each new frame), or -1 if none yet.
RR_EXPORT int64_t rr_copy_frame(uint8_t* dst, int32_t capacity, int32_t* width, int32_t* height);

RR_EXPORT double rr_get_fps(void);
RR_EXPORT double rr_get_sample_rate(void);
RR_EXPORT double rr_get_aspect_ratio(void);   // 0 if core gave none
RR_EXPORT int32_t rr_get_max_width(void);
RR_EXPORT int32_t rr_get_max_height(void);

RR_EXPORT int rr_save_state(const char* path);
RR_EXPORT int rr_load_state(const char* path);

// Write battery save now (also done on unload and every ~10 s).
RR_EXPORT int rr_flush_sram(void);

// Stop thread, save SRAM, unload game and core.
RR_EXPORT void rr_unload(void);

RR_EXPORT const char* rr_last_error(void);
RR_EXPORT const char* rr_core_name(void);

// Headless helper for tests: run N frames synchronously (no thread/audio).
RR_EXPORT int rr_run_frames(int count);

#ifdef __cplusplus
}
#endif
#endif
