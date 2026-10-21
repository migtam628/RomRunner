/*
 * PIF: boot (high-level, no PIF ROM or IPL3 needed), the joybus protocol for
 * controllers and EEPROM, CIC detection and save-type guessing.
 */
#include <string.h>

#include "n64.h"

/* ------------------------------------------------------------------ */
/* CIC detection: recompute the boot checksum with each chip's seed and see
 * which one matches the header (bytes 0x10..0x17).                      */

static uint32_t rol32(uint32_t v, uint32_t s) { s &= 31; return s ? (v << s) | (v >> (32 - s)) : v; }

static void boot_checksum(const uint8_t *rom, uint32_t size, uint32_t cic, uint32_t *c1, uint32_t *c2) {
  uint32_t seed = cic == 6103 ? 0xA3886759u : cic == 6105 ? 0xDF26F436u : cic == 6106 ? 0x1FEA617Au : 0xF8CA4DDCu;
  uint32_t t1 = seed, t2 = seed, t3 = seed, t4 = seed, t5 = seed, t6 = seed;
  for (uint32_t i = 0x1000; i < 0x101000; i += 4) {
    uint32_t d = i + 3 < size ? rd32be(rom + i) : 0;
    if (t6 + d < t6) t4++;
    t6 += d;
    t3 ^= d;
    uint32_t r = rol32(d, d & 0x1F);
    t5 += r;
    if (t2 > d) t2 ^= r;
    else t2 ^= t6 ^ d;
    if (cic == 6105) t1 += rd32be(rom + 0x0750 + (i & 0xFF)) ^ d;
    else t1 += t5 ^ d;
  }
  if (cic == 6103) { *c1 = (t6 ^ t4) + t3; *c2 = (t5 ^ t2) + t1; }
  else if (cic == 6106) { *c1 = t6 * t4 + t3; *c2 = t5 * t2 + t1; }
  else { *c1 = t6 ^ t4 ^ t3; *c2 = t5 ^ t2 ^ t1; }
}

uint32_t cic_detect(const uint8_t *rom, uint32_t size) {
  if (size < 0x1000) return 6102;
  uint32_t h1 = rd32be(rom + 0x10), h2 = rd32be(rom + 0x14);
  static const uint32_t chips[] = {6102, 6103, 6105, 6106};
  for (unsigned i = 0; i < sizeof chips / sizeof chips[0]; i++) {
    uint32_t c1, c2;
    boot_checksum(rom, size, chips[i], &c1, &c2);
    if (c1 == h1 && c2 == h2) return chips[i];
  }
  return 6102;
}

/* Public for the ROM tools in test/. */
void rrn64_boot_checksum(const uint8_t *rom, uint32_t size, uint32_t cic, uint32_t *c1, uint32_t *c2) {
  boot_checksum(rom, size, cic, c1, c2);
}

/* ------------------------------------------------------------------ */
/* Save type: a short list for now (stage 4 brings a full database).    */

SaveType save_guess(const uint8_t *rom, uint32_t size) {
  if (size < 0x40) return SAVE_NONE;
  char id[4] = {(char)rom[0x3B], (char)rom[0x3C], (char)rom[0x3D], 0};
  static const char *sram[] = {"ZL", "AL", "FZ", "MF", "KG", "VP", 0};    /* by 2-letter game code */
  static const char *flash[] = {"ZS", "MQ", "PN", "CC", 0};
  static const char *eep16[] = {"B7", "YS", "FU", "PD", "DO", "MX", 0};
  const char *code = id + 1;
  for (int i = 0; sram[i]; i++) if (!strncmp(code, sram[i], 2)) return SAVE_SRAM;
  for (int i = 0; flash[i]; i++) if (!strncmp(code, flash[i], 2)) return SAVE_FLASH;
  for (int i = 0; eep16[i]; i++) if (!strncmp(code, eep16[i], 2)) return SAVE_EEPROM_16K;
  return SAVE_EEPROM_4K;
}

/* ------------------------------------------------------------------ */
/* High-level boot: do what the PIF ROM and the cartridge's IPL3 would.  */

void pif_hle_boot(N64 *n) {
  Cpu *c = &n->cpu;
  const uint8_t *rom = n->rom;
  uint32_t seed = n->cic == 6103 ? 0x78 : n->cic == 6105 ? 0x91 : n->cic == 6106 ? 0x85 : 0x3F;

  /* IPL3 runs from DMEM with the ROM header in place. */
  memcpy(n->dmem, rom, n->rom_size < 0x1000 ? n->rom_size : 0x1000);

  uint32_t entry = rd32be(rom + 8);
  if (n->cic == 6103) entry -= 0x100000;
  if (n->cic == 6106) entry -= 0x200000;
  uint32_t dst = entry & 0x1FFFFFFF;
  for (uint32_t i = 0; i < 0x100000 && 0x1000 + i < n->rom_size; i++) {
    n->rdram[(dst + i) & (RDRAM_SIZE - 1)] = rom[0x1000 + i];
  }

  /* RDRAM size, where libultra reads it. */
  wr32be(n->rdram + (n->cic == 6105 ? 0x3F0 : 0x318), RDRAM_SIZE);

  c->gpr[11] = 0xFFFFFFFFA4000040ull;  /* t3 */
  c->gpr[19] = 0;                      /* s3: ROM type (cartridge) */
  c->gpr[20] = n->pal ? 0 : 1;         /* s4: TV type */
  c->gpr[21] = 0;                      /* s5: reset type (cold) */
  c->gpr[22] = seed;                   /* s6: CIC seed */
  c->gpr[23] = 0;                      /* s7: version */
  c->gpr[29] = 0xFFFFFFFFA4001FF0ull;  /* sp */
  c->gpr[31] = 0xFFFFFFFFA4001550ull;  /* ra */

  c->cp0[CP0_STATUS] = 0x34000000u;    /* CU0, CU1, FR */
  c->cp0[CP0_RANDOM] = 31;
  c->cp0[CP0_COUNT] = 0x5000;
  c->cp0[CP0_CAUSE] = 0x0000005Cu;
  c->cp0[CP0_CONTEXT] = 0x007FFFF0u;
  c->cp0[CP0_EPC] = c->cp0[CP0_ERROREPC] = 0xFFFFFFFFFFFFFFFFull;
  c->cp0[CP0_BADVADDR] = 0xFFFFFFFFFFFFFFFFull;

  c->pc = sext32(entry);
  c->next_pc = c->pc + 4;

  n->sp.status = 1;          /* RSP halted */
  n->dp.status = 0x80;       /* RDP ready */
  n->ri[0] = 0x0E;           /* mode */
  n->ri[1] = 0x40;           /* config */
  n->ri[3] = 0x14;           /* select */
  n->ri[4] = 0x00063634;     /* refresh */
  n->mi.mode = 0;
}

/* ------------------------------------------------------------------ */
/* Joybus                                                               */

static bool controller_command(N64 *n, int ch, const uint8_t *cmd, int tx, uint8_t *resp, int rx) {
  if (ch > 3 || !n->pad_connected[ch] || tx < 1) return false;
  switch (cmd[0]) {
    case 0x00:
    case 0xFF: /* info: standard controller, nothing in the pak slot */
      if (rx >= 3) { resp[0] = 0x05; resp[1] = 0x00; resp[2] = 0x02; }
      return true;
    case 0x01: /* buttons and stick */
      if (rx >= 4) {
        resp[0] = (uint8_t)(n->pad[ch].buttons >> 8);
        resp[1] = (uint8_t)n->pad[ch].buttons;
        resp[2] = (uint8_t)n->pad[ch].x;
        resp[3] = (uint8_t)n->pad[ch].y;
      }
      return true;
    default:
      return false; /* pak access arrives with the controller pak (stage 4) */
  }
}

static bool eeprom_command(N64 *n, const uint8_t *cmd, int tx, uint8_t *resp, int rx) {
  if (n->save_type != SAVE_EEPROM_4K && n->save_type != SAVE_EEPROM_16K) return false;
  uint32_t size = n->save_type == SAVE_EEPROM_16K ? 2048 : 512;
  switch (cmd[0]) {
    case 0x00:
    case 0xFF:
      if (rx >= 3) { resp[0] = 0x00; resp[1] = size == 2048 ? 0xC0 : 0x80; resp[2] = 0x00; }
      return true;
    case 0x04: { /* read 8 bytes */
      if (tx < 2) return false;
      uint32_t a = (uint32_t)cmd[1] * 8;
      for (int i = 0; i < rx && i < 8; i++) resp[i] = a + i < size ? n->eeprom[a + i] : 0;
      return true;
    }
    case 0x05: { /* write 8 bytes */
      if (tx < 10) return false;
      uint32_t a = (uint32_t)cmd[1] * 8;
      for (int i = 0; i < 8; i++) if (a + i < size) n->eeprom[a + i] = cmd[2 + i];
      n->save_dirty = true;
      if (rx >= 1) resp[0] = 0;
      return true;
    }
    default:
      return false;
  }
}

void pif_process(N64 *n) {
  uint8_t *ram = n->pif_ram;
  uint8_t ctrl = ram[63];
  if (ctrl & 0x01) {
    int ch = 0, i = 0;
    while (i < 63 && ch < 6) {
      uint8_t t = ram[i];
      if (t == 0xFE) break;                  /* end of commands */
      if (t == 0xFF || t == 0xFD) { i++; continue; } /* padding / channel reset */
      if (t == 0x00) { ch++; i++; continue; }        /* skip this channel */
      int tx = t & 0x3F;
      if (i + 1 >= 63) break;
      uint8_t rbyte = ram[i + 1];
      if (rbyte == 0xFE) break;
      int rx = rbyte & 0x3F;
      if (i + 2 + tx + rx > 63) break;
      const uint8_t *cmd = &ram[i + 2];
      uint8_t *resp = &ram[i + 2 + tx];
      bool ok = ch < 4 ? controller_command(n, ch, cmd, tx, resp, rx) : ch == 4 ? eeprom_command(n, cmd, tx, resp, rx) : false;
      if (!ok) ram[i + 1] |= 0x80;           /* no device on this channel */
      i += 2 + tx + rx;
      ch++;
    }
  }
  /* Command bits are acknowledged by clearing them. 0x08 ends the boot
   * handshake; 0x20/0x02 (CIC challenge) are not needed with this boot. */
  ram[63] = (uint8_t)(ctrl & ~(0x01 | 0x02 | 0x08 | 0x20 | 0x30));
}
