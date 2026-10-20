import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../input/key_mapping.dart';
import '../models/console.dart';
import '../models/game.dart';
import '../services/library.dart';
import '../services/cover_art.dart';
import '../widgets/cover_image.dart';
import '../services/external_support.dart';
import '../widgets/install_dialog.dart';
import 'game_screen.dart';
import 'settings_screen.dart';

class LibraryScreen extends StatefulWidget {
  const LibraryScreen({super.key});

  @override
  State<LibraryScreen> createState() => _LibraryScreenState();
}

/// Systems this build can play itself or open in an installed emulator.
List<Console> availableSystems() =>
    [for (final c in Console.values) if (c.playsHere || ExternalSupport.instance.supports(c)) c];

class _LibraryScreenState extends State<LibraryScreen> {
  final _library = GameLibrary.instance;
  List<Game> _games = [];
  Console? _filter;
  bool _loading = true;
  bool _importing = false;

  @override
  void initState() {
    super.initState();
    _reload();
  }

  Future<void> _reload() async {
    final games = await _library.load();
    if (!mounted) return;
    setState(() {
      _games = games;
      _loading = false;
      if (_filter != null && !games.any((g) => g.console == _filter)) _filter = null;
    });
  }

  Future<Console?> _askConsole(String fileName) => showDialog<Console>(
        context: context,
        builder: (context) => SimpleDialog(
          title: Text('Which system is "$fileName" for?'),
          children: [
            for (final c in availableSystems())
              SimpleDialogOption(
                onPressed: () => Navigator.pop(context, c),
                child: Row(children: [
                  _Dot(color: c.color),
                  const SizedBox(width: 12),
                  Expanded(child: Text(c.fullName)),
                ]),
              ),
          ],
        ),
      );

  Future<void> _import() async {
    if (_importing) return;
    setState(() => _importing = true);
    try {
      final result = await _library.importFiles(_askConsole);
      await _reload();
      if (!mounted) return;
      final added = result.games;
      final parts = [
        if (added.length == 1) 'Added ${added.first.title}',
        if (added.length > 1) 'Added ${added.length} games',
        if (result.bios.isNotEmpty) 'Installed BIOS: ${result.bios.join(', ')}',
        ...result.notes,
      ];
      _snack(parts.isEmpty ? 'No supported ROMs in that selection' : parts.join(' · '));
      if (kIsWeb && added.length == 1) _play(added.first);
    } catch (e) {
      _snack('Import failed: $e');
    } finally {
      if (mounted) setState(() => _importing = false);
    }
  }

  Future<void> _addFolder() async {
    if (_importing) return;
    setState(() => _importing = true);
    try {
      final r = await _library.addFolder(askPath: () => askFolderPath(context));
      await _reload();
      if (!mounted) return;
      if (r.error != null) {
        _snack(r.error!);
      } else if (r.folder != null) {
        final parts = [
          r.found == 1 ? 'Found 1 game' : 'Found ${r.found} games',
          if (r.skipped > 0)
            'skipped ${r.skipped} .bin/.zip file${r.skipped == 1 ? '' : 's'} (put .bin files in a folder named after '
                'the system, and unzip archives with Add ROMs)',
        ];
        _snack(parts.join(' · '));
      }
    } catch (e) {
      _snack('Could not read that folder: $e');
    } finally {
      if (mounted) setState(() => _importing = false);
    }
  }

  void _snack(String msg, {SnackBarAction? action}) => ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(msg), action: action));

  Future<void> _play(Game game) async {
    if (game.console.launchesExternally) {
      try {
        await ExternalSupport.instance.launch(game);
        _snack('Opening ${game.title}…');
      } on ExternalLaunchException catch (e) {
        final emu = e.installable;
        if (emu != null && mounted) {
          final result = await showInstallDialog(context, emu, game.console);
          if (result is EmulatorChoice) return _play(game); // installed: start the game
          if (result == true) _snack('Tap Install in the Android installer, then open ${game.title} again.');
          return;
        }
        _snack(e.message,
            action: e.needsSetup ? SnackBarAction(label: 'Settings', onPressed: _openSettings) : null);
      }
      return;
    }
    await Navigator.of(context).push(MaterialPageRoute(builder: (_) => GameScreen(game: game)));
  }

  Future<void> _openSettings() async {
    final changed = await Navigator.of(context).push<bool>(MaterialPageRoute(builder: (_) => const SettingsScreen()));
    if (changed == true) await _reload();
  }

  Future<void> _remove(Game game) async {
    if (game.folder != null) {
      _snack('${game.title} comes from ${game.folder}. Remove the folder in Settings to hide its games.',
          action: SnackBarAction(label: 'Settings', onPressed: _openSettings));
      return;
    }
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Remove game?'),
        content: Text(game.console.launchesExternally
            ? '${game.title} will be removed from the library. The file itself stays where it is.'
            : '${game.title} will be removed from the library. Save states are kept.'),
        actions: [
          TextButton(autofocus: true, onPressed: () => Navigator.pop(context, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(context, true), child: const Text('Remove')),
        ],
      ),
    );
    if (ok == true) {
      await _library.remove(game);
      await _reload();
    }
  }

  Future<void> _showInfo() async {
    final storage = await _library.describeStorage();
    if (!mounted) return;
    showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      builder: (context) => SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(24, 0, 24, 24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Storage', style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(height: 8),
              SelectableText(storage),
              const SizedBox(height: 20),
              Text('Systems', style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(height: 8),
              for (final c in availableSystems())
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 3),
                  child: Text('${c.fullName} — ${[...c.extensions, ...c.also].map((e) => '.$e').join(' ')}'
                      '${c.playsHere ? '' : ' (opens in your emulator)'}'),
                ),
              const SizedBox(height: 20),
              Text('Controls', style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(height: 8),
              const Text(keyboardHelp),
              const SizedBox(height: 8),
              const Text('Fire TV / Android TV remote: D-pad moves, center = A, '
                  'play/pause = Start, menu = Select, back = pause menu. '
                  'Bluetooth gamepads work on Android and Fire TV.'),
              const SizedBox(height: 8),
              const Text('PS1 runs without a BIOS, but compatibility is better with '
                  'scph5501.bin — add it with Add ROMs and it is installed automatically.'),
            ],
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final consoles = Console.values.where((c) => _games.any((g) => g.console == c)).toList();
    final visible = _filter == null ? _games : _games.where((g) => g.console == _filter).toList();

    return Scaffold(
      appBar: AppBar(
        title: const Text('RomRunner', style: TextStyle(fontWeight: FontWeight.w800, letterSpacing: -0.5)),
        actions: [
          IconButton(tooltip: 'Info', onPressed: _showInfo, icon: const Icon(Icons.info_outline_rounded)),
          IconButton(tooltip: 'Settings', onPressed: _openSettings, icon: const Icon(Icons.settings_outlined)),
          if (_library.supportsFolders)
            IconButton(
              tooltip: 'Add folder',
              onPressed: _importing ? null : _addFolder,
              icon: const Icon(Icons.create_new_folder_outlined),
            ),
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: FilledButton.icon(
              onPressed: _importing ? null : _import,
              icon: _importing
                  ? const SizedBox.square(dimension: 16, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.add_rounded),
              label: const Text('Add ROMs'),
            ),
          ),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _games.isEmpty
              ? _EmptyState(onImport: _import, onAddFolder: _library.supportsFolders ? _addFolder : null)
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    SizedBox(
                      height: 52,
                      child: ListView(
                        scrollDirection: Axis.horizontal,
                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                        children: [
                          _FilterChip(label: 'All (${_games.length})', selected: _filter == null,
                              onTap: () => setState(() => _filter = null)),
                          for (final c in consoles)
                            _FilterChip(
                              label: '${c.name} (${_games.where((g) => g.console == c).length})',
                              color: c.color,
                              selected: _filter == c,
                              onTap: () => setState(() => _filter = c),
                            ),
                        ],
                      ),
                    ),
                    Expanded(
                      child: GridView.builder(
                        padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
                        gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                          maxCrossAxisExtent: 210,
                          mainAxisSpacing: 14,
                          crossAxisSpacing: 14,
                          childAspectRatio: 0.7,
                        ),
                        itemCount: visible.length,
                        itemBuilder: (context, i) => _GameCard(
                          // Re-fetch art when the game or the cover setting changes.
                          key: ValueKey('${visible[i].id}|${visible[i].path}|${CoverArt.instance.enabled}'),
                          game: visible[i],
                          autofocus: i == 0,
                          onPlay: () => _play(visible[i]),
                          onRemove: () => _remove(visible[i]),
                        ),
                      ),
                    ),
                  ],
                ),
    );
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({required this.onImport, this.onAddFolder});
  final VoidCallback onImport;
  final VoidCallback? onAddFolder;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Wrap(
              spacing: 8,
              children: [
                for (final c in availableSystems()) _Dot(color: c.color, size: 14),
              ],
            ),
            const SizedBox(height: 20),
            Text('Your library is empty', style: theme.textTheme.headlineSmall),
            const SizedBox(height: 8),
            Text(
              'Add ROMs and disc images of games you own.\n'
              '${availableSystems().length} systems, from NES and Atari to '
              '${ExternalSupport.instance.supports(Console.ps2) ? 'PS2 and Switch' : 'DS and PlayStation'}. See ⓘ for the list.',
              textAlign: TextAlign.center,
              style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant),
            ),
            const SizedBox(height: 24),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              alignment: WrapAlignment.center,
              children: [
                FilledButton.icon(
                  autofocus: true,
                  onPressed: onImport,
                  icon: const Icon(Icons.add_rounded),
                  label: const Text('Add ROMs'),
                ),
                if (onAddFolder != null)
                  OutlinedButton.icon(
                    onPressed: onAddFolder,
                    icon: const Icon(Icons.create_new_folder_outlined),
                    label: const Text('Add folder'),
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _FilterChip extends StatelessWidget {
  const _FilterChip({required this.label, required this.selected, required this.onTap, this.color});
  final String label;
  final bool selected;
  final VoidCallback onTap;
  final Color? color;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(right: 8),
      child: ChoiceChip(
        avatar: color == null ? null : _Dot(color: color!),
        label: Text(label),
        selected: selected,
        onSelected: (_) => onTap(),
      ),
    );
  }
}

class _Dot extends StatelessWidget {
  const _Dot({required this.color, this.size = 10});
  final Color color;
  final double size;

  @override
  Widget build(BuildContext context) =>
      Container(width: size, height: size, decoration: BoxDecoration(color: color, shape: BoxShape.circle));
}

/// Focusable game tile; grows and outlines itself when focused (TV / keyboard).
class _GameCard extends StatefulWidget {
  const _GameCard({super.key, required this.game, required this.onPlay, required this.onRemove, this.autofocus = false});
  final Game game;
  final VoidCallback onPlay;
  final VoidCallback onRemove;
  final bool autofocus;

  @override
  State<_GameCard> createState() => _GameCardState();
}

class _GameCardState extends State<_GameCard> {
  bool _focused = false;
  Cover? _cover;

  @override
  void initState() {
    super.initState();
    _loadCover();
  }

  @override
  void didUpdateWidget(_GameCard old) {
    super.didUpdateWidget(old);
    if (old.game.id != widget.game.id || old.game.path != widget.game.path) {
      _cover = null;
      _loadCover();
    }
  }

  Future<void> _loadCover() async {
    final game = widget.game;
    try {
      final cover = await CoverArt.instance.coverFor(game);
      if (mounted && widget.game.id == game.id) setState(() => _cover = cover);
    } catch (_) {
      // No art; the colored tile stays.
    }
  }

  Widget _art(Console c) {
    final cover = _cover;
    Widget fallback() => Center(
          child: Text(
            c.name.toUpperCase(),
            textAlign: TextAlign.center,
            style: const TextStyle(color: Colors.white, fontWeight: FontWeight.w900, fontSize: 22, letterSpacing: 1),
          ),
        );
    if (cover == null) return fallback();
    final image = cover.filePath != null
        ? Image(image: coverImageProvider(cover.filePath!), fit: BoxFit.cover, errorBuilder: (_, _, _) => fallback())
        : Image.network(cover.url!,
            fit: BoxFit.cover,
            // thumbnails.libretro.com has no CORS headers; an <img> element works anyway.
            webHtmlElementStrategy: WebHtmlElementStrategy.prefer,
            errorBuilder: (_, _, _) => fallback());
    return Stack(fit: StackFit.expand, children: [
      image,
      Positioned(
        left: 8,
        bottom: 8,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
          decoration: BoxDecoration(color: c.color, borderRadius: BorderRadius.circular(6)),
          child: Text(c.name.toUpperCase(),
              style: const TextStyle(color: Colors.white, fontSize: 10, fontWeight: FontWeight.w800, letterSpacing: 0.8)),
        ),
      ),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.game.console;
    final theme = Theme.of(context);
    return AnimatedScale(
      scale: _focused ? 1.05 : 1,
      duration: const Duration(milliseconds: 120),
      child: Material(
        color: theme.colorScheme.surfaceContainerHigh,
        clipBehavior: Clip.antiAlias,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
          side: BorderSide(color: _focused ? c.color : Colors.transparent, width: 2.5),
        ),
        child: InkWell(
          autofocus: widget.autofocus,
          onFocusChange: (f) => setState(() => _focused = f),
          onTap: widget.onPlay,
          onLongPress: widget.onRemove,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Expanded(
                child: Container(
                  decoration: BoxDecoration(
                    gradient: LinearGradient(
                      begin: Alignment.topLeft,
                      end: Alignment.bottomRight,
                      colors: [c.color.withValues(alpha: 0.85), c.color.withValues(alpha: 0.35)],
                    ),
                  ),
                  child: Stack(
                    children: [
                      Positioned.fill(child: _art(c)),
                      Positioned(
                        top: 2,
                        right: 2,
                        child: IconButton(
                          tooltip: 'Remove',
                          iconSize: 18,
                          color: Colors.white,
                          style: IconButton.styleFrom(backgroundColor: Colors.black38),
                          onPressed: widget.onRemove,
                          icon: const Icon(Icons.close_rounded),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
                child: Text(
                  widget.game.title,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: theme.textTheme.titleSmall?.copyWith(fontWeight: FontWeight.w600),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Fallback when a device has no folder picker: type the path instead.
Future<String?> askFolderPath(BuildContext context) {
  final controller = TextEditingController(text: '/storage/emulated/0/ROMs');
  return showDialog<String>(
    context: context,
    builder: (context) => AlertDialog(
      title: const Text('Folder path'),
      content: TextField(
        controller: controller,
        autofocus: true,
        decoration: const InputDecoration(helperText: 'This device has no folder picker. Type the folder\'s path.'),
        onSubmitted: (v) => Navigator.pop(context, v.trim()),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
        FilledButton(onPressed: () => Navigator.pop(context, controller.text.trim()), child: const Text('Add')),
      ],
    ),
  );
}
