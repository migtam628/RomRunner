// Emulator apps RomRunner can hand games to, per system and platform.
//
// Command lines, executable names and Android intents follow ES-DE
// (EmulationStation Desktop Edition), whose es_systems.xml and
// es_find_rules.xml are maintained against these emulators' real CLIs.
// Placeholders: {rom} = game path, {inject} = first line of the game file
// (Vita3K title ids).
import '../models/console.dart';

enum HostOs { windows, macos, linux, android }

/// How to start an emulator on Android with a game.
class AndroidLaunch {
  const AndroidLaunch(
    this.package, {
    this.activity,
    this.action,
    this.category,
    this.data = true,
    this.extra,
    this.extraArray,
    this.dirExtra,
    this.clearTask = false,
  });

  final String package;

  /// Component class; a leading "." is relative to [package]. Null lets
  /// Android pick the package's matching activity.
  final String? activity;
  final String? action;
  final String? category;

  /// Put the game's content:// URI in the intent data.
  final bool data;

  /// Also (or instead) put the game URI string in this extra.
  final String? extra;

  /// String-array extra built from ["-r", {inject}] (Vita3K).
  final String? extraArray;

  /// For folder-based games (PS3): put the absolute folder path here.
  final String? dirExtra;
  final bool clearTask;

  Map<String, Object?> toMap() => {
        'package': package,
        'activity': activity,
        'action': action,
        'category': category,
        'data': data,
        'extra': extra,
        'extraArray': extraArray,
        'dirExtra': dirExtra,
        'clearTask': clearTask,
      };
}

class ExternalEmulator {
  const ExternalEmulator({
    required this.id,
    required this.name,
    this.args = const ['{rom}'],
    this.argsByOs = const {},
    this.windows = const [],
    this.linux = const [],
    this.linuxAppImages = const [],
    this.flatpaks = const [],
    this.macApps = const [],
    this.android = const [],
    this.injectsTitleId = false,
  });

  final String id;
  final String name;

  /// Default arguments; [argsByOs] overrides per platform.
  final List<String> args;
  final Map<HostOs, List<String>> argsByOs;

  /// Executable names searched on PATH (and common folders on Windows).
  final List<String> windows;
  final List<String> linux;

  /// AppImage name prefixes (case-insensitive), searched in the usual places.
  final List<String> linuxAppImages;
  final List<String> flatpaks;

  /// .app bundle names in /Applications or ~/Applications.
  final List<String> macApps;
  final List<AndroidLaunch> android;

  /// The game file holds a title id to pass instead of a path (Vita3K).
  final bool injectsTitleId;

  bool availableOn(HostOs os) => switch (os) {
        HostOs.windows => windows.isNotEmpty,
        HostOs.macos => macApps.isNotEmpty,
        HostOs.linux => linux.isNotEmpty || linuxAppImages.isNotEmpty || flatpaks.isNotEmpty,
        HostOs.android => android.isNotEmpty,
      };

  List<String> argsFor(HostOs os, {required String rom, String? injected}) => [
        for (final a in argsByOs[os] ?? args)
          a.replaceAll('{rom}', rom).replaceAll('{inject}', injected ?? ''),
      ];
}

const _eden = ExternalEmulator(
  id: 'eden',
  name: 'Eden',
  args: ['-f', '-g', '{rom}'],
  windows: ['eden.exe'],
  linux: ['eden', 'dev.eden_emu.eden'],
  linuxAppImages: ['eden'],
  flatpaks: ['dev.eden_emu.eden'],
  macApps: ['Eden.app', 'eden.app'],
  android: [
    AndroidLaunch('dev.eden.eden_emulator', activity: 'org.yuzu.yuzu_emu.activities.EmulationActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('dev.eden.eden_emulator.nightly', activity: 'org.yuzu.yuzu_emu.activities.EmulationActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('dev.legacy.eden_emulator', activity: 'org.yuzu.yuzu_emu.activities.EmulationActivity', action: 'android.intent.action.VIEW'),
  ],
);

const _citron = ExternalEmulator(
  id: 'citron',
  name: 'Citron',
  args: ['-f', '-g', '{rom}'],
  windows: ['citron.exe'],
  linux: ['citron'],
  linuxAppImages: ['citron'],
  macApps: ['Citron.app'],
);

const _ryujinx = ExternalEmulator(
  id: 'ryujinx',
  name: 'Ryujinx',
  args: ['{rom}', '--fullscreen'],
  windows: ['Ryujinx.exe', 'Ryujinx.Ava.exe'],
  linux: ['Ryujinx', 'Ryujinx.Ava', 'ryujinx'],
  linuxAppImages: ['ryujinx', 'ryubing'],
  flatpaks: ['io.github.ryubing.Ryujinx', 'org.ryujinx.Ryujinx'],
  macApps: ['Ryujinx.app', 'Ryubing.app'],
);

const _kenji = ExternalEmulator(
  id: 'kenji',
  name: 'Kenji-NX',
  android: [
    AndroidLaunch('org.kenjinx.android', activity: '.MainActivity', action: 'org.kenjinx.android.LAUNCH_GAME', data: false, extra: 'bootPath'),
  ],
);

const _dolphin = ExternalEmulator(
  id: 'dolphin',
  name: 'Dolphin',
  args: ['-b', '-e', '{rom}'],
  windows: ['Dolphin.exe'],
  linux: ['dolphin-emu'],
  linuxAppImages: ['dolphin_emulator', 'dolphin-emu'],
  flatpaks: ['org.DolphinEmu.dolphin-emu'],
  macApps: ['Dolphin.app'],
  android: [
    AndroidLaunch('org.dolphinemu.dolphinemu',
        activity: '.ui.main.TvMainActivity',
        action: 'android.intent.action.MAIN',
        category: 'android.intent.category.LEANBACK_LAUNCHER',
        data: false,
        extra: 'AutoStartFile'),
  ],
);

const _pcsx2 = ExternalEmulator(
  id: 'pcsx2',
  name: 'PCSX2',
  args: ['-batch', '-fullscreen', '{rom}'],
  windows: ['pcsx2-qt.exe', 'pcsx2-qtx64.exe', 'pcsx2-qtx64-avx2.exe'],
  linux: ['pcsx2-qt', 'pcsx2'],
  linuxAppImages: ['pcsx2'],
  flatpaks: ['net.pcsx2.PCSX2'],
  macApps: ['PCSX2.app'],
);

const _nethersx2 = ExternalEmulator(
  id: 'nethersx2',
  name: 'NetherSX2 / AetherSX2',
  android: [
    AndroidLaunch('xyz.aethersx2.android', activity: '.EmulationActivity', action: 'android.intent.action.MAIN', data: false, extra: 'bootPath', clearTask: true),
    AndroidLaunch('xyz.aethersx2.tturnip', activity: 'xyz.aethersx2.android.EmulationActivity', action: 'android.intent.action.MAIN', data: false, extra: 'bootPath', clearTask: true),
  ],
);

const _armsx2 = ExternalEmulator(
  id: 'armsx2',
  name: 'ARMSX2',
  android: [
    AndroidLaunch('come.nanodata.armsx2', activity: 'com.armsx2.MainActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('com.armsx2', activity: '.MainActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('com.armsx2.nightly', activity: 'com.armsx2.MainActivity', action: 'android.intent.action.VIEW'),
  ],
);

const _play = ExternalEmulator(
  id: 'play',
  name: 'Play!',
  args: ['--fullscreen', '--disc', '{rom}'],
  windows: ['Play.exe'],
  linuxAppImages: ['play!', 'play'],
  flatpaks: ['org.purei.Play'],
  macApps: ['Play.app'],
  android: [AndroidLaunch('com.virtualapplications.play', activity: '.MainActivity', action: 'android.intent.action.VIEW')],
);

const _rpcs3 = ExternalEmulator(
  id: 'rpcs3',
  name: 'RPCS3',
  args: ['--no-gui', '{rom}'],
  windows: ['rpcs3.exe'],
  linux: ['rpcs3'],
  linuxAppImages: ['rpcs3'],
  flatpaks: ['net.rpcs3.RPCS3'],
  macApps: ['RPCS3.app'],
);

const _aps3e = ExternalEmulator(
  id: 'aps3e',
  name: 'aPS3e',
  android: [
    AndroidLaunch('aenu.aps3e', activity: 'aenu.aps3e.EmulatorActivity', action: 'aenu.intent.action.APS3E', data: false, extra: 'iso_uri', dirExtra: 'game_dir'),
    AndroidLaunch('aenu.aps3e.premium', activity: 'aenu.aps3e.EmulatorActivity', action: 'aenu.intent.action.APS3E', data: false, extra: 'iso_uri', dirExtra: 'game_dir'),
  ],
);

const _xemu = ExternalEmulator(
  id: 'xemu',
  name: 'xemu',
  args: ['-full-screen', '-dvd_path', '{rom}'],
  windows: ['xemu.exe'],
  linux: ['xemu'],
  linuxAppImages: ['xemu'],
  flatpaks: ['app.xemu.xemu'],
  macApps: ['xemu.app'],
);

const _x1box = ExternalEmulator(
  id: 'x1box',
  name: 'X1 BOX',
  android: [AndroidLaunch('com.izzy2lost.x1box', activity: '.LauncherActivity', action: 'android.intent.action.VIEW')],
);

const _xenia = ExternalEmulator(
  id: 'xenia',
  name: 'Xenia',
  args: ['--fullscreen', '{rom}'],
  windows: ['xenia_canary.exe', 'xenia.exe'],
  linux: ['xenia_canary', 'xenia'],
  linuxAppImages: ['xenia_canary', 'xenia'],
  macApps: ['XeniOS.app'],
);

const _ax360e = ExternalEmulator(
  id: 'ax360e',
  name: 'aX360e',
  android: [
    AndroidLaunch('aenu.ax360e', activity: 'aenu.ax360e.EmulatorActivity', action: 'aenu.intent.action.AX360E', data: false, extra: 'game_uri'),
    AndroidLaunch('aenu.ax360e.free', activity: 'aenu.ax360e.EmulatorActivity', action: 'aenu.intent.action.AX360E', data: false, extra: 'game_uri'),
  ],
);

const _cemu = ExternalEmulator(
  id: 'cemu',
  name: 'Cemu',
  args: ['-g', '{rom}'],
  argsByOs: {HostOs.windows: ['-f', '-g', '{rom}']},
  windows: ['Cemu.exe'],
  linux: ['cemu', 'Cemu'],
  linuxAppImages: ['cemu'],
  flatpaks: ['info.cemu.Cemu'],
  macApps: ['Cemu.app'],
  android: [
    AndroidLaunch('info.cemu.cemu', activity: 'info.cemu.cemu.emulation.EmulationActivity'),
    AndroidLaunch('info.cemu.Cemu', activity: 'info.cemu.Cemu.emulation.EmulationActivity'),
  ],
);

const _simple64 = ExternalEmulator(
  id: 'simple64',
  name: 'simple64',
  args: ['--nogui', '{rom}'],
  windows: ['simple64-gui.exe'],
  linux: ['simple64-gui'],
  flatpaks: ['io.github.simple64.simple64'],
);

const _rmg = ExternalEmulator(
  id: 'rmg',
  name: "Rosalie's Mupen GUI",
  args: ['--nogui', '-q', '{rom}'],
  windows: ['RMG.exe'],
  linux: ['RMG'],
  linuxAppImages: ['rmg'],
  flatpaks: ['com.github.Rosalie241.RMG'],
);

const _project64 = ExternalEmulator(id: 'project64', name: 'Project64', windows: ['Project64.exe']);

const _mupen64 = ExternalEmulator(
  id: 'mupen64plus',
  name: 'Mupen64Plus',
  argsByOs: {
    HostOs.windows: ['--fullscreen', '{rom}'],
    HostOs.linux: ['--nogui', '{rom}'],
  },
  windows: ['mupen64plus-ui-console.exe'],
  linux: ['m64p'],
  flatpaks: ['io.github.m64p.m64p'],
  macApps: ['mupen64plus.app'],
);

const _gopher64 = ExternalEmulator(
  id: 'gopher64',
  name: 'Gopher64',
  args: ['-f', '{rom}'],
  windows: ['gopher64-windows-x86_64.exe'],
  linux: ['gopher64-linux-x86_64'],
  flatpaks: ['io.github.gopher64.gopher64'],
);

const _m64fz = ExternalEmulator(
  id: 'm64plusfz',
  name: 'M64Plus FZ',
  android: [
    AndroidLaunch('org.mupen64plusae.v3.fzurita', activity: 'paulscode.android.mupen64plusae.SplashActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('org.mupen64plusae.v3.fzurita.pro', activity: 'paulscode.android.mupen64plusae.SplashActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('org.mupen64plusae.v3.fzurita.amazon', activity: 'paulscode.android.mupen64plusae.SplashActivity', action: 'android.intent.action.VIEW'),
    AndroidLaunch('org.mupen64plusae.v3.alpha', activity: 'paulscode.android.mupen64plusae.SplashActivity', action: 'android.intent.action.VIEW'),
  ],
);

const _ppsspp = ExternalEmulator(
  id: 'ppsspp',
  name: 'PPSSPP',
  args: ['--fullscreen', '{rom}'],
  windows: ['PPSSPPWindows64.exe', 'PPSSPPWindows.exe'],
  linux: ['ppsspp', 'PPSSPPSDL', 'PPSSPPQt'],
  linuxAppImages: ['ppsspp'],
  flatpaks: ['org.ppsspp.PPSSPP'],
  macApps: ['PPSSPPSDL.app', 'PPSSPP.app'],
  android: [
    AndroidLaunch('org.ppsspp.ppssppgold', activity: 'org.ppsspp.ppsspp.PpssppActivity', action: 'android.intent.action.VIEW', category: 'android.intent.category.DEFAULT'),
    AndroidLaunch('org.ppsspp.ppsspp', activity: '.PpssppActivity', action: 'android.intent.action.VIEW', category: 'android.intent.category.DEFAULT'),
  ],
);

const _azahar = ExternalEmulator(
  id: 'azahar',
  name: 'Azahar',
  windows: ['azahar.exe'],
  linux: ['azahar'],
  linuxAppImages: ['azahar'],
  flatpaks: ['org.azahar_emu.Azahar'],
  macApps: ['Azahar.app', 'azahar.app'],
  android: [
    AndroidLaunch('org.azahar_emu.azahar', activity: 'org.citra.citra_emu.activities.EmulationActivity', clearTask: true),
    AndroidLaunch('io.github.lime3ds.android', activity: 'org.citra.citra_emu.activities.EmulationActivity', clearTask: true),
  ],
);

const _vita3k = ExternalEmulator(
  id: 'vita3k',
  name: 'Vita3K',
  args: ['-r', '{inject}'],
  injectsTitleId: true,
  windows: ['Vita3K.exe'],
  linux: ['vita3k', 'Vita3K'],
  linuxAppImages: ['vita3k'],
  macApps: ['Vita3K.app'],
  android: [
    AndroidLaunch('org.vita3k.emulator', activity: '.Emulator', data: false, extraArray: 'AppStartParameters'),
  ],
);

/// Emulators per system, best first.
const Map<Console, List<ExternalEmulator>> externalCatalog = {
  Console.n64: [_simple64, _rmg, _mupen64, _project64, _gopher64, _m64fz],
  Console.ps2: [_pcsx2, _nethersx2, _armsx2, _play],
  Console.psp: [_ppsspp],
  Console.gcwii: [_dolphin],
  Console.wiiu: [_cemu],
  Console.n3ds: [_azahar],
  Console.nswitch: [_eden, _citron, _ryujinx, _kenji],
  Console.xbox: [_xemu, _x1box],
  Console.xbox360: [_xenia, _ax360e],
  Console.ps3: [_rpcs3, _aps3e],
  Console.vita: [_vita3k],
};

List<ExternalEmulator> emulatorsFor(Console c, HostOs os) =>
    [for (final e in externalCatalog[c] ?? const <ExternalEmulator>[]) if (e.availableOn(os)) e];

/// Every Android package in the catalog (for package-visibility queries).
Iterable<String> get allAndroidPackages =>
    {for (final list in externalCatalog.values) for (final e in list) for (final a in e.android) a.package};

ExternalEmulator? emulatorById(String id) {
  for (final list in externalCatalog.values) {
    for (final e in list) {
      if (e.id == id) return e;
    }
  }
  return null;
}

/// Which emulator a user-picked executable most likely is (by file name).
ExternalEmulator? guessEmulator(Console c, String path) {
  final file = path.replaceAll('\\', '/').split('/').where((s) => s.isNotEmpty).last.toLowerCase();
  for (final e in externalCatalog[c] ?? const <ExternalEmulator>[]) {
    final names = [
      e.id,
      ...e.windows,
      ...e.linux,
      ...e.linuxAppImages,
      ...e.macApps,
      e.name,
    ].map((n) => n.toLowerCase().replaceAll(RegExp(r'\.(exe|app|appimage)$'), ''));
    if (names.any((n) => n.length > 2 && file.contains(n))) return e;
  }
  return null;
}

/// "PCSX2, Play!" — emulators for [c] on the current platform (all if unknown).
String emulatorNames(Console c, [HostOs? os]) {
  final list = externalCatalog[c] ?? const <ExternalEmulator>[];
  final names = [for (final e in list) if (os == null || e.availableOn(os)) e.name];
  return names.isEmpty ? 'a ${c.fullName} emulator' : names.join(', ');
}
