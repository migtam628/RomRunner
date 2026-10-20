# RomRunner

[![CI](https://github.com/migtam628/RomRunner/actions/workflows/ci.yml/badge.svg)](https://github.com/migtam628/RomRunner/actions/workflows/ci.yml)

Play your own retro game ROMs on **Android, Fire TV, iOS, macOS, Windows, Linux and the web** from one app. RomRunner is a Flutter front end on top of [libretro](https://www.libretro.com/) emulation cores. It also keeps Nintendo Switch games in the same library and opens them in a Switch emulator you install separately.

---

## Download

Every build comes from GitHub Actions; get it from **[Releases](https://github.com/migtam628/RomRunner/releases)**.

| Platform | File | Install |
|---|---|---|
| Android phones and tablets, Fire TV | `RomRunner-<ver>-android.apk` | Sideload. Fire TV: turn on *Developer options → ADB debugging* and *Install unknown apps*, then `adb connect <tv-ip>` and `adb install -r RomRunner-<ver>-android.apk`. It appears in the Fire TV launcher. |
| Windows | `RomRunner-<ver>-windows-x64.zip` | Unzip and run `RomRunner.exe`. Cores included. |
| macOS | `RomRunner-<ver>-macos.zip` | Unzip, then right-click the app → **Open** the first time (not notarized). Cores download on first game. |
| Linux | `RomRunner-<ver>-linux-x64.tar.gz` | Extract and run `./romrunner`. Needs GTK 3. Cores included. |
| iOS | `RomRunner-<ver>-ios-unsigned.ipa` | Sign and install with Sideloadly or AltStore. |
| Web | `RomRunner-<ver>-web.zip` | Serve the folder over HTTP(S). Needs internet (EmulatorJS loads from its CDN). |

`SHA256SUMS.txt` on each release has checksums.

---

## Systems

| System | Files | Emulated by |
|---|---|---|
| NES / Famicom Disk System | `.nes` `.unf` `.fds` | FCEUmm |
| Game Boy / Game Boy Color | `.gb` `.gbc` | Gambatte |
| Game Boy Advance | `.gba` | mGBA |
| SNES | `.sfc` `.smc` | Snes9x |
| Genesis / Mega Drive / Master System / Game Gear | `.md` `.gen` `.smd` `.sms` `.gg` (and `.bin`) | Genesis Plus GX |
| PlayStation | `.cue`+`.bin` `.chd` `.pbp` `.m3u` | PCSX-ReARMed |
| Nintendo Switch | `.nsp` `.xci` `.nro` | Your installed Switch emulator ([details](#nintendo-switch)) |

### What works where

| | Android | Fire TV | iOS | macOS | Windows | Linux | Web |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| NES · GB · GBA · SNES · Genesis | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| PS1 | ✓ | ✓ | ✓ ¹ | ✓ | ✓ | ✓ | ✓ ² |
| Switch (via installed emulator) | ✓ ³ | – | – | ✓ ⁴ | ✓ | ✓ | – |
| Game folders (linked) | ✓ | ✓ | – | ✓ | ✓ | ✓ | import only |
| Touch controls | ✓ | – | ✓ | – | – | – | EmulatorJS |
| Gamepads | ✓ | ✓ | – | – | – | – | ✓ |
| TV remote | ✓ | ✓ | – | – | – | – | – |

¹ Interpreter only (iOS forbids JIT), slower on older iPhones. ² Single-file `.chd` or `.pbp`. ³ Recent Snapdragon 8-series phones. ⁴ Ryujinx-based builds.

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
- **BIOS files** (optional) go in through Add ROMs like a game: `scph5501.bin`, `gba_bios.bin` and so on are recognised and installed into the BIOS folder automatically. PS1 runs without a BIOS, but compatibility is better with one.

## Playing

- **Pause menu:** Esc, the gamepad Guide button, the TV Back button, or the ⏸ button. It has 4 save-state slots, fast-forward (4×), reset and quit. On web, EmulatorJS has its own menu and save states, and in-game saves stay in the browser.
- **Battery saves** (`.srm`) are written about every 10 seconds and when you quit.
- **Touch controls** appear on phones and tablets, with a layout per system. They hide once a controller or keyboard is used; the pause menu brings them back.

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

## Nintendo Switch

RomRunner doesn't emulate Switch itself. No libretro core exists, and the working emulators are large standalone apps. Instead, Switch games sit in the library like everything else and open fullscreen in a Switch emulator you install and set up separately, with keys and firmware dumped from your own console.

| Platform | Emulators | How RomRunner finds it |
|---|---|---|
| Windows, Linux | Eden, Citron, Ryujinx / Ryubing | On `PATH`; Linux also checks for AppImages in `~`, `~/Downloads`, `~/Applications`. Or **Browse…** |
| macOS | Ryujinx-based builds | In `/Applications` or `~/Applications`, or **Browse…** |
| Android | Eden, Citron (Yuzu-family apps) | Installed apps that accept game files; or "Ask each time" |
| iOS, Fire TV, web | – | Not possible (no JIT on iOS, not enough GPU on Fire TV, no browser support) |

1. Install the emulator and get it running a game on its own first.
2. In RomRunner, open **Settings → Nintendo Switch** and pick the emulator.
3. Add games with **Add Switch games**, a **game folder**, or (desktop) **Add ROMs**.

Switch games are **linked, never copied**, since they're often 5–20 GB. RomRunner launches Yuzu-family emulators with `-f -g <game>` and Ryujinx with `<game> --fullscreen`. On Android it sends the file to the emulator with a one-time read grant. Removing a Switch game from the library never deletes the file.

---

## Where RomRunner keeps files

| Platform | Folder |
|---|---|
| macOS | `~/Library/Application Support/com.mttech.romrunner/` |
| Windows | `%APPDATA%\com.mttech\RomRunner\` |
| Linux | `~/.local/share/com.mttech.romrunner/` |
| Android, iOS | The app's private storage |

Inside: `roms/` (imported games, one folder per system), `saves/` (battery saves), `states/` (save states), `system/` (BIOS), `cores/` (downloaded cores, desktop), `folders.json` (linked folders) and `switch.json` (Switch emulator and linked games). The ⓘ button in the app shows the exact paths.

---

## Releases and CI

All packaging happens on GitHub Actions.

**Publish a release:** push a version tag. About 20 minutes later, a GitHub Release appears with every file in the [Download](#download) table.

```bash
git tag v0.1.0 && git push origin v0.1.0
```

- A tag with a dash, like `v0.2.0-beta.1`, is published as a pre-release.
- **Dry run** (build everything, publish nothing): **Actions → Release → Run workflow**, or push any branch named `release-test/<anything>`. The builds can be downloaded from that run.
- **CI** (`.github/workflows/ci.yml`) runs on every push and pull request. It runs `flutter analyze` and `flutter test`, compiles the C bridge with warnings as errors, and runs a headless check of the bridge against the Gambatte core with Blargg's `cpu_instrs` test ROM.

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
  lib/services/         library (native/web), game folders, core download, Switch launcher, app folders
  lib/screens/          library, settings, game screen (native and web)
  lib/widgets/          touch controller, pause menu
  lib/input/            keyboard / gamepad / TV-remote bindings
  android/…/MainActivity.kt   Switch handoff, storage access, file provider
  ios/embed_cores.sh    Xcode build phase that embeds and signs cores
packages/romrunner_core/  FFI plugin: the C libretro host
  src/romrunner_core.c    the bridge (one file, rr_* API)
  test/headless.c         headless test harness
tool/fetch_cores.sh     downloads prebuilt cores from the libretro buildbot, per platform
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
- **Every push:** GitHub Actions builds all seven platforms.
- **C bridge:** ran real cores headless for NES, GB (passes `cpu_instrs`), GBA, SNES, Genesis and PS1. Save states, 60 fps pacing, fast-forward, pause and core reload all work.
- **Linux app, end to end:** library, playing a game, pause menu, game folders, and Switch launching (checked against a stand-in emulator that records its arguments).
- **Web app in Chromium:** importing ROMs and folders, and full-window play.

**Not yet tested on real hardware:** Android phones, Fire TV, iOS and Windows builds, audio output, and real Switch emulators with real games. On macOS, the app has been run and the file-picker issue it showed is fixed; nothing else has been checked there yet.

### Known limitations

- **PS1 crashes:** in the Linux test VM, the PS1 core's fast (dynarec) mode crashed on homebrew `.exe` test programs, while its interpreter ran them fine. If PS1 games crash on a device, switch to the interpreter (one line in `game_screen_native.dart`, the same way iOS does).
- **Desktop gamepads:** not supported yet on iOS, macOS, Windows or Linux. Android and Fire TV gamepads work.
- **Zips in game folders:** these are skipped; unzip them with Add ROMs.
- **macOS:** the app isn't notarized, and it ships without the App Sandbox so it can load downloaded cores.

### Roadmap

1. Gamepads on iOS, macOS, Windows and Linux.
2. Core options UI (region, palette, renderer); the bridge already supports `rr_set_option`.
3. Box art from the libretro thumbnails repo, plus recent and favorite rows.
4. Return to RomRunner automatically when the Switch emulator exits (desktop).
5. Rewind, and CRT/LCD shaders.
6. GPU texture upload instead of per-frame image decode (matters for high-res PS1).

---

## Legal

RomRunner doesn't include or download any games, BIOS files, Switch keys or firmware. Use dumps of games and hardware you own. The libretro cores and EmulatorJS are GPL-licensed: personal and sideloaded use is fine, but publishing to an app store requires GPL compliance (source availability), and stores review emulators differently.
