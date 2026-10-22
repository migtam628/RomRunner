/* RomRunner PS1: the whole machine as one plain struct (save states copy
 * it), plus the functions each part exposes. Written from public hardware
 * documentation (the R3000A manual, nocash's PSX-SPX); no code from other
 * emulators. */
#ifndef RRPS1_H
#define RRPS1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CPU_HZ 33868800u
#define RAM_SIZE 0x200000u      /* 2 MB, mirrored through the first 8 MB */
#define BIOS_SIZE 0x80000u      /* 512 KB */
#define SCRATCH_SIZE 0x400u     /* 1 KB data cache used as fast RAM */
#define VRAM_W 1024
#define VRAM_H 512

/* Interrupt lines (I_STAT / I_MASK bits). */
enum {
  IRQ_VBLANK = 1 << 0, IRQ_GPU = 1 << 1, IRQ_CDROM = 1 << 2, IRQ_DMA = 1 << 3,
  IRQ_TMR0 = 1 << 4, IRQ_TMR1 = 1 << 5, IRQ_TMR2 = 1 << 6, IRQ_PAD = 1 << 7,
  IRQ_SIO = 1 << 8, IRQ_SPU = 1 << 9, IRQ_PIO = 1 << 10,
};

/* COP0 registers the R3000A has. */
enum { C0_BPC = 3, C0_BDA = 5, C0_TAR = 6, C0_DCIC = 7, C0_BADVADDR = 8, C0_BDAM = 9, C0_BPCM = 11,
       C0_SR = 12, C0_CAUSE = 13, C0_EPC = 14, C0_PRID = 15 };

/* Exception codes (Cause bits 2-6). */
enum { EXC_INT = 0, EXC_ADEL = 4, EXC_ADES = 5, EXC_IBE = 6, EXC_DBE = 7, EXC_SYS = 8, EXC_BP = 9,
       EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12 };

#define SR_IEC (1u << 0)
#define SR_ISC (1u << 16)  /* isolate cache: stores go to the cache, not memory */
#define SR_BEV (1u << 22)

typedef struct {
  uint32_t gpr[32];
  uint32_t hi, lo;
  uint32_t pc;        /* instruction to execute next */
  uint32_t next_pc;   /* the one after it (branch target when pc is a delay slot) */
  uint32_t cur_pc;    /* address of the instruction executing now (for EPC) */
  bool in_delay;      /* pc sits in a branch delay slot */
  bool cur_delay;
  bool branch_taken;  /* the branch whose delay slot is executing was taken (Cause.BT) */
  /* Load delay: a load's result reaches the register one instruction late. */
  uint32_t load_reg, load_val;      /* issued by the instruction just executed */
  uint32_t pend_reg, pend_val;      /* to land after the current instruction */
  uint32_t write_reg;               /* register the current instruction wrote */
  uint32_t cop0[16];
  uint64_t cycles;
  /* Multiply/divide unit busy until this cycle (MFHI/MFLO stall). */
  uint64_t muldiv_ready;
} Cpu;

typedef struct {
  uint32_t madr, bcr, chcr;
} DmaChannel;

typedef struct {
  DmaChannel ch[7];
  uint32_t dpcr, dicr;
} Dma;

typedef struct {
  uint32_t count, mode, target;
  uint32_t frac;      /* sub-tick remainder for divided sources */
  bool irq_done;      /* one-shot mode already fired */
} Timer;

/* GPU (gpu.c): GP0 command assembly, VRAM, display settings. */
typedef struct {
  uint16_t vram[VRAM_W * VRAM_H];
  uint32_t fifo[16];
  uint32_t fifo_len, cmd_words;     /* words collected / needed for the current GP0 command */
  bool poly_line;                   /* collecting a poly-line until the terminator */
  /* CPU<->VRAM transfers (GP0 A0 / C0) */
  uint32_t xfer_x, xfer_y, xfer_w, xfer_h, xfer_i, xfer_n;
  bool xfer_in, xfer_out;
  uint32_t read_latch;              /* GPUREAD */
  /* Drawing state */
  uint32_t texpage;                 /* E1 */
  uint32_t texwin;                  /* E2 */
  int32_t clip_x1, clip_y1, clip_x2, clip_y2;   /* E3/E4 */
  int32_t off_x, off_y;             /* E5 */
  bool mask_set, mask_check;        /* E6 */
  bool tex_disable;
  /* Display (GP1) */
  bool display_off;
  uint32_t dma_dir;
  uint32_t disp_x, disp_y;          /* VRAM start of the displayed area */
  uint32_t h_start, h_end, v_start, v_end;
  uint32_t hres, vres_bit, pal, color24, interlace, hres2;
  bool field;                       /* odd/even field (interlace) */
  bool irq;                         /* GP0 1F requested */
  /* CLUT cache: the palette is read from VRAM once and reused until a
   * primitive needs a different one (position or 4-bit/8-bit). */
  uint16_t clut_cache[256];
  uint32_t clut_tag_x, clut_tag_y, clut_tag_depth;   /* depth: entries loaded */
  bool clut_valid;
} Gpu;

/* Controller port (SIO0). */
typedef struct {
  uint16_t buttons;                 /* PS1 layout, active high here; inverted on the wire */
  bool connected;
} Pad;

typedef struct {
  uint16_t stat, mode, ctrl, baud;
  uint8_t rx;
  bool rx_full;
  int seq;                          /* position in the current controller exchange */
  uint32_t ack_at;                  /* cycle at which /ACK raises IRQ7 (0 = none) */
  int device;                       /* 0 none, 1 pad, 2 memory card */
} Sio;

typedef struct PS1 {
  Cpu cpu;
  Dma dma;
  Timer tmr[3];
  Gpu gpu;
  Sio sio;
  Pad pad[2];
  uint32_t i_stat, i_mask;
  uint32_t mem_ctrl[9], ram_size, cache_ctrl;
  uint32_t spu_regs[0x200];         /* SPU register file, stage 4 */
  uint32_t gte_data[32], gte_ctrl[32];   /* GTE registers (gte.c) */
  uint8_t cd_index;                 /* CD-ROM register bank (stage 4) */
  uint8_t ram[RAM_SIZE];
  uint8_t scratch[SCRATCH_SIZE];

  /* Frame timing */
  uint32_t line, line_cycles;       /* current scanline and cycles into it */
  bool in_vblank, in_hblank;
  bool pal;
  bool frame_done;

  /* tty output (BIOS putchar and the expansion-2 debug UART) for tests */
  char tty[4096];
  uint32_t tty_len;
  bool hle_bios;                    /* no BIOS image: answer a few BIOS calls ourselves */

  /* EXE sideloading: with a BIOS, the EXE replaces the shell when the BIOS
   * jumps to it (0x80030000), after the kernel is set up. */
  bool exe_pending;

  /* Not in save states: */
  const uint8_t *bios;              /* 512 KB, owned by the frontend glue */
  const uint8_t *exe;               /* PS-EXE waiting to be sideloaded */
  size_t exe_size;
} PS1;

/* little-endian access helpers */
static inline uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void wr16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* cpu.c */
void cpu_reset(PS1 *p);
void cpu_step(PS1 *p);
void cpu_run(PS1 *p, uint32_t cycles);

/* bus.c: physical addresses; false = bus error (no device). */
bool bus_read(PS1 *p, uint32_t pa, int size, uint32_t *v);
bool bus_write(PS1 *p, uint32_t pa, int size, uint32_t v);
void irq_raise(PS1 *p, uint32_t bits);

/* dma.c */
void dma_write(PS1 *p, uint32_t off, uint32_t v);
uint32_t dma_read(PS1 *p, uint32_t off);

/* timers.c */
void timers_tick(PS1 *p, uint32_t cycles);
void timers_hblank(PS1 *p);
void timers_vblank(PS1 *p, bool start);
uint32_t timer_read(PS1 *p, uint32_t off);
void timer_write(PS1 *p, uint32_t off, uint32_t v);

/* gpu.c */
void gpu_reset(PS1 *p);
void gp0_write(PS1 *p, uint32_t v);
void gp1_write(PS1 *p, uint32_t v);
uint32_t gpu_read(PS1 *p);
uint32_t gpu_stat(PS1 *p);
bool gpu_render(PS1 *p, uint32_t *out, uint32_t max_w, uint32_t max_h, uint32_t *w, uint32_t *h);

/* sio.c */
uint32_t sio_read(PS1 *p, uint32_t off, int size);
void sio_write(PS1 *p, uint32_t off, int size, uint32_t v);
void sio_tick(PS1 *p);

/* ps1.c */
bool ps1_load_bios(PS1 *p, const uint8_t *bios, size_t size);
void ps1_reset(PS1 *p);
bool ps1_load_exe(PS1 *p, const uint8_t *exe, size_t size);
void ps1_sideload(PS1 *p);
void ps1_run_frame(PS1 *p);
void ps1_tty_putc(PS1 *p, char c);

#endif
