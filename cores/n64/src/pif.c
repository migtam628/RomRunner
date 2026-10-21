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

  /* The boot values libultra's osInitialize reads (osTvType, osRomType,
   * osRomBase, osResetType, osCicId, osVersion, osMemSize). Games check
   * osTvType against their region: Zelda refuses to run on "PAL" with
   * "This game is not designed for use on this system". */
  wr32be(n->rdram + 0x300, n->pal ? 0 : 1);
  wr32be(n->rdram + 0x304, 0);                 /* cartridge */
  wr32be(n->rdram + 0x308, 0xB0000000u);
  wr32be(n->rdram + 0x30C, 0);                 /* cold reset */
  wr32be(n->rdram + 0x310, seed);
  wr32be(n->rdram + 0x314, 0);
  wr32be(n->rdram + 0x318, RDRAM_SIZE);
  if (n->cic == 6105) wr32be(n->rdram + 0x3F0, RDRAM_SIZE);

  c->gpr[11] = 0xFFFFFFFFA4000040ull;  /* t3 */
  c->gpr[19] = 0;                      /* s3: ROM type (cartridge) */
  c->gpr[20] = n->pal ? 0 : 1;         /* s4: TV type */
  c->gpr[21] = 0;                      /* s5: reset type (cold) */
  c->gpr[22] = seed;                   /* s6: CIC seed */
  c->gpr[23] = 0;                      /* s7: version */
  c->gpr[29] = 0xFFFFFFFFA4001FF0ull;  /* sp */
  c->gpr[31] = 0xFFFFFFFFA4001550ull;  /* ra */

  c->cp0[CP0_STATUS] = 0x241000E0u;    /* as IPL3 leaves it: CU1, FR, KX/SX/UX */
  c->cp0[CP0_RANDOM] = 31;
  c->cp0[CP0_COUNT] = 0x5000;
  c->cp0[CP0_CAUSE] = 0xB000007Cu;    /* what the console shows after boot */
  c->cp0[CP0_CONTEXT] = 0x007FFFF0u;
  c->cp0[CP0_EPC] = c->cp0[CP0_ERROREPC] = 0xFFFFFFFFFFFFFFFFull;
  c->cp0[CP0_BADVADDR] = 0xFFFFFFFFFFFFFFFFull;

  /* The TLB powers up with junk; make sure no entry matches a real address. */
  for (int i = 0; i < 32; i++) {
    c->tlb[i].mask = 0;
    c->tlb[i].hi = 0xFFFFFFFF80000000ull + (uint64_t)i * 0x2000;  /* KSEG0: never translated */
    c->tlb[i].lo0 = c->tlb[i].lo1 = 0;
    c->tlb[i].global = false;
  }

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

/* CIC-NUS-6105 challenge (PIF command bit 0x02). Games on this chip
 * (Majora's Mask, Banjo-Tooie, Ocarina on some carts, ...) write 15 bytes
 * of challenge to PIF RAM 0x30..0x3E and refuse to run unless the answer
 * matches what the chip computes: nibble by nibble, each answer nibble is
 * key + 5 * challenge, and the answer picks the next key from one of two
 * tables (described on n64brew, "CIC-NUS-6105"). */
static void cic_6105_challenge(uint8_t *ram) {
  static const uint8_t lut0[16] = {0x4, 0x7, 0xA, 0x7, 0xE, 0x5, 0xE, 0x1, 0xC, 0xF, 0x8, 0xF, 0x6, 0x3, 0x6, 0x9};
  static const uint8_t lut1[16] = {0x4, 0x1, 0xA, 0x7, 0xE, 0x5, 0xE, 0x1, 0xC, 0x9, 0x8, 0x5, 0x6, 0x3, 0xC, 0x9};
  uint8_t chl[30], rsp[30];
  for (int i = 0; i < 15; i++) {
    chl[i * 2] = (uint8_t)(ram[0x30 + i] >> 4);
    chl[i * 2 + 1] = (uint8_t)(ram[0x30 + i] & 0xF);
  }
  uint8_t key = 0xB;
  const uint8_t *lut = lut0;
  for (int i = 0; i < 30; i++) {
    uint8_t r = (uint8_t)((key + 5 * chl[i]) & 0xF);
    rsp[i] = r;
    key = lut[r];
    int sgn = (r >> 3) & 1;
    int mag = (sgn ? ~r : r) & 7;
    int mod = (mag % 3 == 1) ? sgn : 1 - sgn;
    if (lut == lut1 && (r == 0x1 || r == 0x9)) mod = 1;
    if (lut == lut1 && (r == 0xB || r == 0xE)) mod = 0;
    lut = mod ? lut1 : lut0;
  }
  ram[0x2E] = ram[0x2F] = 0;
  for (int i = 0; i < 15; i++) ram[0x30 + i] = (uint8_t)(rsp[i * 2] << 4 | rsp[i * 2 + 1]);
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
  if (ctrl & 0x02) cic_6105_challenge(ram);
  /* Command bits are acknowledged by clearing them. 0x08 ends the boot
   * handshake; 0x20 (checksum verify) isn't needed with the HLE boot. */
  ram[63] = (uint8_t)(ctrl & ~(0x01 | 0x02 | 0x08 | 0x20 | 0x30));
}
