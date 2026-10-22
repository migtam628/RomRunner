/* Physical memory map and I/O registers.
 *
 *   00000000-007FFFFF  RAM (2 MB, mirrored 4 times)        cpu.c fast path
 *   1F000000-1F7FFFFF  expansion 1 (nothing: reads FF)
 *   1F800000-1F8003FF  scratchpad
 *   1F801000-1F801FFF  I/O: memory control, SIO, interrupts, DMA, timers,
 *                      CD-ROM, GPU, MDEC, SPU
 *   1F802000-1F803FFF  expansion 2 (debug UART / POST display)
 *   1FC00000-1FC7FFFF  BIOS ROM
 *   FFFE0130           cache control
 */
#include "ps1.h"

uint32_t cdrom_read(PS1 *p, uint32_t off);
void cdrom_write(PS1 *p, uint32_t off, uint32_t v);
uint32_t mdec_read(PS1 *p, uint32_t off);
void mdec_write(PS1 *p, uint32_t off, uint32_t v);

void irq_raise(PS1 *p, uint32_t bits) { p->i_stat |= bits; }

static uint32_t io_read(PS1 *p, uint32_t pa, int size) {
  uint32_t off = pa - 0x1F801000u;
  if (off < 0x24) return p->mem_ctrl[off >> 2];
  if (off >= 0x40 && off < 0x60) return sio_read(p, off - 0x40, size);
  if (off == 0x60) return p->ram_size;
  if (off == 0x70) return p->i_stat;
  if (off == 0x74) return p->i_mask;
  if (off >= 0x80 && off < 0x100) {
    uint32_t v = dma_read(p, (off - 0x80) & ~3u);
    return v >> ((pa & 3) * 8);
  }
  if (off >= 0x100 && off < 0x130) return timer_read(p, off - 0x100);
  if (off >= 0x800 && off < 0x804) return cdrom_read(p, off - 0x800);
  if (off == 0x810) return gpu_read(p);
  if (off == 0x814) return gpu_stat(p);
  if (off == 0x820 || off == 0x824) return mdec_read(p, off - 0x820);
  if (off >= 0xC00 && off < 0x1000) {   /* SPU: 16-bit registers */
    uint32_t i = (off - 0xC00) >> 1;
    uint32_t v = p->spu_regs[i] & 0xFFFF;
    if (size == 4) v |= (p->spu_regs[i + 1] & 0xFFFF) << 16;
    return v;
  }
  return 0;
}

static void io_write(PS1 *p, uint32_t pa, int size, uint32_t v) {
  uint32_t off = pa - 0x1F801000u;
  if (off < 0x24) { p->mem_ctrl[off >> 2] = v; return; }
  if (off >= 0x40 && off < 0x60) { sio_write(p, off - 0x40, size, v); return; }
  if (off == 0x60) { p->ram_size = v; return; }
  if (off == 0x70) { p->i_stat &= v; return; }          /* write 0 to acknowledge */
  if (off == 0x74) { p->i_mask = v & 0x7FF; return; }
  if (off >= 0x80 && off < 0x100) {
    /* DMA registers take narrower writes shifted into place. */
    uint32_t sh = (pa & 3) * 8;
    dma_write(p, (off - 0x80) & ~3u, v << sh);
    return;
  }
  if (off >= 0x100 && off < 0x130) { timer_write(p, off - 0x100, v); return; }
  if (off >= 0x800 && off < 0x804) { cdrom_write(p, off - 0x800, v & 0xFF); return; }
  if (off == 0x810) { gp0_write(p, v); return; }
  if (off == 0x814) { gp1_write(p, v); return; }
  if (off == 0x820 || off == 0x824) { mdec_write(p, off - 0x820, v); return; }
  if (off >= 0xC00 && off < 0x1000) {
    uint32_t i = (off - 0xC00) >> 1;
    p->spu_regs[i] = v & 0xFFFF;
    if (size == 4 && i + 1 < sizeof p->spu_regs / sizeof p->spu_regs[0]) p->spu_regs[i + 1] = v >> 16;
    return;
  }
  /* 0x1F802000+: expansion 2, handled by the caller */
}

bool bus_read(PS1 *p, uint32_t pa, int size, uint32_t *v) {
  if (pa < 0x00800000u) {
    const uint8_t *m = p->ram + (pa & (RAM_SIZE - 1));
    *v = size == 4 ? rd32le(m) : size == 2 ? rd16le(m) : *m;
    return true;
  }
  if (pa >= 0x1F800000u && pa < 0x1F800400u) {
    const uint8_t *m = p->scratch + (pa & 0x3FF);
    *v = size == 4 ? rd32le(m) : size == 2 ? rd16le(m) : *m;
    return true;
  }
  if (pa >= 0x1F801000u && pa < 0x1F802000u) {
    uint32_t w = io_read(p, pa & ~(size == 1 ? 0u : (uint32_t)size - 1), size);
    *v = size == 4 ? w : size == 2 ? (w & 0xFFFF) : (w & 0xFF);
    return true;
  }
  if (pa >= 0x1FC00000u && pa < 0x1FC00000u + BIOS_SIZE) {
    if (!p->bios) { *v = 0; return true; }
    const uint8_t *m = p->bios + (pa - 0x1FC00000u);
    *v = size == 4 ? rd32le(m) : size == 2 ? rd16le(m) : *m;
    return true;
  }
  if (pa >= 0x1F000000u && pa < 0x1F800000u) {   /* expansion 1: open bus reads FF */
    *v = size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFF : 0xFF;
    return true;
  }
  if (pa >= 0x1F802000u && pa < 0x1F804000u) {   /* expansion 2 */
    *v = pa == 0x1F802021u ? 0x0C : 0;             /* DUART status: transmitter ready */
    return true;
  }
  if (pa == 0xFFFE0130u) { *v = p->cache_ctrl; return true; }
  return false;
}

bool bus_write(PS1 *p, uint32_t pa, int size, uint32_t v) {
  if (pa < 0x00800000u) {
    uint8_t *m = p->ram + (pa & (RAM_SIZE - 1));
    if (size == 4) wr32le(m, v); else if (size == 2) wr16le(m, (uint16_t)v); else *m = (uint8_t)v;
    return true;
  }
  if (pa >= 0x1F800000u && pa < 0x1F800400u) {
    uint8_t *m = p->scratch + (pa & 0x3FF);
    if (size == 4) wr32le(m, v); else if (size == 2) wr16le(m, (uint16_t)v); else *m = (uint8_t)v;
    return true;
  }
  if (pa >= 0x1F801000u && pa < 0x1F802000u) {
    io_write(p, pa & ~(size == 1 ? 0u : (uint32_t)size - 1), size, v);
    return true;
  }
  if (pa >= 0x1F802000u && pa < 0x1F804000u) {
    /* Debug UART transmit (used by test programs) and the POST display. */
    if (pa == 0x1F802023u || pa == 0x1F802080u) ps1_tty_putc(p, (char)v);
    return true;
  }
  if (pa >= 0x1F000000u && pa < 0x1F800000u) return true;
  if (pa >= 0x1FC00000u && pa < 0x1FC00000u + BIOS_SIZE) return true;   /* ROM */
  if (pa == 0xFFFE0130u) { p->cache_ctrl = v; return true; }
  return false;
}
