# RomRunner N64

RomRunner's own Nintendo 64 emulator. It is written in C from public hardware documentation (the VR4300 user's manual, the MIPS III instruction set, and n64brew). It contains no code from other emulators.

It plugs into RomRunner the same way the other emulators do (through the libretro API), so the app's bridge loads it unchanged.

```
make          # build/romrunner_n64_libretro.so (.dylib on macOS)
make test     # CPU and system tests
```

CMake builds it for Windows (MSVC), Android (NDK), iOS and universal macOS; `tool/build_n64.sh <platform>` runs that and copies the library where the app finds it.

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | Main processor (VR4300) with floating point, virtual memory (TLB), exceptions and interrupts; memory map; cartridge loading in .z64/.v64/.n64 order; boot without a PIF ROM or BIOS; CIC detection and the CIC-6105 challenge; video output; audio DMA; controllers and EEPROM over the PIF; save states | **done** |
| 2 | RSP: the scalar unit, the vector unit (multiply-accumulate, clip compares, reciprocal/inverse square root, all vector loads and stores), SP DMA, and running in step with the CPU, so games' own audio and graphics microcode runs | **done** |
| 3 | RDP, drawn in software: triangles with shade/texture/depth interpolation and perspective-correct textures, texture and fill rectangles, fill/copy/1-cycle/2-cycle modes, TMEM loads (block, tile, TLUT), every texture format except YUV, point and 3-point bilinear filtering, wrap/mirror/clamp, the colour combiner (with its 9-bit overflow rule), the blender, Z buffer, 16/32-bit framebuffers | **first pass done** |
| 4 | Saves: SRAM, FlashRAM, a save-type database, controller pak | **mostly done** (EEPROM 4K/16K, SRAM, FlashRAM; no controller pak yet) |
| 5 | Speed: a JIT translator for ARM64 and x86-64, and GPU drawing | |

Not yet in the RDP: antialiasing/coverage, dithering, mipmap LOD selection, YUV textures. RomRunner still opens N64 games in an outside emulator by default; this core is an experimental choice in Settings → Emulators → N64, shipped in every native release.

krom's N64NICCC polygon demo (RSP + RDP streaming) renders correctly at about 2.3x full speed on a desktop CPU. Majora's Mask plays: intro, title screen with Clock Town, file select and naming, and its FlashRAM save survives a restart.

## Test cartridges

Besides its own tests, the core runs the community hardware test suites on every push: krom's CPU and RSP tests (public domain, [PeterLemon/N64](https://github.com/PeterLemon/N64)) and, when their assembler builds, Dillon's n64-tests. Results are checked against `test/suite_baseline.txt`; any test on that list that stops passing fails CI.

**238 of 323** pass: all 26 of Dillon's, 160 of krom's 170 CPU/RSP tests, and 52 of krom's 127 RDP tests. RDP tests are compared with their reference screenshots and pass at 99% of pixels matching (many of the rest are at 92 to 98%: texture filtering and edge rules still differ slightly; 7 are animated or interactive and can't be compared by screenshot; 4 need YUV textures). The CPU/RSP ones that fail:

| Test | Why |
|---|---|
| CPUTIMINGNTSC, CP1TIMINGNTSC | Exact per-instruction cycle timing (stage 5) |
| DMAAlignment-PI-ROM-FROM | 24 of 28 checks; the rest are post-boot register values and 8/16-bit cartridge reads games don't do |
| RSPCP2 VACC, VSAC, VEXTN/Q/T, V073 | Reserved vector opcodes no game uses |
| RSPXBUSRDP | Reports through the RDP; not checked yet |

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
| `src/rdp.c` | RDP: command decoder, rasteriser, TMEM, texture sampling, combiner, blender, Z buffer |
| `src/rdp_thread.c` | Optional worker thread that draws RDP commands in parallel (on in the app, off in tests) |
| `src/flash.c` | FlashRAM: status/read/write/erase command protocol |
| `src/bus.c` | Physical memory map, MI/VI/AI/PI/SI/RI, SP and DP registers, DMA |
| `src/pif.c` | High-level boot, joybus (controllers, EEPROM), CIC and save-type detection |
| `src/vi.c` | Framebuffer to XRGB8888 output |
| `src/n64.c` | ROM loading, reset, per-frame scheduler |
| `src/libretro.c` | libretro API |
| `test/asm.h` | A small MIPS encoder for writing test programs in C |
| `test/cpu_test.c` | 148 checks against the documented instruction behaviour |
| `test/rsp_test.c` | 24 checks of the vector unit |
| `test/rdp_test.c` | 11 checks: fill mode, triangles, copy-mode texture rectangles, SYNC_FULL |
| `test/romtest.c` | Runs one community test cartridge and reads its result |
| `test/play.c` | Runs a real game with scripted button presses, saves screenshots and the save file, prints the speed |
| `test/suites.py` | Runs all of them, compares screenshot-only tests with their reference images, checks the baseline |
| `test/chksum64.c` | Writes the boot checksum into a ROM header (for building test ROMs) |
| `test/system_test.c` | Builds a cartridge in memory and checks boot, video, controller input, audio, save states, .v64 loading, the CIC-6105 challenge and FlashRAM |

## Controls (RetroPad → N64)

B → A · Y → B · L2/R2 → Z · L → L · R → R · Start → Start · X → C-Up · A → C-Right · Select → C-Down · L3 → C-Left · right stick → C buttons · left stick → stick. With no analog input, the D-pad moves the stick (core option `rrn64_dpad_stick`).

## Speed

Fast paths for unmapped RDRAM accesses and skipping the CPU's polling loops (with Count kept exact) put the N64NICCC demo at about 2.3x full speed on a desktop CPU.

For real games: the CPU applies Count, Random and interrupt checks in batches instead of every instruction (the timer interrupt still lands on the exact instruction), the combiner is decoded once per mode change instead of per pixel, and the RDP draws on a second core (core option `rrn64_rdp_thread`, on by default). Majora's Mask went from 69% to 99% of full speed on a 2.1 GHz Xeon (`test/play.c`). Slower phones will still need the stage 5 JIT.
