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
| 2 | RSP: the scalar unit, the vector unit (multiply-accumulate, clip compares, reciprocal/inverse square root, all vector loads and stores), SP DMA, and running in step with the CPU, so games' own audio and graphics microcode runs | **done** |
| 3 | RDP: the 3D graphics chip, drawn in software | next |
| 4 | Saves: SRAM, FlashRAM, a save-type database, controller pak | partial (EEPROM, SRAM) |
| 5 | Speed: a JIT translator for ARM64 and x86-64, and GPU drawing | |

Until stage 3 lands, RDP command lists complete at once without drawing. RomRunner still opens N64 games in an outside emulator by default.

## Test cartridges

Besides its own tests, the core runs the community hardware test suites on every push: krom's CPU and RSP tests (public domain, [PeterLemon/N64](https://github.com/PeterLemon/N64)) and, when their assembler builds, Dillon's n64-tests. Results are checked against `test/suite_baseline.txt`; any test on that list that stops passing fails CI.

**186 of 196** pass: all 26 of Dillon's, and 160 of krom's 170. The rest:

| Test | Why |
|---|---|
| CPUTIMINGNTSC, CP1TIMINGNTSC | Exact per-instruction cycle timing (stage 5) |
| DMAAlignment-PI-ROM-FROM | 24 of 28 checks; the rest are post-boot register values and 8/16-bit cartridge reads games don't do |
| RSPCP2 VACC, VSAC, VEXTN/Q/T, V073 | Reserved vector opcodes no game uses |
| RSPXBUSRDP | Needs the RDP (stage 3) |

```
make tools
python3 test/suites.py --romtest build/romtest --krom <PeterLemon/N64 checkout> --baseline test/suite_baseline.txt
```

## Layout

| File | What |
|---|---|
| `src/n64.h` | The whole machine as one plain struct (save states copy it) |
| `src/cpu.c` | VR4300 interpreter: integer unit, COP0, TLB, exceptions |
| `src/fpu.c` | COP1: single/double precision, conversions, FR=0/1 register models |
| `src/rsp.c` | RSP: scalar unit, vector unit (COP2), vector loads/stores, SP/DP access |
| `src/bus.c` | Physical memory map, MI/VI/AI/PI/SI/RI, SP and DP registers, DMA |
| `src/pif.c` | High-level boot, joybus (controllers, EEPROM), CIC and save-type detection |
| `src/vi.c` | Framebuffer to XRGB8888 output |
| `src/n64.c` | ROM loading, reset, per-frame scheduler |
| `src/libretro.c` | libretro API |
| `test/asm.h` | A small MIPS encoder for writing test programs in C |
| `test/cpu_test.c` | 148 checks against the documented instruction behaviour |
| `test/rsp_test.c` | 24 checks of the vector unit |
| `test/romtest.c` | Runs one community test cartridge and reads its result |
| `test/suites.py` | Runs all of them, compares screenshot-only tests with their reference images, checks the baseline |
| `test/chksum64.c` | Writes the boot checksum into a ROM header (for building test ROMs) |
| `test/system_test.c` | Builds a cartridge in memory and checks boot, video, controller input, audio, save states and .v64 loading through the libretro API |

## Controls (RetroPad → N64)

B → A · Y → B · L2/R2 → Z · L → L · R → R · Start → Start · X → C-Up · A → C-Right · Select → C-Down · L3 → C-Left · right stick → C buttons · left stick → stick. With no analog input, the D-pad moves the stick (core option `rrn64_dpad_stick`).

## Speed

The interpreter runs about 100 million CPU instructions a second on a desktop CPU (with the RSP stepping alongside), around full speed for simple code. Phones will need the stage 5 JIT.
