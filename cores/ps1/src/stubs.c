/* Devices that come in later stages, answering as an idle device would:
 * CD-ROM with no disc and empty FIFOs, MDEC idle, SPU DMA ignored. */
#include "ps1.h"

uint32_t cdrom_read(PS1 *p, uint32_t off) {
  if (off == 0) return (uint32_t)p->cd_index | 0x18;   /* parameter FIFO empty and not full */
  return 0;
}
void cdrom_write(PS1 *p, uint32_t off, uint32_t v) {
  if (off == 0) p->cd_index = (uint8_t)(v & 3);
}
uint32_t cdrom_dma_word(PS1 *p) { (void)p; return 0; }

uint32_t mdec_read(PS1 *p, uint32_t off) { (void)p; return off == 4 ? 0x80040000u : 0; }
void mdec_write(PS1 *p, uint32_t off, uint32_t v) { (void)p; (void)off; (void)v; }
void mdec_dma_in(PS1 *p, uint32_t v) { (void)p; (void)v; }
uint32_t mdec_dma_out(PS1 *p) { (void)p; return 0; }

void spu_dma_write(PS1 *p, uint32_t v) { (void)p; (void)v; }
uint32_t spu_dma_read(PS1 *p) { (void)p; return 0; }
