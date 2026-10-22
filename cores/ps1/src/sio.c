/* Controller and memory card port (SIO0, 0x1F801040).
 *
 * The CPU selects a port (JOY_CTRL bit 1, port in bit 13) and exchanges
 * bytes through JOY_DATA. A digital pad answers 01 42 00 00 00 with
 * FF 41 5A <buttons low> <buttons high> (buttons active low), pulling /ACK
 * after every byte but the last; /ACK raises IRQ7 when JOY_CTRL bit 12 is
 * set. Memory cards (stage 4) answer 0x81; without one nothing acks.
 *
 * JOY_STAT: bit 0 TX ready, 1 RX has data, 2 TX done, 7 /ACK low,
 * 9 IRQ. JOY_CTRL: bit 1 select, 4 acknowledge IRQ, 6 reset, 12 ACK IRQ
 * enable, 13 port 2. */
#include "ps1.h"

#define ACK_DELAY 1500   /* cycles from a byte to its /ACK (~8 bits at 250 kHz plus the pad's delay) */

static uint8_t exchange(PS1 *p, uint8_t tx, bool *ack) {
  Sio *s = &p->sio;
  int port = (s->ctrl >> 13) & 1;
  Pad *pad = &p->pad[port];
  *ack = false;
  if (s->seq == 0) {
    if (tx == 0x01 && pad->connected) { s->device = 1; s->seq = 1; *ack = true; return 0xFF; }
    s->device = 0;   /* 0x81 (memory card) or nothing attached */
    return 0xFF;
  }
  if (s->device != 1) return 0xFF;
  uint16_t b = (uint16_t)~pad->buttons;
  switch (s->seq++) {
    case 1: *ack = tx == 0x42; return 0x41;     /* digital pad ID */
    case 2: *ack = true; return 0x5A;
    case 3: *ack = true; return (uint8_t)b;
    case 4: s->seq = 0; return (uint8_t)(b >> 8);   /* last byte: no /ACK */
    default: s->seq = 0; return 0xFF;
  }
}

uint32_t sio_read(PS1 *p, uint32_t off, int size) {
  Sio *s = &p->sio;
  switch (off) {
    case 0x0: {   /* JOY_DATA */
      uint32_t v = s->rx_full ? s->rx : 0xFF;
      s->rx_full = false;
      return size == 1 ? v : v | (uint32_t)v << 8 | (uint32_t)v << 16 | (uint32_t)v << 24;
    }
    case 0x4: {   /* JOY_STAT */
      uint32_t st = 0x5 | (s->rx_full ? 2u : 0u) | (s->stat & 0x280u);
      return st;
    }
    case 0x8: return s->mode;
    case 0xA: return s->ctrl;
    case 0xE: return s->baud;
    default: return 0;
  }
}

void sio_write(PS1 *p, uint32_t off, int size, uint32_t v) {
  Sio *s = &p->sio;
  (void)size;
  switch (off) {
    case 0x0: {   /* JOY_DATA: send a byte */
      if (!(s->ctrl & 2)) { s->rx = 0xFF; s->rx_full = true; break; }   /* nothing selected */
      bool ack;
      s->rx = exchange(p, (uint8_t)v, &ack);
      s->rx_full = true;
      if (ack) s->ack_at = ((uint32_t)p->cpu.cycles + ACK_DELAY) | 1;   /* never 0 (= none) */
      break;
    }
    case 0x8: s->mode = (uint16_t)v; break;
    case 0xA:
      if (v & 0x40) {   /* reset */
        s->stat = 0;
        s->seq = 0;
        s->rx_full = false;
        s->ack_at = 0;
        s->ctrl = 0;
        break;
      }
      if (v & 0x10) s->stat &= ~0x200u;           /* acknowledge IRQ */
      if (!(v & 2)) { s->seq = 0; s->device = 0; } /* deselected: next exchange starts over */
      s->ctrl = (uint16_t)(v & ~0x50u);
      break;
    case 0xE: s->baud = (uint16_t)v; break;
    default: break;
  }
}

void sio_tick(PS1 *p) {
  Sio *s = &p->sio;
  if (s->ack_at && (uint32_t)p->cpu.cycles - s->ack_at < 0x80000000u) {
    s->ack_at = 0;
    s->stat |= 0x80;            /* /ACK low */
    if (s->ctrl & 0x1000) {
      s->stat |= 0x200;
      irq_raise(p, IRQ_PAD);
    }
  } else if (!s->ack_at) {
    s->stat &= ~0x80u;
  }
}
