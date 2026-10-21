import '../models/console.dart';
import '../models/game.dart';
import 'external_catalog.dart';
import 'external_support.dart';

ExternalSupport createExternalSupport() => _WebExternalSupport();

class _WebExternalSupport extends ExternalSupport {
  @override
  Future<bool> hasBuiltIn(Console c) async => false;

  @override
  HostOs? get host => null;
  @override
  String unsupportedReason(Console c) => '${c.fullName} games can\'t run in a browser.';
  @override
  bool get canBrowse => false;
  @override
  Future<EmulatorChoice?> current(Console c) async => null;
  @override
  Future<void> choose(Console c, EmulatorChoice choice) async {}
  @override
  Future<List<EmulatorChoice>> detect(Console c) async => const [];
  @override
  Future<EmulatorChoice?> browse(Console c) async => null;
  @override
  Future<List<Game>> addGames() async => const [];
  @override
  Future<List<Game>> linkFiles(Map<String, Console> pathToConsole) async => const [];
  @override
  Future<List<Game>> linkedGames() async => const [];
  @override
  Future<void> forget(Game game) async {}
  @override
  Future<void> launch(Game game) async => throw ExternalLaunchException(unsupportedReason(game.console));
}
