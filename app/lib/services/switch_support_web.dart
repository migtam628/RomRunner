import '../models/game.dart';
import 'switch_support.dart';

SwitchSupport createSwitchSupport() => _WebSwitchSupport();

class _WebSwitchSupport implements SwitchSupport {
  @override
  bool get supported => false;
  @override
  String get unsupportedReason => 'Switch games can\'t run in a browser. Use RomRunner for Windows, macOS, Linux or Android.';
  @override
  bool get canBrowse => false;
  @override
  Future<EmulatorChoice?> current() async => null;
  @override
  Future<void> choose(EmulatorChoice choice) async {}
  @override
  Future<List<EmulatorChoice>> detect() async => const [];
  @override
  Future<EmulatorChoice?> browse() async => null;
  @override
  Future<List<Game>> addGames() async => const [];
  @override
  Future<List<Game>> linkFiles(List<String> paths) async => const [];
  @override
  Future<List<Game>> linkedGames() async => const [];
  @override
  Future<void> forget(Game game) async {}
  @override
  Future<void> launch(Game game) async => throw SwitchLaunchException(unsupportedReason);
}
