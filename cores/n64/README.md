# RomRunner N64

RomRunner's own Nintendo 64 emulator. It is written in C from public hardware documentation (the VR4300 user's manual, the MIPS III instruction set, and n64brew). It contains no code from other emulators.

It plugs into RomRunner the same way the other emulators do (through the libretro API), so the app's bridge loads it unchanged.

```
make          # build/romrunner_n64_libretro.so (.dylib on macOS)
make test     # CPU and system tests
```

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | Main processor (VR4300) with floating point, virtual memory (TLB), exceptions and interrupts; memory map; cartridge loading in .z64/.v64/.n64 order; boot without a PIF ROM or BIOS; CIC detection; video output; audio DMA; controllers and EEPROM over the PIF; save states | **done** |
| 2 | RSP: the second processor, its vector unit, and audio microcode | next |
| 3 | RDP: the 3D graphics chip, drawn in software | |
| 4 | Saves: SRAM, FlashRAM, a save-type database, controller pak | partial (EEPROM, SRAM) |
| 5 | Speed: a JIT translator for ARM64 and x86-64, and GPU drawing | |

Until stages 2 and 3 land, starting an RSP task or an RDP command list completes it at once. Commercial games keep running, but show no 3D graphics and play no audio. RomRunner still opens N64 games in an outside emulator by default.

## Layout

| File | What |
|---|---|
| `src/n64.h` | The whole machine as one plain struct (save states copy it) |
| `src/cpu.c` | VR4300 interpreter: integer unit, COP0, TLB, exceptions |
| `src/fpu.c` | COP1: single/double precision, conversions, FR=0/1 register models |
| `src/bus.c` | Physical memory map, MI/VI/AI/PI/SI/RI, SP and DP registers, DMA |
| `src/pif.c` | High-level boot, joybus (controllers, EEPROM), CIC and save-type detection |
| `src/vi.c` | Framebuffer to XRGB8888 output |
| `src/n64.c` | ROM loading, reset, per-frame scheduler |
| `src/libretro.c` | libretro API |
| `test/asm.h` | A small MIPS encoder for writing test programs in C |
| `test/cpu_test.c` | 148 checks against the documented instruction behaviour |
| `test/system_test.c` | Builds a cartridge in memory and checks boot, video, controller input, audio, save states and .v64 loading through the libretro API |

## Controls (RetroPad → N64)

B → A · Y → B · L2/R2 → Z · L → L · R → R · Start → Start · X → C-Up · A → C-Right · Select → C-Down · L3 → C-Left · right stick → C buttons · left stick → stick. With no analog input, the D-pad moves the stick (core option `rrn64_dpad_stick`).

## Speed

The interpreter runs about 120 million instructions a second on a desktop CPU, which is around full speed for simple code. Phones will need the stage 5 JIT.
