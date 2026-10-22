/* Root counters (timers 0-2).
 *
 * Mode: bit 0 sync enable, 1-2 sync mode, 3 reset at target (else after
 * 0xFFFF), 4 IRQ at target, 5 IRQ at 0xFFFF, 6 repeat (else one-shot),
 * 7 toggle IRQ bit (else pulse), 8-9 clock source, 10 IRQ line (0 =
 * requested), 11 reached target, 12 reached 0xFFFF (11 and 12 clear when
 * read). Writing the mode resets the counter.
 *
 * Sources: timer 0 system clock or dot clock, timer 1 system clock or
 * hblank, timer 2 system clock or system clock / 8. */
#include "ps1.h"

static void hit(PS1 *p, int i, bool target) {
  Timer *t = &p->tmr[i];
  t->mode |= target ? 0x800u : 0x1000u;
  bool want = target ? (t->mode & 0x10) : (t->mode & 0x20);
  if (!want) return;
  bool repeat = t->mode & 0x40;
  if (!repeat && t->irq_done) return;
  t->irq_done = true;
  if (t->mode & 0x80) {
    t->mode ^= 0x400u;                 /* toggle: IRQ on each 1 -> 0 */
    if (t->mode & 0x400u) return;
  } else {
    t->mode &= ~0x400u;                /* pulse: low briefly */
  }
  irq_raise(p, IRQ_TMR0 << i);
  if (!(t->mode & 0x80)) t->mode |= 0x400u;
}

static void advance(PS1 *p, int i, uint32_t ticks) {
  Timer *t = &p->tmr[i];
  while (ticks) {
    uint32_t target = t->target & 0xFFFF;
    uint32_t c = t->count;
    /* Distance to the next event (target or wrap). */
    uint32_t to_target = target > c ? target - c : 0x10000;
    uint32_t to_wrap = 0x10000 - c;
    uint32_t step = ticks;
    if (to_target < step) step = to_target;
    if (to_wrap < step) step = to_wrap;
    c += step;
    ticks -= step;
    if (c == target) {
      hit(p, i, true);
      if (t->mode & 0x08) c = 0;
    }
    if (c >= 0x10000) {
      c &= 0xFFFF;
      if (c == 0) hit(p, i, false);
    } else if (c == 0xFFFF && !(t->mode & 0x08)) {
      /* counted to 0xFFFF; the wrap comes next tick */
    }
    t->count = c;
  }
}

/* Timer 2 in sync modes 0 and 3 stops counting. */
static bool paused(const Timer *t, int i) {
  if (!(t->mode & 1)) return false;
  uint32_t sync = (t->mode >> 1) & 3;
  if (i == 2) return sync == 0 || sync == 3;
  return false;
}

/* GPU dot clock per CPU cycle, from the horizontal resolution. */
static uint32_t dot_divider(const PS1 *p) {
  if (p->gpu.hres2) return 7;
  static const uint32_t div[4] = {10, 8, 5, 4};
  return div[p->gpu.hres & 3];
}

void timers_tick(PS1 *p, uint32_t cycles) {
  for (int i = 0; i < 3; i++) {
    Timer *t = &p->tmr[i];
    if (paused(t, i)) continue;
    uint32_t src = (t->mode >> 8) & 3;
    uint32_t ticks;
    if (i == 0 && (src & 1)) {
      /* dot clock: GPU clock (CPU * 11/7) / divider */
      uint32_t num = t->frac + cycles * 11;
      uint32_t den = 7 * dot_divider(p);
      ticks = num / den;
      t->frac = num % den;
    } else if (i == 1 && (src & 1)) {
      continue;   /* counts hblanks: timers_hblank */
    } else if (i == 2 && (src & 2)) {
      uint32_t num = t->frac + cycles;
      ticks = num / 8;
      t->frac = num % 8;
    } else {
      ticks = cycles;
    }
    if (ticks) advance(p, i, ticks);
  }
}

void timers_hblank(PS1 *p) {
  Timer *t = &p->tmr[1];
  if (((t->mode >> 8) & 1) && !paused(t, 1)) advance(p, 1, 1);
  /* Timer 0 sync mode 1/2: reset at hblank. */
  Timer *t0 = &p->tmr[0];
  if ((t0->mode & 1) && (((t0->mode >> 1) & 3) == 1 || ((t0->mode >> 1) & 3) == 2)) t0->count = 0;
}

void timers_vblank(PS1 *p, bool start) {
  Timer *t = &p->tmr[1];
  if (start && (t->mode & 1) && (((t->mode >> 1) & 3) == 1 || ((t->mode >> 1) & 3) == 2)) t->count = 0;
}

uint32_t timer_read(PS1 *p, uint32_t off) {
  uint32_t i = off >> 4;
  if (i > 2) return 0;
  Timer *t = &p->tmr[i];
  switch (off & 0xF) {
    case 0: return t->count & 0xFFFF;
    case 4: {
      uint32_t m = t->mode;
      t->mode &= ~0x1800u;   /* reached-target / reached-FFFF clear on read */
      return m;
    }
    case 8: return t->target & 0xFFFF;
    default: return 0;
  }
}

void timer_write(PS1 *p, uint32_t off, uint32_t v) {
  uint32_t i = off >> 4;
  if (i > 2) return;
  Timer *t = &p->tmr[i];
  switch (off & 0xF) {
    case 0: t->count = v & 0xFFFF; break;
    case 4:
      t->mode = (v & 0x3FF) | 0x400u;
      t->count = 0;
      t->frac = 0;
      t->irq_done = false;
      break;
    case 8: t->target = v & 0xFFFF; break;
    default: break;
  }
}
