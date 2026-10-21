/*
 * RomRunner N64 — an in-house Nintendo 64 emulator core.
 *
 * Written from public hardware documentation (n64brew wiki, the VR4300
 * user's manual, MIPS III ISA). No code from other emulators.
 *
 * Layout: one plain-old-data struct holds the whole machine (so save states
 * are a straight copy), except the cartridge ROM which is loaded separately.
 */
#ifndef RRN64_H
#define RRN64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDRAM_SIZE   0x800000u   /* 8 MB: base 4 MB + Expansion Pak */
#define PIF_RAM_SIZE 64u

/* CPU clock and video timing (NTSC). */
#define CPU_HZ            93750000u
#define VI_NTSC_CLOCK     48681812u
#define VI_PAL_CLOCK      49656530u

/* MI interrupt bits */
enum { MI_INTR_SP = 1 << 0, MI_INTR_SI = 1 << 1, MI_INTR_AI = 1 << 2, MI_INTR_VI = 1 << 3, MI_INTR_PI = 1 << 4, MI_INTR_DP = 1 << 5 };

/* COP0 register numbers */
enum {
  CP0_INDEX = 0, CP0_RANDOM, CP0_ENTRYLO0, CP0_ENTRYLO1, CP0_CONTEXT, CP0_PAGEMASK, CP0_WIRED,
  CP0_BADVADDR = 8, CP0_COUNT, CP0_ENTRYHI, CP0_COMPARE, CP0_STATUS, CP0_CAUSE, CP0_EPC, CP0_PRID,
  CP0_CONFIG, CP0_LLADDR, CP0_WATCHLO, CP0_WATCHHI, CP0_XCONTEXT, CP0_PERR = 26, CP0_CACHEERR,
  CP0_TAGLO, CP0_TAGHI, CP0_ERROREPC
};

/* Status register bits */
#define ST_IE   (1u << 0)
#define ST_EXL  (1u << 1)
#define ST_ERL  (1u << 2)
#define ST_KX   (1u << 7)
#define ST_SX   (1u << 6)
#define ST_UX   (1u << 5)
#define ST_BEV  (1u << 22)
#define ST_FR   (1u << 26)
#define ST_CU0  (1u << 28)
#define ST_CU1  (1u << 29)
#define ST_CU2  (1u << 30)

/* Exception codes */
enum {
  EXC_INT = 0, EXC_MOD = 1, EXC_TLBL = 2, EXC_TLBS = 3, EXC_ADEL = 4, EXC_ADES = 5,
  EXC_SYS = 8, EXC_BP = 9, EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12, EXC_TR = 13, EXC_FPE = 15, EXC_WATCH = 23
};

typedef struct {
  uint64_t mask;   /* PageMask (bits 24..13) */
  uint64_t hi;     /* EntryHi: R, VPN2, ASID (G stored separately) */
  uint64_t lo0, lo1;
  bool global;
} TlbEntry;

typedef struct {
  uint64_t gpr[32];
  uint64_t hi, lo;
  uint64_t pc;        /* instruction about to execute */
  uint64_t next_pc;   /* the one after it (branch target when in a delay slot) */
  bool in_delay;      /* the instruction at pc sits in a branch delay slot */
  bool branch_pending;/* the instruction just executed was a branch */
  bool llbit;
  uint64_t cur_pc;    /* address of the instruction executing now */
  bool cur_delay;     /* it is in a delay slot (for EPC / Cause.BD) */

  uint64_t cp0[32];
  uint32_t count_frac;  /* Count ticks at half the CPU clock */
  TlbEntry tlb[32];

  uint64_t fgr[32];   /* FPU registers (raw bits) */
  uint32_t fcr31;

  uint64_t cycles;    /* total CPU cycles executed */
} Cpu;

typedef struct {
  uint32_t mode, intr, mask;
} Mi;

typedef struct {
  uint32_t ctrl, origin, width, v_intr, v_current, burst, v_sync, h_sync, leap, h_video, v_video, v_burst, x_scale, y_scale;
  uint32_t line_cycles;   /* CPU cycles until the next half-line */
  uint32_t field;
} Vi;

typedef struct {
  uint32_t dram_addr[2], len[2];  /* two-entry FIFO, [0] is playing */
  uint32_t count;                 /* buffers queued */
  uint32_t control, dacrate, bitrate;
  double pos;                     /* position within the current buffer, in samples */
  int16_t last_l, last_r;
} Ai;

typedef struct {
  uint32_t dram_addr, cart_addr, rd_len, wr_len, status;
  uint32_t dom1_lat, dom1_pwd, dom1_pgs, dom1_rls, dom2_lat, dom2_pwd, dom2_pgs, dom2_rls;
} Pi;

typedef struct {
  uint32_t dram_addr, status;
} Si;

typedef struct {
  uint32_t mem_addr, dram_addr;   /* as written; a DMA starts from these */
  uint32_t cur_mem, cur_dram;     /* where the last DMA got to (what reads return) */
  uint32_t rd_len, wr_len, status, semaphore, pc;
} SpRegs;

typedef struct {
  uint32_t start, end, current, status, clock, bufbusy, pipebusy, tmem;
} DpRegs;

/* RSP: a MIPS-like scalar unit plus the vector unit (COP2). */
typedef struct {
  uint32_t r[32];
  uint32_t pc, next_pc;     /* 12-bit IMEM addresses */
  bool in_delay;
  uint16_t vr[32][8];       /* vector registers; element 0 is the most significant */
  int64_t acc[8];           /* 48-bit accumulators, sign-extended */
  uint16_t vco, vcc;        /* low byte: carry / compare, high byte: not-equal / clip */
  uint8_t vce;
  int32_t div_in, div_out;  /* VRCP/VRSQ double-precision state */
  bool div_dp;
} Rsp;

/* Save memory kinds (stage 4 adds the controller pak). */
typedef enum { SAVE_NONE, SAVE_EEPROM_4K, SAVE_EEPROM_16K, SAVE_SRAM, SAVE_FLASH } SaveType;

typedef struct {
  uint16_t buttons;  /* N64 controller bit layout, see pif.c */
  int8_t x, y;
} PadState;

typedef struct N64 {
  Cpu cpu;
  Mi mi;
  Vi vi;
  Ai ai;
  Pi pi;
  Si si;
  SpRegs sp;
  DpRegs dp;
  Rsp rsp;
  uint32_t ri[8];

  uint8_t rdram[RDRAM_SIZE];   /* big-endian bytes, as on the console */
  uint8_t dmem[0x1000];
  uint8_t imem[0x1000];
  uint8_t pif_ram[PIF_RAM_SIZE];

  SaveType save_type;
  uint8_t eeprom[2048];
  uint8_t sram[0x20000];       /* SRAM (32 KB) or FlashRAM (128 KB) */
  bool save_dirty;

  /* Audio resampled to 44.1 kHz stereo, drained by the frontend each frame. */
  int16_t audio_buf[8192 * 2];
  uint32_t audio_frames;
  double audio_frac;

  PadState pad[4];
  bool pad_connected[4];
  bool pal;
  uint32_t cic;               /* 6101, 6102, 6103, 6105, 6106 */

  /* Not saved in states: the cartridge, and output buffers. */
  const uint8_t *rom;          /* big-endian (.z64 order) */
  uint32_t rom_size;
} N64;

/* ---- system (n64.c) ---- */
bool n64_load_rom(N64 *n, const uint8_t *data, size_t size, uint8_t **owned_rom);
void n64_reset(N64 *n);
void n64_run_frame(N64 *n);
void n64_raise_mi(N64 *n, uint32_t bit);
void n64_clear_mi(N64 *n, uint32_t bit);
void n64_update_interrupts(N64 *n);

/* ---- CPU (cpu.c) ---- */
void cpu_reset(N64 *n);
void cpu_run(N64 *n, uint32_t cycles);
void cpu_step(N64 *n);

/* ---- FPU (fpu.c) ---- */
void fpu_cop1(N64 *n, uint32_t op);
void fpu_lwc1(N64 *n, uint32_t ft, uint32_t value);
void fpu_ldc1(N64 *n, uint32_t ft, uint64_t value);
uint32_t fpu_swc1(N64 *n, uint32_t ft);
uint64_t fpu_sdc1(N64 *n, uint32_t ft);

/* ---- bus (bus.c): physical addresses ---- */
bool bus_read8(N64 *n, uint32_t pa, uint8_t *v);
bool bus_read16(N64 *n, uint32_t pa, uint16_t *v);
bool bus_read32(N64 *n, uint32_t pa, uint32_t *v);
bool bus_read64(N64 *n, uint32_t pa, uint64_t *v);
bool bus_write8(N64 *n, uint32_t pa, uint8_t v);
bool bus_write16(N64 *n, uint32_t pa, uint16_t v);
bool bus_write32(N64 *n, uint32_t pa, uint32_t v);
bool bus_write64(N64 *n, uint32_t pa, uint64_t v);
void vi_tick(N64 *n, uint32_t cycles);
void ai_tick(N64 *n, uint32_t cycles);
#define AUDIO_RATE 44100u

/* ---- RSP (rsp.c) ---- */
void rsp_run(N64 *n, uint32_t cycles);
uint32_t rcp_reg_read(N64 *n, uint32_t pa);
void rcp_reg_write(N64 *n, uint32_t pa, uint32_t v);

/* ---- PIF / joybus / boot (pif.c) ---- */
void pif_hle_boot(N64 *n);
void pif_process(N64 *n);
uint32_t cic_detect(const uint8_t *rom, uint32_t size);
SaveType save_guess(const uint8_t *rom, uint32_t size);

/* ---- video out (vi.c) ---- */
/* Converts the current VI framebuffer to XRGB8888. Returns false when blank. */
bool vi_render(N64 *n, uint32_t *out, uint32_t max_w, uint32_t max_h, uint32_t *w, uint32_t *h);

/* ---- helpers ---- */
static inline uint16_t rd16be(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint64_t rd64be(const uint8_t *p) { return (uint64_t)rd32be(p) << 32 | rd32be(p + 4); }
static inline void wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline void wr64be(uint8_t *p, uint64_t v) { wr32be(p, (uint32_t)(v >> 32)); wr32be(p + 4, (uint32_t)v); }
static inline uint64_t sext32(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }
static inline uint64_t sext16(uint16_t v) { return (uint64_t)(int64_t)(int16_t)v; }
static inline uint64_t sext8(uint8_t v) { return (uint64_t)(int64_t)(int8_t)v; }

#ifdef __cplusplus
}
#endif
#endif
