# RomRunner

[![CI](https://github.com/migtam628/RomRunner/actions/workflows/ci.yml/badge.svg)](https://github.com/migtam628/RomRunner/actions/workflows/ci.yml)

Play your own games on **Android, Fire TV, iOS, macOS, Windows, Linux and the web** from one library. RomRunner plays 17 systems itself through [libretro](https://www.libretro.com/) cores. For 11 more (PS2, PS3, Switch, GameCube/Wii, Xbox and others), it opens games in a dedicated emulator, and on desktop and Android it can download that emulator for you.

---

## Download

Every build comes from GitHub Actions; get it from **[Releases](https://github.com/migtam628/RomRunner/releases)**.

| Platform | File | Install |
|---|---|---|
| Android phones and tablets, Fire TV | `RomRunner-<ver>-android-arm64.apk` (64-bit phones, newer Fire TV), `RomRunner-<ver>-android-armv7.apk` (32-bit Fire TV sticks, older phones), or `RomRunner-<ver>-android.apk` (all CPU types, ~250 MB) | Sideload. The per-CPU APKs are much smaller; installing needs about twice the APK size free. Fire TV: turn on *Developer options → ADB debugging* and *Install unknown apps*, then `adb connect <tv-ip>` and `adb install -r RomRunner-<ver>-android-armv7.apk`. It appears in the Fire TV launcher. |
| Windows | `RomRunner-<ver>-windows-x64.zip` | Unzip and run `RomRunner.exe`. Cores included. |
| macOS | `RomRunner-<ver>-macos.zip` | Unzip, then right-click the app → **Open** the first time (not notarized). Cores download on first game. |
| Linux | `RomRunner-<ver>-linux-x64.tar.gz` | Extract and run `./romrunner`. Needs GTK 3. Cores included. |
| iOS | `RomRunner-<ver>-ios-unsigned.ipa` | Sign and install with Sideloadly or AltStore. |
| Web | `RomRunner-<ver>-web.zip` | Serve the folder over HTTP(S). Needs internet (EmulatorJS loads from its CDN). |

`SHA256SUMS.txt` on each release has checksums.

---

## Systems

### Played inside RomRunner

| System | Files | Core |
|---|---|---|
| NES / Famicom Disk System | `.nes` `.unf` `.fds` | FCEUmm |
| Game Boy / Color | `.gb` `.gbc` | Gambatte |
| Game Boy Advance | `.gba` | mGBA |
| Nintendo DS | `.nds` (tap the touchscreen) | melonDS DS (melonDS on iOS) |
| SNES | `.sfc` `.smc` | Snes9x |
| Virtual Boy | `.vb` | Beetle VB |
| Genesis / Mega Drive / Master System / Game Gear | `.md` `.gen` `.sms` `.gg` (and `.bin`) | Genesis Plus GX |
| Sega CD | `.cue`/`.chd`/`.iso` (needs BIOS) | Genesis Plus GX |
| Sega 32X | `.32x` | PicoDrive |
| PC Engine / TurboGrafx-16 | `.pce` | Beetle PCE Fast |
| Neo Geo Pocket / Color | `.ngp` `.ngc` | Beetle NeoPop |
| WonderSwan / Color | `.ws` `.wsc` | Beetle Cygne |
| Atari 2600 / 7800 / Lynx | `.a26` `.a78` `.lnx` (Lynx needs BIOS) | Stella / ProSystem / Handy |
| Arcade | FinalBurn Neo `.zip` romsets | FBNeo |
| PlayStation | `.cue`+`.bin` `.chd` `.pbp` `.m3u` | PCSX-ReARMed |

### Opened in a dedicated emulator

| System | Files | Emulators (RomRunner can install the ones in **bold**) |
|---|---|---|
| Nintendo 64 | `.z64` `.n64` `.v64` | **simple64** (Win), **RMG** (Win/Linux), Mupen64Plus, Project64, M64Plus FZ (Android) · built in on web |
| PlayStation 2 | `.iso` `.chd` `.cso` `.zso` | **PCSX2** · Android: **ARMSX2**, NetherSX2, Play! |
| PSP | `.iso` `.cso` `.pbp` | **PPSSPP** (Win/macOS/Android) |
| GameCube / Wii | `.rvz` `.iso` `.gcz` `.wbfs` | Dolphin |
| Wii U | `.wua` `.wud` `.wux` `.rpx` | **Cemu** |
| 3DS | `.3ds` `.cci` `.cxi` `.3dsx` | **Azahar** |
| Switch | `.nsp` `.xci` `.nro` | **Eden**, Citron, Ryujinx, Kenji-NX (Android) |
| Xbox | `.iso` `.xiso` | **xemu** · Android: X1 BOX |
| Xbox 360 | `.iso` `.xex` | **Xenia** (Win/Linux) · Android: aX360e |
| PlayStation 3 | game folders, `.iso` | **RPCS3** · Android: aPS3e |
| PS Vita | `.psvita` (installed in Vita3K) | **Vita3K** |

Shared extensions like `.iso` and `.bin` are recognised by looking inside the disc: PS1, PS2, PSP, PS3, Sega CD, GameCube/Wii, Xbox and Xbox 360 discs are told apart automatically. A folder named after the system (`PS2/`, `Genesis/`…) also settles it.

### What works where

| | Android | Fire TV | iOS | macOS | Windows | Linux | Web |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Built-in systems | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ ¹ |
| PS2 · PSP · GC/Wii · 3DS · Switch · N64 · Wii U · Vita | ✓ ² | some ³ | – | ✓ | ✓ | ✓ | N64 only |
| Xbox · PS3 | ✓ ² | – | – | ✓ | ✓ | ✓ | – |
| Xbox 360 | ✓ ² | – | – | – | ✓ | ✓ | – |
| Download emulators automatically | ✓ (one tap to install) | ✓ | – | ✓ | ✓ | ✓ | – |
| Game folders (linked) | ✓ | ✓ | – | ✓ | ✓ | ✓ | import only |
| Touch controls | ✓ | – | ✓ | – | – | – | EmulatorJS |
| Gamepads | ✓ | ✓ | – | – | – | – | ✓ |

¹ Through EmulatorJS; PS1 needs single-file `.chd`/`.pbp`. ² Through Android emulator apps, which need a fast phone (Snapdragon 8-series for Switch, PS3, Xbox 360). ³ Fire TV can run lighter emulators like PPSSPP. iOS can't open other apps with a file, so it plays only the built-in systems.

---

## Adding games

- **Add ROMs** copies files into RomRunner's library. `.zip` files are unpacked. A lone `.bin` asks which system it belongs to, and a `.cue` sheet keeps its `.bin` tracks together.
- **Add folder** (folder icon in the library, or **Settings → Game folders**) links a folder instead of copying it:
  - Games stay where they are, subfolders included. The folder is rescanned every time the library opens, so new games just appear.
  - Ambiguous `.bin` files are assigned by folder name, e.g. `Genesis/`, `Mega Drive/`, `PS1/`, `PSX/`.
  - Hidden folders and macOS `._` files are ignored. Archives are skipped and counted; unzip them with Add ROMs.
  - Removing a folder only hides its games. RomRunner never deletes files from linked folders.
  - **Android** asks once for *All files access*. **Fire TV** models without a folder picker let you type a path instead (e.g. `/storage/emulated/0/ROMs`).
  - **Web:** picking a folder imports its games into that browser tab for the session.
  - **iOS** doesn't let apps keep folder access between launches, so it uses Add ROMs.
- **Cover art** appears on each game's card automatically, from the [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails) collection. Games are matched by name: No-Intro / Redump file names (`Super Metroid (Japan, USA).sfc`) match exactly, and looser names (`zelda links awakening.gb`) usually work too, with the USA release preferred. Covers are downloaded once and kept offline; games with no match show the system tile. Switch has no covers. Turn it off in **Settings → Cover art**.
- **App icon:** pick one of 12 icons in **Settings → App icon**.
  - **Android, iOS:** changes the launcher / Home Screen icon. On Android the launcher can take a few seconds to update, and some launchers drop a home-screen shortcut, so you may need to add it again.
  - **macOS, Windows, Linux:** changes the Dock, taskbar or window icon while RomRunner is open. The installed app keeps the default icon (Play R) when it isn't running.
  - **Web:** changes the browser tab icon.
  - The icon sources are in `app/tool/icons/svg/`. `python3 app/tool/icons/generate_icons.py` rebuilds every platform's icon files.
- **BIOS files** (optional) go in through Add ROMs like a game: `scph5501.bin`, `gba_bios.bin` and so on are recognised and installed into the BIOS folder automatically. PS1 runs without a BIOS, but compatibility is better with one.

## Playing

- **Pause menu:** Esc, the gamepad Guide button, the TV Back button, or the ⏸ button. It has 4 save-state slots, fast-forward (4×), reset and quit. On web, EmulatorJS has its own menu and save states, and in-game saves stay in the browser.
- **Battery saves** (`.srm`) are written about every 10 seconds and when you quit.
- **Touch controls** appear on phones and tablets, with a layout per system. They hide once a controller or keyboard is used; the pause menu brings them back. The game runs full screen; swipe from the edge for the system bars.
- **Edit controls & screen** (pause menu) lets you lay things out your way. RomRunner saves the layout per system, with separate layouts for portrait and landscape:
  - Drag any button, the D-pad, or the game screen itself to move it. Pinch, or use the size slider, to resize it.
  - Hide buttons you don't need, and bring them back from the same screen.
  - Choose how the picture fills its space: **Fit**, **Pixel** (whole-number scaling for sharp pixels), or **Stretch**. You can also turn on smoothing.
  - **Settings** in the editor: opacity, overall button size, classic button colours (Super Famicom / PlayStation), and vibration.
  - **Reset** puts one item or the whole layout back to the default.

### Controls

| | Keyboard | Gamepad (Android / Fire TV) | Fire TV remote |
|---|---|---|---|
| D-pad | Arrows | D-pad | D-pad |
| A / B | X / Z | right / bottom face button | Center = A |
| X / Y | S / A | top / left face button | – |
| L / R · L2 / R2 | Q / W · 1 / 3 | shoulders · triggers | – |
| Start / Select | Enter / Backspace | Start / Select | Play-Pause / Menu |
| Pause menu | Esc | Guide | Back |
| Fast-forward | Tab | – | Fast-forward |

---

## Systems with a dedicated emulator

The first time you play a game for one of these systems, RomRunner looks for an installed emulator. If it finds none, it offers to install one:

- **Windows, macOS, Linux:** downloads the latest official release from the project's own GitHub, Forgejo or GitLab releases, or its download page. It unpacks it into RomRunner's folder (`emulators/`), sets it up, and starts the game.
- **Android / Fire TV:** downloads the official APK and opens Android's installer, so you tap **Install** once. The first time, Android asks you to allow RomRunner to install apps.
- **iOS:** not possible.

You can also pick or change the emulator in **Settings → Other systems**. That screen also finds emulators you installed yourself (PATH, `/Applications`, AppImages, Flatpaks, Android apps) and has **Browse…** on desktop.

RomRunner never downloads BIOS files, keys or firmware; those come from your own consoles. The emulator picker tells you what each system needs:

- **PS2:** BIOS.
- **Xbox:** MCPX ROM, BIOS and HDD image.
- **Switch:** `prod.keys` and firmware.
- **Wii U:** `keys.txt` for encrypted games.
- **PS3 and Vita:** system software, which Sony offers free; the emulators walk you through installing it.

Games for these systems are **linked, never copied**. The launch commands, executable names and Android intents come from [ES-DE](https://es-de.org)'s tested configurations; for example, PCSX2 gets `-batch -fullscreen`, Dolphin `-b -e`, RPCS3 `--no-gui` and Eden `-f -g`. Dolphin can't be downloaded automatically yet (its site blocks automated downloads), so install it yourself; RomRunner then finds it.

---

## Where RomRunner keeps files

| Platform | Folder |
|---|---|
| macOS | `~/Library/Application Support/com.mttech.romrunner/` |
| Windows | `%APPDATA%\com.mttech\RomRunner\` |
| Linux | `~/.local/share/com.mttech.romrunner/` |
| Android, iOS | The app's private storage |

Inside:

- `roms/`: imported games, one folder per system.
- `saves/`: battery saves.
- `states/`: save states.
- `system/`: BIOS files.
- `cores/`: downloaded cores (desktop).
- `emulators/`: emulators RomRunner installed.
- `covers/`: downloaded cover art (safe to delete; it's fetched again).
- `folders.json`: linked folders.
- `cover_art.json`: the cover art on/off setting.
- `controls.json`: your touch layouts.
- `app_icon.json`: the chosen app icon.
- `externals.json`: the chosen emulator per system, and linked games. The ⓘ button in the app shows the exact paths.

---

## Releases and CI

All packaging happens on GitHub Actions.

**Publish a release:** push a version tag. About 20 minutes later, a GitHub Release appears with every file in the [Download](#download) table.

```bash
git tag v0.1.0 && git push origin v0.1.0
```

- A tag with a dash, like `v0.2.0-beta.1`, is published as a pre-release.
- **Dry run** (build everything, publish nothing): **Actions → Release → Run workflow**, or push any branch named `release-test/<anything>`. The builds can be downloaded from that run.
- **CI** (`.github/workflows/ci.yml`) runs on every push and pull request:
  - `flutter analyze` and `flutter test`.
  - A warnings-as-errors compile of the C bridge.
  - A headless run of the bridge against Gambatte with Blargg's `cpu_instrs` test ROM.
  - One job per emulator that checks its official download still resolves for every platform (`app/tool/check_downloads.dart`).

### Android signing (one time)

Without a signing key in the repo secrets, each release APK is signed with a throwaway key, and Android won't install a new version over an old one. To set it up without installing anything locally:

1. On the repo page: **Code → Codespaces → Create codespace on main**.
2. In its terminal:
   ```bash
   unset GITHUB_TOKEN
   gh auth login                     # GitHub.com → HTTPS → Login with a web browser
   java -version || (sudo apt-get update && sudo apt-get install -y openjdk-17-jdk-headless)
   bash tool/android_signing.sh      # choose a password (hidden while typing)
   gh secret list --repo migtam628/RomRunner   # expect four ANDROID_… secrets
   ```
3. Back up the key, then delete the Codespace:
   ```bash
   cp ~/romrunner-release.jks /workspaces/RomRunner/   # right-click it in the file list → Download
   rm /workspaces/RomRunner/romrunner-release.jks
   ```
   Keep the `.jks` file and its password somewhere safe. If you lose them, devices have to uninstall RomRunner before they can install updates. `.gitignore` already blocks `.jks` files from being committed.

---

## How it works

```
 Flutter UI (app/lib)                one codebase, all 7 targets
   ├─ library, folders, settings, pause menu, touch pad, TV focus
   ├─ native targets ──► romrunner_core (C, FFI plugin)
   │                       ├─ dlopen()s a libretro core
   │                       ├─ emulation thread, audio-paced (miniaudio)
   │                       ├─ converts frames to RGBA; Dart paints them each vsync
   │                       └─ input bitmask, save states, battery saves
   ├─ web target ─────► EmulatorJS iframe (the same libretro cores, built to WASM)
   └─ Switch ─────────► launches the user's installed emulator (process / Android intent)
```

The emulation loop and audio run natively, so a slow UI frame never stutters the game.

### Project layout

```
.github/workflows/      ci.yml (checks), release.yml (all platforms → GitHub Release)
app/                    Flutter app, with runners for all 7 platforms
  lib/models/           systems, games, RetroPad buttons
  lib/services/         library (native/web), game folders, disc sniffing, core download,
                        external emulator catalog + launcher, emulator download sources + installer
  lib/screens/          library, settings, game screen (native and web)
  lib/widgets/          touch controller, pause menu
  lib/input/            keyboard / gamepad / TV-remote bindings
  android/…/MainActivity.kt   emulator intents, APK install, storage access, file provider
  tool/check_downloads.dart   resolves every emulator download (run by CI)
  ios/embed_cores.sh    Xcode build phase that embeds and signs cores
packages/romrunner_core/  FFI plugin: the C libretro host
  src/romrunner_core.c    the bridge (one file, rr_* API)
  test/headless.c         headless test harness
tool/fetch_cores.sh     downloads prebuilt cores from the libretro buildbot, per platform
tool/build_n64.sh       builds RomRunner's own N64 core and puts it where the app looks
tool/test_bridge.sh     builds the bridge headless and runs a ROM through a core
tool/android_signing.sh creates the Android release key and stores it in repo secrets
```

### Building locally (optional)

Not needed for releases, but useful for development. Requires Flutter 3.24+; CI uses 3.47.7.

```bash
cd app && flutter pub get
flutter run -d macos                     # or windows / linux; cores download on first game
../tool/fetch_cores.sh android && flutter build apk --release
../tool/fetch_cores.sh ios && open ios/Runner.xcworkspace   # set your signing team, then run
flutter build web --release
```

Linux needs `clang cmake ninja-build pkg-config libgtk-3-dev`. Test the C bridge alone with `tool/test_bridge.sh <core> <rom> [frames]`, which writes `frame.ppm`.

---

## Status

**Tested:**
- **Every push:** GitHub Actions builds all seven platforms and checks every emulator download.
- **C bridge:** ran real cores headless:
  - NES, Game Boy (passes `cpu_instrs`), GBA, SNES, Genesis and PS1.
  - DS (melonDS DS and DeSmuME), PC Engine, WonderSwan, Neo Geo Pocket, Virtual Boy, Atari 2600/7800 and 32X.
- **Linux app, end to end:** library, playing, pause menu, game folders, disc sniffing, and launching N64, PS2, PSP, GameCube, Xbox 360 and PS3 (folder) games. The launches were checked against stand-in emulators that record their arguments.
- **Installer:** unpacking zip, tar.xz and AppImage downloads and finding the emulator's executable are covered by tests.
- **Web app in Chromium:** importing ROMs and folders, and full-window play.

**Not yet tested on real hardware:** Android, Fire TV, iOS and Windows builds; audio; real emulators with real games; and the full download-and-install flow on each OS. macOS was run once, and the file-picker issue it showed is fixed.

### Known limitations

- **PS1 crashes:** in the Linux test VM, the PS1 core's fast (dynarec) mode crashed on homebrew `.exe` test programs, while its interpreter ran them fine. If PS1 games crash on a device, switch to the interpreter (one line in `game_screen_native.dart`, the same way iOS does).
- **Desktop gamepads:** not supported yet on iOS, macOS, Windows or Linux. Android and Fire TV gamepads work.
- **Zips in game folders:** these are skipped, except arcade romsets in a folder named `Arcade`/`FBNeo`/`MAME`. Unzip other zips with Add ROMs.
- **N64 inside RomRunner:** the libretro N64 cores need GPU rendering in the engine, so by default N64 opens in an external emulator on native platforms and runs built in on the web. RomRunner's own N64 core (below) can be picked instead in Settings → Emulators as "RomRunner N64 (built in, experimental)", or from the "no emulator" dialog.
- **Dolphin:** has to be installed by hand (see above).
- **macOS:** the app isn't notarized, and it ships without the App Sandbox so it can load downloaded cores.

### In-house emulators

RomRunner is getting its own emulators, written in C from public hardware documentation with no code from other emulators. Each one plugs into the same bridge as the libretro cores, and each is tested on every push. The libretro core for a system stays as the fallback until the in-house one is ready.

- **N64** (`cores/n64/`): stages 1 and 2 of 5 are done (main processor, memory, boot without a BIOS, video, controllers, EEPROM, save states, the RSP), and stage 3, the RDP graphics chip in software, has its first pass. 238 of 323 community hardware test cartridges pass, and krom's N64NICCC 3D demo renders correctly. Releases ship it on Android, iOS, macOS, Windows and Linux as an experimental choice for N64 (`tool/build_n64.sh <platform>` builds it locally). See [cores/n64/README.md](cores/n64/README.md).

### Roadmap

1. Gamepads on iOS, macOS, Windows and Linux.
2. Core options UI (region, palette, renderer); the bridge already supports `rr_set_option`.
3. Recent and favorite rows in the library.
4. Return to RomRunner automatically when an external emulator exits (desktop).
5. GPU rendering in the engine, to run N64, PSP and Dreamcast cores built in.
6. Rewind, and CRT/LCD shaders.
7. GPU texture upload instead of per-frame image decode (matters for high-res PS1).

---

## Legal

RomRunner doesn't include or download any games, BIOS files, keys or firmware. The emulators it installs are open-source projects, downloaded from their own official releases. Use dumps of games and hardware you own. The libretro cores and EmulatorJS are GPL-licensed: personal and sideloaded use is fine, but publishing to an app store requires GPL compliance (source availability), and stores review emulators differently.
