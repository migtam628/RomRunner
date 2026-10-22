## What's new

- PS1 core, stage 1 (start): R3000A CPU, bus, DMA, timers, SIO, GPU framework

## Install

Already have RomRunner? **Settings → Updates → Check for updates** installs this version (Android) or downloads it (desktop, iOS).

| Platform | File | Install |
|---|---|---|
| Android phones, newer Fire TV | `-android-arm64.apk` | Sideload: `adb install -r <file>` (Fire TV: `adb connect <tv-ip>` first) |
| 32-bit Fire TV sticks, older phones | `-android-armv7.apk` | Same. Much smaller than the all-CPU APK. |
| Any Android (all CPU types) | `-android.apk` | Same, ~250 MB. |
| Windows | `-windows-x64.zip` | Unzip, run `RomRunner.exe`. Cores included. |
| macOS | `-macos.zip` | Unzip, then right-click the app → Open the first time (not notarized). Cores download on first game. |
| Linux | `-linux-x64.tar.gz` | Extract, run `./romrunner`. Needs GTK 3. Cores included. |
| iOS | `-ios-unsigned.ipa` | Sign and install with Sideloadly or AltStore. |
| Web | `-web.zip` | Serve the folder over HTTP(S); games need internet for EmulatorJS. |

Bring your own ROMs, disc images and BIOS files.
Switch games open in a Switch emulator you install separately (Eden, Citron or Ryujinx); see the README.
