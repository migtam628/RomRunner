# RomRunner PS1

RomRunner's own PlayStation emulator. It is written in C from public hardware documentation (the R3000A manual, nocash's PSX-SPX) and contains no code from other emulators. Like the N64 core, it plugs into the app through the libretro API.

```
make          # build/romrunner_ps1_libretro.so (.dylib on macOS)
make test     # CPU and system checks
make tools    # build/exerun: run a PS-EXE and print what it outputs
```

## Status

| Stage | Scope | State |
|---|---|---|
| 1 | R3000A CPU (load and branch delay slots, COP0 exceptions and interrupts, multiply/divide timing), memory map, DMA (all sync modes, linked lists, OTC), timers, controller port, GPU command framework, VRAM transfers, display output, PS-EXE loading | **in progress** |
| 2 | GPU rasteriser: polygons (flat, Gouraud, textured 4/8/15-bit, raw or modulated), the four semi-transparency modes, dithering, mask bit, texture window, CLUT cache, lines and poly-lines, rectangles with texture flip, 16-pixel VRAM copy buffer | **done**: all 11 drawing tests match the console's VRAM pixel for pixel |
| 3 | GTE (the geometry coprocessor: RTPS/RTPT, NCLIP, lighting, the rest) | |
| 4 | CD-ROM (disc images, commands, XA audio), SPU (ADPCM voices, reverb), memory cards, MDEC video | |
| 5 | Speed: a recompiler for ARM64 and x86-64 | |

The machine is one plain struct (`src/ps1.h`), so save states are a single copy.

## BIOS

Games need a BIOS. The core looks in the system folder for a console BIOS dump (`scph5501.bin`, `scph1001.bin`, `scph7001.bin` and others) or `openbios.bin`, the free BIOS written from scratch by the pcsx-redux project (MIT). Without either, it answers the BIOS console-output calls itself, which is enough for test programs.

## Test programs

CI runs JaCzekanski's [ps1-tests](https://github.com/JaCzekanski/ps1-tests) (MIT) twice on every push: without a BIOS and booted through OpenBIOS (built in CI from [pcsx-redux/nugget](https://github.com/pcsx-redux/nugget)). Each program prints `pass` / `fail` per check; any check listed in `test/suite_baseline.txt` or `test/suite_baseline_openbios.txt` that stops passing fails CI.

| | Checks passing |
|---|---|
| No BIOS | 38 of 47 |
| OpenBIOS | 53 of 62 |

Drawing tests are judged by their reference VRAM picture (from the test sources, `--refs`): the displayed area must match exactly. Rules worked out against those references are noted in `src/raster.c` (fill rule, interpolation rounding, line stepping, the X-flip offset, CLUT cache reloads). One test has a known unmatched area: `gpu/lines` never sets one vertex colour, so that segment shows the console's stack contents (skipped, see `IGNORE` in `test/suites.py`). The CD-ROM, SPU and MDEC tests wait for stage 4.

```
python3 test/suites.py --exerun build/exerun --tests <unzipped tests.zip> --refs <ps1-tests checkout> [--bios openbios.bin] --baseline test/suite_baseline.txt
```
