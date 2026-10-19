// MigPlay core bridge: hosts one libretro core and exposes a small C API
// that Dart calls through FFI. The emulation loop, audio and pixel
// conversion all run natively; Dart only polls frames and sends input.
#ifndef MIGPLAY_CORE_H
#define MIGPLAY_CORE_H

#include <stdint.h>

#if defined(_WIN32)
#define MP_EXPORT __declspec(dllexport)
#else
#define MP_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Joypad button ids (same numbering as RETRO_DEVICE_ID_JOYPAD_*).
enum {
  MP_BTN_B = 0, MP_BTN_Y = 1, MP_BTN_SELECT = 2, MP_BTN_START = 3,
  MP_BTN_UP = 4, MP_BTN_DOWN = 5, MP_BTN_LEFT = 6, MP_BTN_RIGHT = 7,
  MP_BTN_A = 8, MP_BTN_X = 9, MP_BTN_L = 10, MP_BTN_R = 11,
  MP_BTN_L2 = 12, MP_BTN_R2 = 13, MP_BTN_L3 = 14, MP_BTN_R3 = 15
};

// Directories for BIOS files (system) and battery saves. Call before load.
MP_EXPORT void mp_set_directories(const char* system_dir, const char* save_dir);

// Set a core option ("fceumm_region" = "NTSC"). Takes effect on next read.
MP_EXPORT void mp_set_option(const char* key, const char* value);

// Load a libretro core (path or, on Android, bare soname). 0 on success.
MP_EXPORT int mp_load_core(const char* core_path);

// Load a game. 0 on success. Reads battery save if present.
MP_EXPORT int mp_load_game(const char* rom_path);

// Start the emulation thread (and audio). 0 on success.
MP_EXPORT int mp_start(void);

MP_EXPORT void mp_set_paused(int paused);
MP_EXPORT void mp_set_fast_forward(int enabled);
MP_EXPORT void mp_set_audio_enabled(int enabled);
MP_EXPORT void mp_reset(void);

// port 0..3, id = MP_BTN_*, pressed 0/1.
MP_EXPORT void mp_set_button(int port, int id, int pressed);
// Set all 16 buttons of a port at once as a bitmask (bit n = button n).
MP_EXPORT void mp_set_buttons(int port, uint32_t mask);

// Copy the latest frame as RGBA8888 into dst (capacity in bytes).
// Returns the frame counter (increments each new frame), or -1 if none yet.
MP_EXPORT int64_t mp_copy_frame(uint8_t* dst, int32_t capacity, int32_t* width, int32_t* height);

MP_EXPORT double mp_get_fps(void);
MP_EXPORT double mp_get_sample_rate(void);
MP_EXPORT double mp_get_aspect_ratio(void);   // 0 if core gave none
MP_EXPORT int32_t mp_get_max_width(void);
MP_EXPORT int32_t mp_get_max_height(void);

MP_EXPORT int mp_save_state(const char* path);
MP_EXPORT int mp_load_state(const char* path);

// Write battery save now (also done on unload and every ~10 s).
MP_EXPORT int mp_flush_sram(void);

// Stop thread, save SRAM, unload game and core.
MP_EXPORT void mp_unload(void);

MP_EXPORT const char* mp_last_error(void);
MP_EXPORT const char* mp_core_name(void);

// Headless helper for tests: run N frames synchronously (no thread/audio).
MP_EXPORT int mp_run_frames(int count);

#ifdef __cplusplus
}
#endif
#endif
