/* FlashRAM (128 KB, cartridge domain 2 at 0x08000000).
 *
 * Unlike SRAM, FlashRAM is driven by commands written to 0x08010000:
 *
 *   0xF0000000          read mode: PI DMA from 0x08000000 reads the array
 *   0xE1000000          status mode: reads return the 64-bit status/ID
 *   0xB4000000          write mode: PI DMA to 0x08000000 fills the
 *                       128-byte page buffer
 *   0xA5000000 | page   program the page buffer into page
 *   0x4B000000 | page   select the 16 KB sector holding page for erasing
 *   0x3C000000          select the whole chip for erasing
 *   0x78000000          erase mode (arms the erase)
 *   0xD2000000          execute the armed erase
 *
 * Erased bytes read 0xFF. In read mode the PI address counts 16-bit
 * words, so array offset = (address - 0x08000000) * 2. The ID reported is
 * a Macronix MX29L1100, which every FlashRAM game accepts.
 * (Behaviour from n64brew, "Flash RAM".) */
#include <string.h>

#include "n64.h"

#define FLASH_ID 0x1111800100C2001Eull
#define FLASH_SIZE 0x20000u
#define PAGE 128u
#define SECTOR (128u * PAGE)

static void set_status(N64 *n, uint32_t hi) { n->flash.status = (uint64_t)hi << 32 | (FLASH_ID & 0xFFFFFFFFu); }

void flash_reset(N64 *n) {
  memset(&n->flash, 0, sizeof n->flash);
  n->flash.mode = FLASH_READ;
  set_status(n, 0x11118001u);
}

void flash_erase_all(N64 *n) { memset(n->sram, 0xFF, FLASH_SIZE); }

void flash_command(N64 *n, uint32_t v) {
  Flash *f = &n->flash;
  switch (v >> 24) {
    case 0xF0:
      f->mode = FLASH_READ;
      set_status(n, 0x11118004u);
      break;
    case 0xE1:
      f->mode = FLASH_STATUS;
      set_status(n, 0x11118001u);
      break;
    case 0xB4:
      f->mode = FLASH_WRITE;
      break;
    case 0xA5:
      f->offset = (v & 0xFFFF) * PAGE % FLASH_SIZE;
      if (f->mode == FLASH_WRITE) {
        memcpy(n->sram + f->offset, f->page, PAGE);
        n->save_dirty = true;
      }
      set_status(n, 0x11118004u);
      break;
    case 0x4B:
      f->erase_all = false;
      f->offset = (v & 0xFF80) * PAGE % FLASH_SIZE;   /* page rounded down to its sector */
      f->mode = FLASH_ERASE;
      break;
    case 0x3C:
      f->erase_all = true;
      f->mode = FLASH_ERASE;
      break;
    case 0x78:
      f->mode = FLASH_ERASE;
      set_status(n, 0x11118008u);
      break;
    case 0xD2:
      if (f->mode == FLASH_ERASE) {
        if (f->erase_all) flash_erase_all(n);
        else memset(n->sram + f->offset, 0xFF, SECTOR);
        n->save_dirty = true;
      }
      f->mode = FLASH_STATUS;
      break;
    default:
      break;
  }
}

uint32_t flash_read32(N64 *n) { return (uint32_t)(n->flash.status >> 32); }

/* One byte of a PI DMA from cartridge domain 2; i counts bytes from the
 * start of the transfer. */
uint8_t flash_dma_byte(N64 *n, uint32_t cart_addr, uint32_t i) {
  if (n->flash.mode == FLASH_STATUS) return (uint8_t)(n->flash.status >> (56 - 8 * (i & 7)));
  uint32_t o = (cart_addr - 0x08000000u) * 2 + i;
  return n->sram[o % FLASH_SIZE];
}

/* One byte of a PI DMA into domain 2: only the page buffer takes it. */
void flash_dma_store(N64 *n, uint32_t i, uint8_t v) {
  if (n->flash.mode == FLASH_WRITE && i < PAGE) n->flash.page[i] = v;
}
