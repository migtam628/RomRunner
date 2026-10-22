/* With no BIOS image, a handful of BIOS calls (A0/B0/C0 vectors, function
 * number in $t1) are answered directly: console output, so test programs
 * can report, and harmless defaults for the rest. Games need a real BIOS
 * (or OpenBIOS). */
#include <stdio.h>
#include <string.h>

#include "ps1.h"

static uint8_t ram8(PS1 *p, uint32_t va) { return p->ram[va & (RAM_SIZE - 1)]; }
static uint32_t ram32(PS1 *p, uint32_t va) { return rd32le(p->ram + (va & (RAM_SIZE - 4))); }

static void puts_ram(PS1 *p, uint32_t s) {
  for (int i = 0; i < 4096; i++) {
    char ch = (char)ram8(p, s + (uint32_t)i);
    if (!ch) break;
    ps1_tty_putc(p, ch);
  }
}

/* printf with %d %i %u %x %X %c %s %p and widths/zero padding. */
static void printf_ram(PS1 *p, uint32_t fmt) {
  Cpu *c = &p->cpu;
  int argi = 1;
  uint32_t args[16];
  args[0] = c->gpr[4]; args[1] = c->gpr[5]; args[2] = c->gpr[6]; args[3] = c->gpr[7];
  for (int i = 4; i < 16; i++) args[i] = ram32(p, c->gpr[29] + 16 + (uint32_t)(i - 4) * 4);
  for (int i = 0; i < 4096; i++) {
    char ch = (char)ram8(p, fmt + (uint32_t)i);
    if (!ch) break;
    if (ch != '%') { ps1_tty_putc(p, ch); continue; }
    /* The format comes from the emulated program: keep only flags, width
     * and precision digits (no '*', no length modifiers) before handing it
     * to the host's snprintf with a single 32-bit argument. */
    char spec[16] = "%";
    int n = 1;
    for (;;) {
      ch = (char)ram8(p, fmt + (uint32_t)++i);
      if (!ch) return;
      if (strchr("diuxXcsp%", ch)) break;
      if (n < 10 && strchr("0123456789-+ #.", ch)) spec[n++] = ch;
    }
    spec[n++] = ch == 'p' ? 'x' : ch;
    spec[n] = 0;
    char out[256];
    uint32_t a = ch == '%' ? 0 : args[argi < 16 ? argi++ : 15];
    if (ch == '%') { ps1_tty_putc(p, '%'); continue; }
    if (ch == 's') {
      puts_ram(p, a);
      continue;
    }
    if (ch == 'd' || ch == 'i') snprintf(out, sizeof out, spec, (int)a);
    else snprintf(out, sizeof out, spec, a);
    for (char *q = out; *q; q++) ps1_tty_putc(p, *q);
  }
}

void hle_bios_call(PS1 *p, uint32_t vector) {
  Cpu *c = &p->cpu;
  uint32_t fn = c->gpr[9];
  uint32_t a0 = c->gpr[4];
  c->gpr[2] = 0;
  if (vector == 0xA0) {
    if (fn == 0x3C) ps1_tty_putc(p, (char)a0);       /* putchar */
    else if (fn == 0x3E) { puts_ram(p, a0); ps1_tty_putc(p, '\n'); }   /* puts */
    else if (fn == 0x3F) printf_ram(p, a0);          /* printf */
  } else if (vector == 0xB0) {
    if (fn == 0x3D) ps1_tty_putc(p, (char)a0);       /* putchar */
    else if (fn == 0x3F) { puts_ram(p, a0); ps1_tty_putc(p, '\n'); }   /* puts */
  }
  /* return to the caller */
  c->pc = c->gpr[31];
  c->next_pc = c->pc + 4;
}
