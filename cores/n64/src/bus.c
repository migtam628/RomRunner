/*
 * Physical memory map and the RCP's memory-mapped interfaces:
 * RDRAM, RSP memory/registers, RDP command registers, MI, VI, AI, PI, RI,
 * SI, the cartridge domains and PIF RAM.
 *
 * The RSP (rsp.c) and RDP (rdp.c) are driven from here.
 */
#include <string.h>

#include "n64.h"

/* ------------------------------------------------------------------ */
/* DMA helpers                                                          */

static void sp_dma(N64 *n, bool to_rdram, uint32_t v) {
  uint32_t len = (v & 0xFFF) | 7;          /* bytes - 1, rounded to 8 */
  uint32_t count = ((v >> 12) & 0xFF) + 1;
  uint32_t skip = (v >> 20) & 0xFF8;       /* the skip is in whole 8-byte units */
  uint8_t *mem = (n->sp.mem_addr & 0x1000) ? n->imem : n->dmem;
  uint32_t ma = n->sp.mem_addr & 0xFF8, da = n->sp.dram_addr & 0xFFFFF8;
  for (uint32_t i = 0; i < count; i++) {
    for (uint32_t j = 0; j <= len; j++) {
      uint32_t m = (ma + j) & 0xFFF, d = (da + j) & (RDRAM_SIZE - 1);
      if (to_rdram) n->rdram[d] = mem[m];
      else mem[m] = n->rdram[d];
    }
    ma = (ma + len + 1) & 0xFFF;
    da += len + 1;
    if (i + 1 < count) da += skip;  /* skip only between rows */
  }
  n->sp.cur_mem = (n->sp.mem_addr & 0x1000) | ma;
  n->sp.cur_dram = da & 0xFFFFF8;
  /* When done the length reads back as 0xFF8 with count 0 (skip kept). */
  n->sp.rd_len = n->sp.wr_len = (v & 0xFFF00000u) | 0xFF8;
}

static uint8_t cart_byte(N64 *n, uint32_t pa) {
  if (pa >= 0x10000000u && pa < 0x1FC00000u) {
    uint32_t o = pa - 0x10000000u;
    return o < n->rom_size ? n->rom[o] : 0;
  }
  if (pa >= 0x08000000u && pa < 0x08000000u + sizeof n->sram) return n->sram[pa - 0x08000000u];
  return 0;
}

static void pi_dma(N64 *n, bool to_rdram, uint32_t len) {
  uint32_t da = n->pi.dram_addr & 0xFFFFFE, ca = n->pi.cart_addr & ~1u;
  len = (len & 0xFFFFFF) + 1;
  uint32_t count = len;
  /* Into RDRAM, a destination that isn't 8-byte aligned shortens the first
   * block by the misalignment (as measured on hardware). */
  if (to_rdram && (da & 7) && len <= 128 - (da & 7)) count = len - (da & 7) > 0 ? len - (da & 7) : 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t d = (da + i) & (RDRAM_SIZE - 1);
    if (to_rdram) {
      n->rdram[d] = cart_byte(n, ca + i);
    } else if (ca + i >= 0x08000000u && ca + i < 0x08000000u + sizeof n->sram) {
      n->sram[ca + i - 0x08000000u] = n->rdram[d];
      n->save_dirty = true;
    }
  }
  n->pi.dram_addr = (da + len + 7) & ~7u & 0xFFFFFF;
  n->pi.cart_addr = ca + ((len + 1) & ~1u);
  n64_raise_mi(n, MI_INTR_PI);
}

/* ------------------------------------------------------------------ */
/* 32-bit register access                                               */

uint32_t rcp_reg_read(N64 *n, uint32_t pa) {
  uint32_t r = (pa & 0xFFFFF) >> 2;
  switch (pa >> 20) {
    case 0x040:
      if (pa < 0x04040000u) {
        uint8_t *m = (pa & 0x1000) ? n->imem : n->dmem;
        return rd32be(m + (pa & 0xFFC));
      }
      if (pa == 0x04080000u) return n->rsp.pc & 0xFFC;
      switch (pa & 0x1F) {
        case 0x00: return n->sp.cur_mem;
        case 0x04: return n->sp.cur_dram;
        case 0x08: return n->sp.rd_len;
        case 0x0C: return n->sp.wr_len;
        case 0x10: return n->sp.status;
        case 0x14: return 0;  /* DMA_FULL */
        case 0x18: return 0;  /* DMA_BUSY */
        case 0x1C: { uint32_t v = n->sp.semaphore; n->sp.semaphore = 1; return v; }
      }
      return 0;
    case 0x041:
      switch (r & 7) {
        case 0: return n->dp.start;
        case 1: return n->dp.end;
        case 2: return n->dp.current;
        case 3: return n->dp.status;
        case 4: return n->dp.clock;
        case 5: return n->dp.bufbusy;
        case 6: return n->dp.pipebusy;
        case 7: return n->dp.tmem;
      }
      return 0;
    case 0x043:
      switch (r & 3) {
        case 0: return n->mi.mode;
        case 1: return 0x02020102;  /* MI_VERSION */
        case 2: return n->mi.intr;
        case 3: return n->mi.mask;
      }
      return 0;
    case 0x044: {
      Vi *v = &n->vi;
      uint32_t *regs[] = {&v->ctrl, &v->origin, &v->width, &v->v_intr, &v->v_current, &v->burst, &v->v_sync,
                          &v->h_sync, &v->leap, &v->h_video, &v->v_video, &v->v_burst, &v->x_scale, &v->y_scale};
      return r < 14 ? *regs[r] : 0;
    }
    case 0x045:
      switch (r & 7) {
        case 1: return n->ai.count ? n->ai.len[0] - (uint32_t)n->ai.pos * 4 : 0; /* remaining length */
        case 3: {
          uint32_t st = 0x00000000u;
          if (n->ai.count >= 2) st |= 0x80000001u; /* full */
          if (n->ai.count >= 1) st |= 0x40000000u; /* busy */
          return st;
        }
        default: return n->ai.len[0];
      }
    case 0x046: {
      Pi *p = &n->pi;
      uint32_t *regs[] = {&p->dram_addr, &p->cart_addr, &p->rd_len, &p->wr_len, &p->status, &p->dom1_lat, &p->dom1_pwd,
                          &p->dom1_pgs, &p->dom1_rls, &p->dom2_lat, &p->dom2_pwd, &p->dom2_pgs, &p->dom2_rls};
      if (r == 4) return (n->mi.intr & MI_INTR_PI) ? 8 : 0;
      return r < 13 ? *regs[r] : 0;
    }
    case 0x047: return r < 8 ? n->ri[r] : 0;
    case 0x048:
      switch (pa & 0x1F) {
        case 0x00: return n->si.dram_addr;
        case 0x18: return (n->mi.intr & MI_INTR_SI) ? 0x1000 : 0;
      }
      return 0;
  }
  return 0;
}

static void sp_status_write(N64 *n, uint32_t v) {
  uint32_t st = n->sp.status;
  if (v & (1 << 0)) st &= ~1u;            /* clear halt */
  if (v & (1 << 1)) st |= 1u;             /* set halt */
  if (v & (1 << 2)) st &= ~2u;            /* clear broke */
  if (v & (1 << 3)) n64_clear_mi(n, MI_INTR_SP);
  if (v & (1 << 4)) n64_raise_mi(n, MI_INTR_SP);
  if (v & (1 << 5)) st &= ~0x20u;         /* single step */
  if (v & (1 << 6)) st |= 0x20u;
  if (v & (1 << 7)) st &= ~0x40u;         /* interrupt on break */
  if (v & (1 << 8)) st |= 0x40u;
  for (int i = 0; i < 8; i++) {           /* signals 0..7 */
    if (v & (1u << (9 + i * 2))) st &= ~(0x80u << i);
    if (v & (1u << (10 + i * 2))) st |= 0x80u << i;
  }
  n->sp.status = st;
}

void rcp_reg_write(N64 *n, uint32_t pa, uint32_t v) {
  uint32_t r = (pa & 0xFFFFF) >> 2;
  switch (pa >> 20) {
    case 0x040:
      if (pa < 0x04040000u) {
        uint8_t *m = (pa & 0x1000) ? n->imem : n->dmem;
        wr32be(m + (pa & 0xFFC), v);
        return;
      }
      if (pa == 0x04080000u) {
        n->sp.pc = v & 0xFFC;
        n->rsp.pc = n->sp.pc;
        n->rsp.next_pc = (n->rsp.pc + 4) & 0xFFC;
        n->rsp.in_delay = false;
        return;
      }
      switch (pa & 0x1F) {
        case 0x00: n->sp.mem_addr = v & 0x1FF8; return;
        case 0x04: n->sp.dram_addr = v & 0xFFFFF8; return;
        case 0x08: sp_dma(n, false, v); return;
        case 0x0C: sp_dma(n, true, v); return;
        case 0x10: sp_status_write(n, v); return;
        case 0x1C: n->sp.semaphore = 0; return;
      }
      return;
    case 0x041:
      switch (r & 7) {
        case 0: n->dp.start = n->dp.current = v & 0xFFFFF8; return;
        case 1:
          /* Draw everything up to the new end (SYNC_FULL raises the interrupt). */
          n->dp.end = v & 0xFFFFF8;
          if (!(n->dp.status & 2)) rdp_run(n);  /* not frozen */
          return;
        case 3: {
          uint32_t st = n->dp.status;
          if (v & 0x01) st &= ~1u;   /* XBUS */
          if (v & 0x02) st |= 1u;
          if (v & 0x04) st &= ~2u;   /* freeze */
          if (v & 0x08) st |= 2u;
          if (v & 0x10) st &= ~4u;   /* flush */
          if (v & 0x20) st |= 4u;
          if (v & 0x200) n->dp.clock = 0;
          n->dp.status = st;
          return;
        }
      }
      return;
    case 0x043:
      switch (r & 3) {
        case 0: {
          uint32_t m = (n->mi.mode & ~0x7Fu) | (v & 0x7F);
          if (v & (1 << 7)) m &= ~0x80u;
          if (v & (1 << 8)) m |= 0x80u;
          if (v & (1 << 9)) m &= ~0x100u;
          if (v & (1 << 10)) m |= 0x100u;
          if (v & (1 << 11)) n64_clear_mi(n, MI_INTR_DP);
          if (v & (1 << 12)) m &= ~0x200u;
          if (v & (1 << 13)) m |= 0x200u;
          n->mi.mode = m;
          return;
        }
        case 3:
          for (int i = 0; i < 6; i++) {
            if (v & (1u << (i * 2))) n->mi.mask &= ~(1u << i);
            if (v & (1u << (i * 2 + 1))) n->mi.mask |= 1u << i;
          }
          n64_update_interrupts(n);
          return;
      }
      return;
    case 0x044: {
      Vi *vi = &n->vi;
      switch (r) {
        case 0: vi->ctrl = v; return;
        case 1: vi->origin = v & 0xFFFFFF; return;
        case 2: vi->width = v & 0xFFF; return;
        case 3: vi->v_intr = v & 0x3FF; return;
        case 4: n64_clear_mi(n, MI_INTR_VI); return;
        case 5: vi->burst = v; return;
        case 6: vi->v_sync = v & 0x3FF; return;
        case 7: vi->h_sync = v; return;
        case 8: vi->leap = v; return;
        case 9: vi->h_video = v; return;
        case 10: vi->v_video = v; return;
        case 11: vi->v_burst = v; return;
        case 12: vi->x_scale = v; return;
        case 13: vi->y_scale = v; return;
      }
      return;
    }
    case 0x045: {
      Ai *a = &n->ai;
      switch (r & 7) {
        case 0: a->dram_addr[a->count < 2 ? a->count : 1] = v & 0xFFFFF8; return;
        case 1:
          if (a->count < 2 && (v & 0x3FFF8)) {
            a->len[a->count] = v & 0x3FFF8;
            if (a->count == 0) a->pos = 0;
            a->count++;
          }
          return;
        case 2: a->control = v & 1; return;
        case 3: n64_clear_mi(n, MI_INTR_AI); return;
        case 4: a->dacrate = v & 0x3FFF; return;
        case 5: a->bitrate = v & 0xF; return;
      }
      return;
    }
    case 0x046: {
      Pi *p = &n->pi;
      switch (r) {
        case 0: p->dram_addr = v & 0xFFFFFE; return;
        case 1: p->cart_addr = v & ~1u; return;
        case 2: pi_dma(n, false, v); p->rd_len = p->wr_len = 0x7F; return;  /* read back 0x7F when done */
        case 3: pi_dma(n, true, v); p->rd_len = p->wr_len = 0x7F; return;
        case 4: if (v & 2) n64_clear_mi(n, MI_INTR_PI); return;
        case 5: p->dom1_lat = v & 0xFF; return;
        case 6: p->dom1_pwd = v & 0xFF; return;
        case 7: p->dom1_pgs = v & 0xF; return;
        case 8: p->dom1_rls = v & 3; return;
        case 9: p->dom2_lat = v & 0xFF; return;
        case 10: p->dom2_pwd = v & 0xFF; return;
        case 11: p->dom2_pgs = v & 0xF; return;
        case 12: p->dom2_rls = v & 3; return;
      }
      return;
    }
    case 0x047: if (r < 8) n->ri[r] = v; return;
    case 0x048:
      switch (pa & 0x1F) {
        case 0x00: n->si.dram_addr = v & 0xFFFFF8; return;
        case 0x04: /* PIF RAM -> RDRAM */
          for (uint32_t i = 0; i < PIF_RAM_SIZE; i++)
            n->rdram[(n->si.dram_addr + i) & (RDRAM_SIZE - 1)] = n->pif_ram[i];
          n64_raise_mi(n, MI_INTR_SI);
          return;
        case 0x10: /* RDRAM -> PIF RAM */
          for (uint32_t i = 0; i < PIF_RAM_SIZE; i++)
            n->pif_ram[i] = n->rdram[(n->si.dram_addr + i) & (RDRAM_SIZE - 1)];
          pif_process(n);
          n64_raise_mi(n, MI_INTR_SI);
          return;
        case 0x18: n64_clear_mi(n, MI_INTR_SI); return;
      }
      return;
  }
}

/* ------------------------------------------------------------------ */
/* Public bus                                                           */

static inline bool is_rcp(uint32_t pa) { return pa >= 0x04000000u && pa < 0x04900000u; }

bool bus_read32(N64 *n, uint32_t pa, uint32_t *v) {
  if (pa < RDRAM_SIZE) { *v = rd32be(n->rdram + pa); return true; }
  if (is_rcp(pa)) {
    *v = rcp_reg_read(n, pa);
    Cpu *c = &n->cpu;
    if (c->cur_pc == c->poll_pc && pa == c->poll_addr && *v == c->poll_value) {
      if (++c->poll_count >= 8) c->idle = true;
    } else {
      c->poll_pc = c->cur_pc;
      c->poll_addr = pa;
      c->poll_value = *v;
      c->poll_count = 0;
    }
    return true;
  }
  if (pa >= 0x10000000u && pa < 0x1FC00000u) {
    uint32_t o = pa - 0x10000000u;
    /* Past the end of the ROM the bus returns the low address bits. */
    *v = o + 3 < n->rom_size ? rd32be(n->rom + o) : ((pa & 0xFFFF) << 16 | (pa & 0xFFFF));
    return true;
  }
  if (pa >= 0x08000000u && pa < 0x08000000u + sizeof n->sram) { *v = rd32be(n->sram + (pa - 0x08000000u)); return true; }
  if (pa >= 0x1FC007C0u && pa < 0x1FC00800u) { *v = rd32be(n->pif_ram + (pa - 0x1FC007C0u)); return true; }
  *v = 0;
  return pa < 0x03F00000u || (pa >= 0x03F00000u && pa < 0x04000000u); /* RDRAM regs read as 0 */
}

bool bus_read64(N64 *n, uint32_t pa, uint64_t *v) {
  if (pa + 7 < RDRAM_SIZE) { *v = rd64be(n->rdram + pa); return true; }
  uint32_t hi, lo;
  bool ok_hi = bus_read32(n, pa, &hi);
  bool ok_lo = bus_read32(n, pa + 4, &lo);
  bool ok = ok_hi && ok_lo;
  *v = (uint64_t)hi << 32 | lo;
  return ok;
}

bool bus_read16(N64 *n, uint32_t pa, uint16_t *v) {
  if (pa < RDRAM_SIZE) { *v = rd16be(n->rdram + pa); return true; }
  uint32_t w;
  bool ok = bus_read32(n, pa & ~3u, &w);
  *v = (uint16_t)(w >> (16 - 8 * (pa & 2)));
  return ok;
}

bool bus_read8(N64 *n, uint32_t pa, uint8_t *v) {
  if (pa < RDRAM_SIZE) { *v = n->rdram[pa]; return true; }
  uint32_t w;
  bool ok = bus_read32(n, pa & ~3u, &w);
  *v = (uint8_t)(w >> (24 - 8 * (pa & 3)));
  return ok;
}

bool bus_write32(N64 *n, uint32_t pa, uint32_t v) {
  if (pa < RDRAM_SIZE) { wr32be(n->rdram + pa, v); return true; }
  if (is_rcp(pa)) { rcp_reg_write(n, pa, v); return true; }
  if (pa >= 0x08000000u && pa < 0x08000000u + sizeof n->sram) {
    wr32be(n->sram + (pa - 0x08000000u), v);
    n->save_dirty = true;
    return true;
  }
  if (pa >= 0x1FC007C0u && pa < 0x1FC00800u) {
    wr32be(n->pif_ram + (pa - 0x1FC007C0u), v);
    pif_process(n);
    return true;
  }
  return false;
}

bool bus_write64(N64 *n, uint32_t pa, uint64_t v) {
  if (pa + 7 < RDRAM_SIZE) { wr64be(n->rdram + pa, v); return true; }
  bool ok_hi = bus_write32(n, pa, (uint32_t)(v >> 32));
  bool ok_lo = bus_write32(n, pa + 4, (uint32_t)v);
  return ok_hi && ok_lo;
}

/* RCP registers take whole words: narrower stores write the value shifted
 * into place (the rest of the word is the same data, as on hardware). */
bool bus_write16(N64 *n, uint32_t pa, uint16_t v) {
  if (pa < RDRAM_SIZE) { wr16be(n->rdram + pa, v); return true; }
  if (pa >= 0x08000000u && pa + 1 < 0x08000000u + sizeof n->sram) {
    wr16be(n->sram + (pa - 0x08000000u), v);
    n->save_dirty = true;
    return true;
  }
  if (pa >= 0x04000000u && pa < 0x04002000u) {
    uint8_t *m = (pa & 0x1000) ? n->imem : n->dmem;
    wr16be(m + (pa & 0xFFE), v);
    return true;
  }
  return bus_write32(n, pa & ~3u, (uint32_t)v << (16 - 8 * (pa & 2)));
}

bool bus_write8(N64 *n, uint32_t pa, uint8_t v) {
  if (pa < RDRAM_SIZE) { n->rdram[pa] = v; return true; }
  if (pa >= 0x08000000u && pa < 0x08000000u + sizeof n->sram) {
    n->sram[pa - 0x08000000u] = v;
    n->save_dirty = true;
    return true;
  }
  if (pa >= 0x04000000u && pa < 0x04002000u) {
    uint8_t *m = (pa & 0x1000) ? n->imem : n->dmem;
    m[pa & 0xFFF] = v;
    return true;
  }
  if (pa >= 0x1FC007C0u && pa < 0x1FC00800u) {
    n->pif_ram[pa - 0x1FC007C0u] = v;
    if (pa == 0x1FC007FFu) pif_process(n);
    return true;
  }
  return bus_write32(n, pa & ~3u, (uint32_t)v << (24 - 8 * (pa & 3)));
}

/* ------------------------------------------------------------------ */
/* Video timing                                                         */

void vi_tick(N64 *n, uint32_t cycles) {
  (void)cycles;
  Vi *v = &n->vi;
  uint32_t lines = v->v_sync ? v->v_sync + 1 : 525;  /* half-lines per frame */
  v->v_current += 2;
  if (v->v_current >= lines) {
    v->field ^= 1;
    v->v_current = 0;
  }
  if ((v->v_current & ~1u) == (v->v_intr & ~1u)) n64_raise_mi(n, MI_INTR_VI);
}

/* ------------------------------------------------------------------ */
/* Audio: play the AI DMA buffers and resample to 44.1 kHz              */

void ai_tick(N64 *n, uint32_t cycles) {
  Ai *a = &n->ai;
  double out = n->audio_frac + (double)cycles * AUDIO_RATE / CPU_HZ;
  uint32_t frames = (uint32_t)out;
  n->audio_frac = out - frames;

  double in_rate = (double)(n->pal ? VI_PAL_CLOCK : VI_NTSC_CLOCK) / (a->dacrate + 1);
  double step = in_rate / AUDIO_RATE;

  for (uint32_t i = 0; i < frames; i++) {
    int16_t l = a->last_l, r = a->last_r;
    if (a->count) {
      uint32_t samples = a->len[0] / 4;
      uint32_t idx = (uint32_t)a->pos;
      if (idx < samples) {
        uint32_t addr = (a->dram_addr[0] + idx * 4) & (RDRAM_SIZE - 4);
        l = (int16_t)rd16be(n->rdram + addr);
        r = (int16_t)rd16be(n->rdram + addr + 2);
      }
      a->pos += step;
      if ((uint32_t)a->pos >= samples) {
        /* Buffer done: the queued one starts, and the CPU may queue another. */
        a->dram_addr[0] = a->dram_addr[1];
        a->len[0] = a->len[1];
        a->count--;
        a->pos = 0;
        n64_raise_mi(n, MI_INTR_AI);
      }
      a->last_l = l;
      a->last_r = r;
    }
    if (n->audio_frames < 8192) {
      n->audio_buf[n->audio_frames * 2] = l;
      n->audio_buf[n->audio_frames * 2 + 1] = r;
      n->audio_frames++;
    }
  }
}
