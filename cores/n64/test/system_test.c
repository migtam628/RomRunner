/*
 * System test through the libretro API: builds a cartridge in memory whose
 * program sets up the video interface, reads controller 1 over the serial
 * interface / PIF, and fills the screen red (or green while A is held).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "../src/n64.h"
#include "asm.h"

void rrn64_boot_checksum(const uint8_t *rom, uint32_t size, uint32_t cic, uint32_t *c1, uint32_t *c2);
N64 *rrn64_machine(void);

static int failures, checks;
#define CHECK(name, got, want)                                                                         \
  do {                                                                                                 \
    uint64_t g_ = (uint64_t)(got), w_ = (uint64_t)(want);                                              \
    checks++;                                                                                          \
    if (g_ != w_) {                                                                                    \
      failures++;                                                                                      \
      printf("FAIL %-40s got %llx want %llx\n", name, (unsigned long long)g_, (unsigned long long)w_); \
    }                                                                                                  \
  } while (0)

/* ---- frontend stubs ---- */
static uint32_t last_pixel, last_w, last_h;
static unsigned frames_out;
static size_t audio_frames_total;
static bool press_b;

static bool env(unsigned cmd, void *data) {
  (void)data;
  return cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT || cmd == RETRO_ENVIRONMENT_SET_VARIABLES;
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch) {
  (void)pitch;
  frames_out++;
  last_w = w;
  last_h = h;
  if (data && w && h) last_pixel = ((const uint32_t *)data)[(h / 2) * w + w / 2];
}
static void audio1(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio(const int16_t *d, size_t f) { (void)d; audio_frames_total += f; return f; }
static void poll(void) {}
static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id) {
  (void)idx;
  return port == 0 && dev == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_B && press_b;
}

/* ---- the cartridge ---- */
static uint8_t rom[0x102000];
static uint32_t pc_words;

static void emit(uint32_t w) { wr32be(rom + 0x1000 + pc_words * 4, w); pc_words++; }
#define LI(r, v) do { emit(LI_HI(r, v)); emit(LI_LO(r, v)); } while (0)

enum { T0 = 8, T1, T2, T3, T4, T5, T6, T7, T8 = 24, T9 };

static void build_rom(uint32_t cic) {
  memset(rom, 0, sizeof rom);
  wr32be(rom + 0x00, 0x80371240u);
  wr32be(rom + 0x04, 0x0000000Fu);
  wr32be(rom + 0x08, 0x80000400u);   /* entry point */
  wr32be(rom + 0x0C, 0x00001444u);
  memcpy(rom + 0x20, "RROMRUNNER N64 TEST ", 20);
  rom[0x3B] = 'N'; rom[0x3C] = 'R'; rom[0x3D] = 'R'; rom[0x3E] = 'E';
  pc_words = 0;

  /* VI: 320x240, 16-bit, NTSC timing */
  LI(T0, 0xA4400000u);
  LI(T1, 0x00100000u); emit(SW(T1, 0x04, T0));     /* origin */
  emit(ADDIU(T1, 0, 320)); emit(SW(T1, 0x08, T0)); /* width */
  emit(ADDIU(T1, 0, 0x200)); emit(SW(T1, 0x0C, T0)); /* v_intr */
  emit(ADDIU(T1, 0, 0x20D)); emit(SW(T1, 0x18, T0)); /* v_sync */
  LI(T1, 0x006C02ECu); emit(SW(T1, 0x24, T0));     /* h_video */
  LI(T1, 0x002501FFu); emit(SW(T1, 0x28, T0));     /* v_video */
  emit(ADDIU(T1, 0, 0x200)); emit(SW(T1, 0x30, T0)); /* x_scale 0.5 */
  emit(ADDIU(T1, 0, 0x400)); emit(SW(T1, 0x34, T0)); /* y_scale 1.0 */
  emit(ADDIU(T1, 0, 2)); emit(SW(T1, 0x00, T0));   /* 16-bit colour */

  uint32_t loop = 0x80000400u + pc_words * 4;
  /* PIF command block at 0xA0200000: [FF] [01 tx] [04 rx] [01 cmd] [4 bytes] [FE] ... [63]=1 */
  LI(T2, 0xA0200000u);
  LI(T3, 0xFF010401u); emit(SW(T3, 0, T2));
  emit(ADDIU(T3, 0, -1)); emit(SW(T3, 4, T2));
  emit(LUI(T3, 0xFE00)); emit(SW(T3, 8, T2));
  emit(ADDIU(T3, 0, 1)); emit(SW(T3, 60, T2));
  LI(T4, 0xA4800000u);
  emit(LUI(T5, 0x0020)); emit(SW(T5, 0x00, T4));
  LI(T5, 0x1FC007C0u); emit(SW(T5, 0x10, T4));     /* RDRAM -> PIF, runs the commands */
  emit(LUI(T5, 0x0020)); emit(SW(T5, 0x00, T4));
  LI(T5, 0x1FC007C0u); emit(SW(T5, 0x04, T4));     /* PIF -> RDRAM */
  emit(LW(T6, 4, T2));                             /* buttons in the top half; A is bit 31 */
  emit(ORI(T7, 0, 0xF801));                        /* red */
  emit(BGEZ(T6, 2));                               /* not pressed: skip the green */
  emit(NOP);
  emit(ORI(T7, 0, 0x07C1));                        /* green */
  /* fill the 320x240 framebuffer */
  LI(T8, 0xA0100000u);
  LI(T9, 0x00025800u);
  emit(ADDU(T9, T8, T9));
  uint32_t fill = 0x80000400u + pc_words * 4;
  emit(SH(T7, 0, T8));
  emit(ADDIU(T8, T8, 2));
  emit(BNE(T8, T9, (int32_t)(fill - (0x80000400u + pc_words * 4) - 4) / 4));
  emit(NOP);
  emit(J(loop));
  emit(NOP);

  uint32_t c1, c2;
  rrn64_boot_checksum(rom, sizeof rom, cic, &c1, &c2);
  wr32be(rom + 0x10, c1);
  wr32be(rom + 0x14, c2);
}

int main(int argc, char **argv) {
  /* --write-rom <path>: save the test cartridge (for the bridge test in CI). */
  if (argc == 3 && !strcmp(argv[1], "--write-rom")) {
    build_rom(6102);
    FILE *f = fopen(argv[2], "wb");
    if (!f) return 1;
    fwrite(rom, 1, sizeof rom, f);
    fclose(f);
    return 0;
  }
  retro_set_environment(env);
  retro_set_video_refresh(video);
  retro_set_audio_sample(audio1);
  retro_set_audio_sample_batch(audio);
  retro_set_input_poll(poll);
  retro_set_input_state(input);
  retro_init();

  struct retro_system_info si;
  retro_get_system_info(&si);
  CHECK("extensions listed", si.valid_extensions != NULL, 1);

  /* CIC detection from the header checksum. */
  static const uint32_t chips[] = {6102, 6103, 6105, 6106};
  for (unsigned i = 0; i < 4; i++) {
    build_rom(chips[i]);
    CHECK("cic detected", cic_detect(rom, sizeof rom), chips[i]);
  }

  build_rom(6102);
  struct retro_game_info gi = {"test.z64", rom, sizeof rom, NULL};
  CHECK("load game", retro_load_game(&gi), 1);
  N64 *m = rrn64_machine();
  CHECK("cic 6102", m->cic, 6102);
  CHECK("NTSC", m->pal, 0);
  CHECK("boot pc", (uint32_t)m->cpu.pc, 0x80000400u);
  CHECK("osMemSize", rd32be(m->rdram + 0x318), RDRAM_SIZE);
  CHECK("osTvType NTSC", rd32be(m->rdram + 0x300), 1);
  CHECK("osRomBase", rd32be(m->rdram + 0x308), 0xB0000000u);
  {
    /* CIC-6105 challenge: PIF answers and clears command bit 0x02. */
    for (int i = 0; i < 15; i++) m->pif_ram[0x30 + i] = (uint8_t)(i * 17);
    m->pif_ram[63] = 0x02;
    pif_process(m);
    CHECK("6105 challenge acknowledged", m->pif_ram[63] & 0x02, 0);
    CHECK("6105 answer first byte", m->pif_ram[0x30], 0xBF); /* worked by hand: B, then F */
  }

  for (int i = 0; i < 3; i++) retro_run();
  CHECK("frames delivered", frames_out, 3);
  CHECK("width", last_w, 320);
  CHECK("height", last_h, 237);
  CHECK("red screen", last_pixel, 0x00FF0000u);
  CHECK("audio ~735 frames/frame", audio_frames_total >= 3 * 730 && audio_frames_total <= 3 * 740, 1);

  press_b = true;   /* RetroPad B = N64 A */
  for (int i = 0; i < 2; i++) retro_run();
  CHECK("A turns it green", last_pixel, 0x0000FF00u);

  /* Save state round trip. */
  size_t sz = retro_serialize_size();
  void *st = malloc(sz);
  CHECK("serialize", retro_serialize(st, sz), 1);
  press_b = false;
  for (int i = 0; i < 2; i++) retro_run();
  CHECK("released: red", last_pixel, 0x00FF0000u);
  CHECK("unserialize", retro_unserialize(st, sz), 1);
  press_b = true;
  retro_run();
  CHECK("state restored, still green", last_pixel, 0x0000FF00u);
  free(st);

  /* Byte-swapped (.v64) dumps load too. */
  retro_unload_game();
  static uint8_t v64[sizeof rom];
  for (size_t i = 0; i < sizeof rom; i += 2) { v64[i] = rom[i + 1]; v64[i + 1] = rom[i]; }
  struct retro_game_info gv = {"test.v64", v64, sizeof v64, NULL};
  CHECK("load v64", retro_load_game(&gv), 1);
  press_b = false;
  frames_out = 0;
  for (int i = 0; i < 2; i++) retro_run();
  CHECK("v64 runs", last_pixel, 0x00FF0000u);
  retro_unload_game();

  {
    /* FlashRAM protocol, driven through the PI like a game would. */
    N64 *f = (N64 *)calloc(1, sizeof(N64));
    f->save_type = SAVE_FLASH;
    flash_erase_all(f);
    flash_reset(f);
    uint32_t v = 0;
    bus_write32(f, 0x08010000u, 0xE1000000u);
    bus_read32(f, 0x08000000u, &v);
    CHECK("flash status word", v, 0x11118001u);
    rcp_reg_write(f, 0x04600000u, 0x1000);            /* PI DRAM address */
    rcp_reg_write(f, 0x04600004u, 0x08000000u);       /* PI cart address */
    rcp_reg_write(f, 0x0460000Cu, 7);                 /* 8 bytes cart -> RDRAM */
    CHECK("flash ID by DMA", rd32be(f->rdram + 0x1004), 0x00C2001Eu);
    for (int i = 0; i < 128; i++) f->rdram[0x2000 + i] = (uint8_t)(i ^ 0x5A);
    bus_write32(f, 0x08010000u, 0xB4000000u);         /* write mode */
    rcp_reg_write(f, 0x04600000u, 0x2000);
    rcp_reg_write(f, 0x04600004u, 0x08000000u);
    rcp_reg_write(f, 0x04600008u, 127);               /* 128 bytes RDRAM -> page buffer */
    bus_write32(f, 0x08010000u, 0xA5000000u | 3);     /* program page 3 */
    CHECK("flash page programmed", f->sram[3 * 128 + 1], 1 ^ 0x5A);
    CHECK("flash next page untouched", f->sram[4 * 128], 0xFF);
    bus_write32(f, 0x08010000u, 0xF0000000u);         /* read mode */
    rcp_reg_write(f, 0x04600000u, 0x3000);
    rcp_reg_write(f, 0x04600004u, 0x08000000u + 3 * 128 / 2);  /* PI address counts 16-bit words */
    rcp_reg_write(f, 0x0460000Cu, 127);
    CHECK("flash read back", f->rdram[0x3000 + 5], 5 ^ 0x5A);
    bus_write32(f, 0x08010000u, 0x4B000000u | 3);     /* sector holding page 3 */
    bus_write32(f, 0x08010000u, 0x78000000u);
    bus_write32(f, 0x08010000u, 0xD2000000u);
    CHECK("flash sector erased", f->sram[3 * 128 + 1], 0xFF);
    CHECK("flash save marked dirty", f->save_dirty, 1);
    free(f);
  }
  printf("system: %d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
