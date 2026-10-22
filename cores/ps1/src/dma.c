/* DMA controller: 7 channels (0 MDEC in, 1 MDEC out, 2 GPU, 3 CD-ROM,
 * 4 SPU, 5 PIO, 6 ordering-table clear). Transfers complete at once when
 * started; DICR raises IRQ3 per channel.
 *
 * CHCR: bit 0 direction (1 = from RAM), bit 1 step backwards, bits 9-10
 * sync mode (0 all at once, 1 blocks, 2 linked list), bit 24 busy, bit 28
 * start (sync mode 0). */
#include "ps1.h"

uint32_t cdrom_dma_word(PS1 *p);
void spu_dma_write(PS1 *p, uint32_t v);
uint32_t spu_dma_read(PS1 *p);
void mdec_dma_in(PS1 *p, uint32_t v);
uint32_t mdec_dma_out(PS1 *p);

static void update_dicr_irq(PS1 *p) {
  uint32_t d = p->dma.dicr;
  bool before = (d & 0x80000000u) != 0;
  bool master = (d & 0x8000u) || ((d & 0x800000u) && ((d >> 16) & (d >> 24) & 0x7F));
  p->dma.dicr = (d & 0x7FFFFFFFu) | (master ? 0x80000000u : 0);
  if (master && !before) irq_raise(p, IRQ_DMA);
}

static void finish(PS1 *p, int n) {
  DmaChannel *c = &p->dma.ch[n];
  c->chcr &= ~(0x01000000u | 0x10000000u);
  if (p->dma.dicr & (1u << (16 + n))) p->dma.dicr |= 1u << (24 + n);
  update_dicr_irq(p);
}

static uint32_t ram_rd(PS1 *p, uint32_t a) { return rd32le(p->ram + (a & (RAM_SIZE - 4))); }
static void ram_wr(PS1 *p, uint32_t a, uint32_t v) { wr32le(p->ram + (a & (RAM_SIZE - 4)), v); }

static void word_from_ram(PS1 *p, int n, uint32_t v) {
  switch (n) {
    case 0: mdec_dma_in(p, v); break;
    case 2: gp0_write(p, v); break;
    case 4: spu_dma_write(p, v); break;
    default: break;
  }
}

static uint32_t word_to_ram(PS1 *p, int n) {
  switch (n) {
    case 1: return mdec_dma_out(p);
    case 2: return gpu_read(p);
    case 3: return cdrom_dma_word(p);
    case 4: return spu_dma_read(p);
    default: return 0;
  }
}

static void run(PS1 *p, int n) {
  DmaChannel *c = &p->dma.ch[n];
  uint32_t chcr = c->chcr;
  uint32_t sync = (chcr >> 9) & 3;
  bool from_ram = chcr & 1;
  int32_t step = (chcr & 2) ? -4 : 4;
  uint32_t addr = c->madr & 0xFFFFFC;

  if (n == 6) {
    /* Ordering table clear: a backwards chain of empty entries ending in
     * the end-of-list marker. */
    uint32_t count = c->bcr & 0xFFFF;
    if (count == 0) count = 0x10000;
    for (uint32_t i = 0; i < count; i++) {
      ram_wr(p, addr, i == count - 1 ? 0x00FFFFFFu : ((addr - 4) & 0xFFFFFF));
      addr -= 4;
    }
    p->cpu.cycles += count;
    finish(p, n);
    return;
  }

  if (sync == 2) {
    /* Linked list (GPU): each node is a header (count << 24 | next) and
     * count command words. Bit 23 of the next address ends the list. */
    uint32_t guard = 0;
    for (;;) {
      uint32_t header = ram_rd(p, addr);
      uint32_t count = header >> 24;
      for (uint32_t i = 0; i < count; i++) word_from_ram(p, n, ram_rd(p, addr + 4 + i * 4));
      p->cpu.cycles += count + 1;
      if (header & 0x800000u) break;
      addr = header & 0xFFFFFC;
      if (++guard > 0x100000) break;   /* a list that loops forever */
    }
    c->madr = 0x00FFFFFFu;
    finish(p, n);
    return;
  }

  uint32_t words;
  if (sync == 0) {
    words = c->bcr & 0xFFFF;
    if (words == 0) words = 0x10000;
  } else {
    uint32_t bs = c->bcr & 0xFFFF, blocks = c->bcr >> 16;
    if (bs == 0) bs = 0x10000;
    words = bs * blocks;
  }
  for (uint32_t i = 0; i < words; i++) {
    if (from_ram) word_from_ram(p, n, ram_rd(p, addr));
    else ram_wr(p, addr, word_to_ram(p, n));
    addr += (uint32_t)step;
  }
  p->cpu.cycles += words;
  if (sync == 1) {
    c->madr = addr & 0xFFFFFF;
    c->bcr &= 0xFFFF;   /* block count runs down to 0 */
  }
  finish(p, n);
}

uint32_t dma_read(PS1 *p, uint32_t off) {
  uint32_t ch = off >> 4, reg = off & 0xF;
  if (ch < 7) {
    DmaChannel *c = &p->dma.ch[ch];
    return reg == 0 ? c->madr : reg == 4 ? c->bcr : reg == 8 ? c->chcr : 0;
  }
  if (off == 0x70) return p->dma.dpcr;
  if (off == 0x74) return p->dma.dicr;
  return 0;
}

void dma_write(PS1 *p, uint32_t off, uint32_t v) {
  uint32_t ch = off >> 4, reg = off & 0xF;
  if (ch < 7) {
    DmaChannel *c = &p->dma.ch[ch];
    if (reg == 0) c->madr = v & 0xFFFFFF;
    else if (reg == 4) c->bcr = v;
    else if (reg == 8) {
      /* OTC only has the start/trigger bits and runs backwards. */
      c->chcr = ch == 6 ? (v & 0x51000000u) | 2 : v & 0x71770703u;
    }
    bool enabled = p->dma.dpcr & (8u << (ch * 4));
    uint32_t sync = (c->chcr >> 9) & 3;
    bool start = (c->chcr & 0x01000000u) && (sync != 0 || (c->chcr & 0x10000000u));
    if (reg == 8 && start && enabled) run(p, (int)ch);
    return;
  }
  if (off == 0x70) {
    p->dma.dpcr = v;
    return;
  }
  if (off == 0x74) {
    /* Bits 24-30 acknowledge by writing 1; 0-5 and 15-23 are settings. */
    uint32_t flags = (p->dma.dicr & ~(v & 0x7F000000u)) & 0x7F000000u;
    p->dma.dicr = (v & 0x00FF803Fu) | flags;
    update_dicr_irq(p);
  }
}
