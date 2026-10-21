/* System glue: ROM loading, reset, the per-frame scheduler and the MI
 * interrupt line. */
#include <stdlib.h>
#include <string.h>

#include "n64.h"

void n64_update_interrupts(N64 *n) {
  if (n->mi.intr & n->mi.mask) n->cpu.cp0[CP0_CAUSE] |= 1ull << 10;   /* IP2 */
  else n->cpu.cp0[CP0_CAUSE] &= ~(1ull << 10);
  n->cpu.irq_check = true;
}

void n64_raise_mi(N64 *n, uint32_t bit) {
  n->mi.intr |= bit;
  n64_update_interrupts(n);
}

void n64_clear_mi(N64 *n, uint32_t bit) {
  n->mi.intr &= ~bit;
  n64_update_interrupts(n);
}

/* Accepts .z64 (big-endian), .v64 (16-bit swapped) and .n64 (32-bit
 * little-endian); keeps a big-endian copy. */
bool n64_load_rom(N64 *n, const uint8_t *data, size_t size, uint8_t **owned_rom) {
  if (size < 0x1000 || size > 0x10000000) return false;
  uint8_t *rom = (uint8_t *)malloc(size);
  if (!rom) return false;
  uint32_t magic = rd32be(data);
  if (magic == 0x80371240u) {
    memcpy(rom, data, size);
  } else if (magic == 0x37804012u) {
    for (size_t i = 0; i + 1 < size; i += 2) { rom[i] = data[i + 1]; rom[i + 1] = data[i]; }
  } else if (magic == 0x40123780u) {
    for (size_t i = 0; i + 3 < size; i += 4) {
      rom[i] = data[i + 3]; rom[i + 1] = data[i + 2]; rom[i + 2] = data[i + 1]; rom[i + 3] = data[i];
    }
  } else {
    free(rom);
    return false;
  }
  n->rom = rom;
  n->rom_size = (uint32_t)size;
  *owned_rom = rom;

  char region = (char)rom[0x3E];
  n->pal = region == 'D' || region == 'F' || region == 'I' || region == 'P' || region == 'S' ||
           region == 'U' || region == 'X' || region == 'Y';
  n->cic = cic_detect(rom, n->rom_size);
  n->save_type = save_guess(rom, n->rom_size);
  /* Blank FlashRAM reads as erased; the frontend loads a save over this. */
  if (n->save_type == SAVE_FLASH) flash_erase_all(n);
  return true;
}

void n64_reset(N64 *n) {
  rdp_thread_wait(n);   /* nothing may be drawing while the machine is cleared */
  /* Keep the cartridge, saves, controllers; clear the machine. */
  const uint8_t *rom = n->rom;
  uint32_t rom_size = n->rom_size;
  uint8_t eeprom[sizeof n->eeprom], sram[sizeof n->sram];
  memcpy(eeprom, n->eeprom, sizeof eeprom);
  memcpy(sram, n->sram, sizeof sram);
  SaveType st = n->save_type;
  bool pal = n->pal;
  uint32_t cic = n->cic;
  bool conn[4];
  memcpy(conn, n->pad_connected, sizeof conn);

  memset(n, 0, sizeof *n);
  n->rom = rom;
  n->rom_size = rom_size;
  memcpy(n->eeprom, eeprom, sizeof eeprom);
  memcpy(n->sram, sram, sizeof sram);
  n->save_type = st;
  n->pal = pal;
  n->cic = cic;
  memcpy(n->pad_connected, conn, sizeof conn);

  flash_reset(n);
  cpu_reset(n);
  pif_hle_boot(n);
}

void n64_run_frame(N64 *n) {
  uint32_t frame_cycles = CPU_HZ / (n->pal ? 50 : 60);
  uint32_t half_lines = n->vi.v_sync ? n->vi.v_sync + 1 : (n->pal ? 625 : 525);
  uint32_t lines = half_lines / 2;
  if (lines == 0) lines = 1;
  uint32_t per_line = frame_cycles / lines;
  for (uint32_t i = 0; i < lines; i++) {
    /* CPU (93.75 MHz) and RSP (62.5 MHz) take turns in short slices so
     * that polling loops on either side see each other's progress. */
    for (uint32_t done = 0; done < per_line; done += 120) {
      uint32_t slice = per_line - done < 120 ? per_line - done : 120;
      cpu_run(n, slice);
      rsp_run(n, slice * 2 / 3);
      rdp_thread_poll(n);
    }
    ai_tick(n, per_line);
    vi_tick(n, per_line);
  }
}
