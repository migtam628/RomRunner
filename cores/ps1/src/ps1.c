/* System glue: reset, BIOS and PS-EXE loading, the per-frame scheduler
 * (scanlines, hblank, vblank) and tty output. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps1.h"

/* CPU cycles per scanline: 3413 (NTSC) / 3406 (PAL) GPU clocks at 11/7 of
 * the CPU clock. */
#define NTSC_LINE 2172
#define PAL_LINE 2168
#define NTSC_LINES 263
#define PAL_LINES 314

bool ps1_load_bios(PS1 *p, const uint8_t *bios, size_t size) {
  if (!bios || size != BIOS_SIZE) return false;
  p->bios = bios;
  return true;
}

void ps1_reset(PS1 *p) {
  /* Keep the BIOS, controllers and pending EXE; clear the machine. */
  const uint8_t *bios = p->bios, *exe = p->exe;
  size_t exe_size = p->exe_size;
  Pad pads[2];
  memcpy(pads, p->pad, sizeof pads);
  bool pal = p->pal;
  memset(p, 0, offsetof(PS1, bios));
  p->bios = bios;
  p->exe = exe;
  p->exe_size = exe_size;
  memcpy(p->pad, pads, sizeof pads);
  p->pal = pal;
  p->hle_bios = bios == NULL;
  p->ram_size = 0x00000B88;
  cpu_reset(p);
  gpu_reset(p);
  for (int i = 0; i < 3; i++) p->tmr[i].mode = 0x400;
  if (exe) {
    if (bios) p->exe_pending = true;
    else ps1_sideload(p);
  }
}

static bool exe_header_ok(const uint8_t *exe, size_t size) {
  return exe && size >= 0x800 && !memcmp(exe, "PS-X EXE", 8);
}

bool ps1_load_exe(PS1 *p, const uint8_t *exe, size_t size) {
  if (!exe_header_ok(exe, size)) return false;
  p->exe = exe;
  p->exe_size = size;
  return true;
}

/* Copy the EXE into RAM and start it: pc, gp, sp from the header; the BSS
 * area cleared. */
void ps1_sideload(PS1 *p) {
  const uint8_t *e = p->exe;
  p->exe_pending = false;
  if (!exe_header_ok(e, p->exe_size)) return;
  uint32_t pc = rd32le(e + 0x10), gp = rd32le(e + 0x14);
  uint32_t dst = rd32le(e + 0x18), len = rd32le(e + 0x1C);
  uint32_t bss = rd32le(e + 0x28), bss_len = rd32le(e + 0x2C);
  uint32_t sp = rd32le(e + 0x30) + rd32le(e + 0x34);
  if (len > p->exe_size - 0x800) len = (uint32_t)(p->exe_size - 0x800);
  for (uint32_t i = 0; i < len; i++) p->ram[(dst + i) & (RAM_SIZE - 1)] = e[0x800 + i];
  for (uint32_t i = 0; i < bss_len; i++) p->ram[(bss + i) & (RAM_SIZE - 1)] = 0;
  Cpu *c = &p->cpu;
  c->pc = pc;
  c->next_pc = pc + 4;
  c->in_delay = false;
  c->load_reg = 0;
  c->gpr[28] = gp;
  c->gpr[29] = c->gpr[30] = rd32le(e + 0x30) ? sp : 0x801FFFF0u;
  if (p->hle_bios) {
    /* No kernel: interrupts stay off and BIOS calls are answered in hle.c.
     * Exceptions go to 0x80000080 in RAM, where a program can put its own
     * handler. */
    c->cop0[C0_SR] = 0;
  }
}

void ps1_tty_putc(PS1 *p, char ch) {
  if (p->tty_len < sizeof p->tty - 1) {
    p->tty[p->tty_len++] = ch;
    p->tty[p->tty_len] = 0;
  }
  if (getenv("RRPS1_TTY")) fputc(ch, stderr);
}

/* One frame, ending just before vblank starts: by then the game has
 * finished drawing the picture it wants shown, and the next call raises
 * the vblank interrupt on its first line. */
void ps1_run_frame(PS1 *p) {
  uint32_t lines = p->pal ? PAL_LINES : NTSC_LINES;
  uint32_t per_line = p->pal ? PAL_LINE : NTSC_LINE;
  Gpu *g = &p->gpu;
  /* Visible lines from the GP1(07) range, else the usual 240/288. */
  uint32_t vis_start = g->v_start, vis_end = g->v_end;
  if (vis_end <= vis_start || vis_end > lines) {
    vis_start = 16;
    vis_end = p->pal ? 16 + 288 : 16 + 240;
  }
  p->frame_done = false;
  uint32_t guard = 0;
  do {
    uint32_t l = p->line % lines;
    bool vblank = l < vis_start || l >= vis_end;
    if (l == vis_end) {
      irq_raise(p, IRQ_VBLANK);
      timers_vblank(p, true);
      if (g->interlace) g->field = !g->field;
    }
    p->in_vblank = vblank;
    /* The line in slices so timers, SIO /ACK and DMA land close to when
     * they should. */
    for (uint32_t done = 0; done < per_line; done += 128) {
      uint32_t slice = per_line - done < 128 ? per_line - done : 128;
      cpu_run(p, slice);
      timers_tick(p, slice);
      sio_tick(p);
    }
    timers_hblank(p);
    p->line = (l + 1) % lines;
  } while (p->line != vis_end && ++guard < lines);
  p->frame_done = true;
}
