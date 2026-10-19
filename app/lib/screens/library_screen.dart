import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../input/key_mapping.dart';
import '../models/console.dart';
import '../models/game.dart';
import '../services/library.dart';
import 'game_screen.dart';

class LibraryScreen extends StatefulWidget {
  const LibraryScreen({super.key});

  @override
  State<LibraryScreen> createState() => _LibraryScreenState();
}

class _LibraryScreenState extends State<LibraryScreen> {
  final _library = GameLibrary.create();
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
            for (final c in Console.values)
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
      ];
      _snack(parts.isEmpty ? 'No supported ROMs in that selection' : parts.join(' · '));
      if (kIsWeb && added.length == 1) _play(added.first);
    } catch (e) {
      _snack('Import failed: $e');
    } finally {
      if (mounted) setState(() => _importing = false);
    }
  }

  void _snack(String msg) => ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(msg)));

  Future<void> _play(Game game) async {
    await Navigator.of(context).push(MaterialPageRoute(builder: (_) => GameScreen(game: game)));
  }

  Future<void> _remove(Game game) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Remove game?'),
        content: Text('${game.title} will be removed from the library. Save states are kept.'),
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
              for (final c in Console.values)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 3),
                  child: Text('${c.fullName} — ${c.extensions.map((e) => '.$e').join(' ')} (${c.core})'),
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
        title: const Text('MigPlay', style: TextStyle(fontWeight: FontWeight.w800, letterSpacing: -0.5)),
        actions: [
          IconButton(tooltip: 'Info', onPressed: _showInfo, icon: const Icon(Icons.info_outline_rounded)),
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
              ? _EmptyState(onImport: _import)
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
                          childAspectRatio: 0.82,
                        ),
                        itemCount: visible.length,
                        itemBuilder: (context, i) => _GameCard(
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
  const _EmptyState({required this.onImport});
  final VoidCallback onImport;

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
              children: [for (final c in Console.values) _Dot(color: c.color, size: 14)],
            ),
            const SizedBox(height: 20),
            Text('Your library is empty', style: theme.textTheme.headlineSmall),
            const SizedBox(height: 8),
            Text(
              'Add ROMs and disc images of games you own.\n'
              'NES · Game Boy · GBA · SNES · Genesis · PS1',
              textAlign: TextAlign.center,
              style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant),
            ),
            const SizedBox(height: 24),
            FilledButton.icon(
              autofocus: true,
              onPressed: onImport,
              icon: const Icon(Icons.add_rounded),
              label: const Text('Add ROMs'),
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
  const _GameCard({required this.game, required this.onPlay, required this.onRemove, this.autofocus = false});
  final Game game;
  final VoidCallback onPlay;
  final VoidCallback onRemove;
  final bool autofocus;

  @override
  State<_GameCard> createState() => _GameCardState();
}

class _GameCardState extends State<_GameCard> {
  bool _focused = false;

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
                      Center(
                        child: Text(
                          c.name.toUpperCase(),
                          style: const TextStyle(
                            color: Colors.white,
                            fontWeight: FontWeight.w900,
                            fontSize: 22,
                            letterSpacing: 1,
                          ),
                        ),
                      ),
                      Positioned(
                        top: 2,
                        right: 2,
                        child: IconButton(
                          tooltip: 'Remove',
                          iconSize: 18,
                          color: Colors.white70,
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
