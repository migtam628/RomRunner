/* chksum64 <rom.z64>: writes the CIC-6102 boot checksum into the header.
 * In-house stand-in for the tool test-ROM build scripts expect. */
#include <stdio.h>
#include <stdlib.h>

#include "../src/n64.h"

void rrn64_boot_checksum(const uint8_t *rom, uint32_t size, uint32_t cic, uint32_t *c1, uint32_t *c2);

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: chksum64 rom.z64\n"); return 2; }
  FILE *f = fopen(argv[1], "r+b");
  if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *rom = (uint8_t *)malloc((size_t)size);
  if (!rom || fread(rom, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 1; }
  uint32_t c1, c2;
  rrn64_boot_checksum(rom, (uint32_t)size, 6102, &c1, &c2);
  uint8_t hdr[8];
  wr32be(hdr, c1);
  wr32be(hdr + 4, c2);
  fseek(f, 0x10, SEEK_SET);
  fwrite(hdr, 1, 8, f);
  fclose(f);
  printf("%s: %08X %08X\n", argv[1], c1, c2);
  return 0;
}
