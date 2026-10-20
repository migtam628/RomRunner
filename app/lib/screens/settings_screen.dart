import 'package:flutter/material.dart';

import '../models/console.dart';
import '../services/cover_art.dart';
import '../services/external_catalog.dart';
import '../services/library.dart';
import '../services/external_support.dart';
import '../widgets/install_dialog.dart';
import 'library_screen.dart' show askFolderPath;

/// Settings: game folders, and which emulator app opens each system
/// RomRunner doesn't emulate itself.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final _ext = ExternalSupport.instance;
  final _library = GameLibrary.instance;
  List<String> _folders = const [];
  Map<Console, EmulatorChoice?> _chosen = const {};

  /// Set when games were added, so the library refreshes on return.
  bool _changed = false;

  @override
  void initState() {
    super.initState();
    _loadChoices();
    _loadFolders();
    CoverArt.instance.load().then((_) {
      if (mounted) setState(() {});
    });
  }

  Future<void> _loadFolders() async {
    final f = await _library.folders();
    if (mounted) setState(() => _folders = f);
  }

  Future<void> _addFolder() async {
    final r = await _library.addFolder(askPath: () => askFolderPath(context));
    if (!mounted) return;
    if (r.error != null) {
      _snack(r.error!);
    } else if (r.folder != null) {
      _changed = true;
      _snack('${r.folder}: ${r.found == 1 ? '1 game' : '${r.found} games'}');
      await _loadFolders();
    }
  }

  Future<void> _removeFolder(String path) async {
    await _library.removeFolder(path);
    _changed = true;
    await _loadFolders();
  }

  Future<void> _loadChoices() async {
    final map = <Console, EmulatorChoice?>{};
    for (final c in _ext.systems) {
      map[c] = await _ext.current(c);
    }
    if (!mounted) return;
    // Pre-select the only emulator found for systems nothing is set for.
    for (final c in _ext.systems.where((c) => map[c] == null)) {
      try {
        final found = await _ext.detect(c);
        if (found.length == 1) {
          await _ext.choose(c, found.first);
          map[c] = found.first;
        }
      } catch (_) {}
    }
    if (mounted) setState(() => _chosen = map);
  }

  Future<void> _pickFor(Console c) async {
    final choice = await showModalBottomSheet<EmulatorChoice>(
      context: context,
      showDragHandle: true,
      isScrollControlled: true,
      builder: (_) => _EmulatorPicker(console: c, current: _chosen[c]),
    );
    if (choice == null) return;
    await _ext.choose(c, choice);
    if (mounted) setState(() => _chosen = {..._chosen, c: choice});
  }

  Future<void> _addGames() async {
    try {
      final added = await _ext.addGames();
      if (!mounted) return;
      if (added.isNotEmpty) _changed = true;
      _snack(added.isEmpty
          ? 'Nothing in that selection belongs to a system listed here'
          : added.length == 1
              ? 'Added ${added.first.title} (${added.first.console.name})'
              : 'Added ${added.length} games');
    } catch (e) {
      _snack('Could not add games: $e');
    }
  }

  void _snack(String msg) => ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(msg)));

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final muted = theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant);
    final systems = _ext.systems;
    final unavailable = _ext.unavailable;

    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) {
        if (!didPop) Navigator.of(context).pop(_changed);
      },
      child: Scaffold(
        appBar: AppBar(title: const Text('Settings')),
        body: ListView(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
          children: [
            Card(
              margin: EdgeInsets.zero,
              child: SwitchListTile(
                secondary: const Icon(Icons.image_outlined),
                title: const Text('Cover art'),
                subtitle: const Text('Box art from the libretro thumbnails collection, matched by game name'),
                value: CoverArt.instance.enabled,
                onChanged: (on) async {
                  await CoverArt.instance.setEnabled(on);
                  _changed = true;
                  if (mounted) setState(() {});
                },
              ),
            ),
            const SizedBox(height: 28),
            if (_library.supportsFolders) ...[
              Text('Game folders', style: theme.textTheme.titleMedium),
              const SizedBox(height: 8),
              Text(
                _library.foldersAreLinked
                    ? 'Games in these folders (and their subfolders) appear in the library and stay where they '
                        'are. RomRunner rescans them each time the library opens. Disc images (.iso, .bin) are '
                        'recognised by their contents; if one isn\'t, put it in a folder named after its system, '
                        'such as "PS2" or "Genesis".'
                    : 'In the browser, picking a folder imports its games for this session only.',
                style: muted,
              ),
              const SizedBox(height: 8),
              Card(
                margin: EdgeInsets.zero,
                child: Column(
                  children: [
                    for (final f in _folders)
                      ListTile(
                        leading: const Icon(Icons.folder_outlined),
                        title: Text(f, maxLines: 2, overflow: TextOverflow.ellipsis),
                        trailing: IconButton(
                          tooltip: 'Remove folder',
                          icon: const Icon(Icons.close_rounded),
                          onPressed: () => _removeFolder(f),
                        ),
                      ),
                    if (_folders.isEmpty && _library.foldersAreLinked)
                      const ListTile(leading: Icon(Icons.folder_off_outlined), title: Text('No folders yet')),
                    OverflowBar(
                      alignment: MainAxisAlignment.end,
                      children: [
                        TextButton.icon(
                          onPressed: _addFolder,
                          icon: const Icon(Icons.create_new_folder_outlined),
                          label: const Text('Add folder'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 28),
            ],
            Text('Other systems', style: theme.textTheme.titleMedium),
            const SizedBox(height: 8),
            if (systems.isEmpty)
              Text(
                unavailable.isEmpty ? '' : _ext.unsupportedReason(unavailable.first).replaceFirst(unavailable.first.fullName, 'These'),
                style: muted,
              )
            else ...[
              Text(
                'RomRunner opens these systems in an emulator app you install separately, set up with BIOS, keys '
                'or firmware from your own console. Their games are linked where they are, never copied.',
                style: muted,
              ),
              const SizedBox(height: 8),
              Card(
                margin: EdgeInsets.zero,
                child: Column(
                  children: [
                    for (final c in systems)
                      ListTile(
                        leading: CircleAvatar(radius: 6, backgroundColor: c.color),
                        title: Text(c.fullName),
                        subtitle: Text(_chosen[c]?.label ?? 'Choose emulator…',
                            style: _chosen[c] == null ? TextStyle(color: theme.colorScheme.primary) : null),
                        trailing: const Icon(Icons.chevron_right_rounded),
                        onTap: () => _pickFor(c),
                      ),
                  ],
                ),
              ),
              const SizedBox(height: 12),
              Align(
                alignment: Alignment.centerLeft,
                child: FilledButton.icon(
                  onPressed: _addGames,
                  icon: const Icon(Icons.add_rounded),
                  label: const Text('Add games without copying'),
                ),
              ),
            ],
            if (unavailable.isNotEmpty && systems.isNotEmpty) ...[
              const SizedBox(height: 16),
              Text('Not available on this device: ${unavailable.map((c) => c.name).join(', ')}.', style: muted),
            ],
          ],
        ),
      ),
    );
  }
}

/// Bottom sheet: pick the emulator app for one system.
class _EmulatorPicker extends StatefulWidget {
  const _EmulatorPicker({required this.console, this.current});
  final Console console;
  final EmulatorChoice? current;

  @override
  State<_EmulatorPicker> createState() => _EmulatorPickerState();
}

class _EmulatorPickerState extends State<_EmulatorPicker> {
  final _ext = ExternalSupport.instance;
  List<EmulatorChoice>? _found;

  @override
  void initState() {
    super.initState();
    _scan();
  }

  Future<void> _scan() async {
    setState(() => _found = null);
    List<EmulatorChoice> found;
    try {
      found = await _ext.detect(widget.console);
    } catch (_) {
      found = const [];
    }
    if (mounted) setState(() => _found = found);
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final c = widget.console;
    final current = widget.current;
    final options = <EmulatorChoice>[
      ?current,
      ...?_found?.where((f) => f.id != current?.id),
      if (!_ext.canBrowse && current?.id != ExternalSupport.chooseEachTime)
        const EmulatorChoice(ExternalSupport.chooseEachTime, 'Ask each time', detail: 'Pick an app from the Open with sheet'),
    ];
    final names = emulatorNames(c, _ext.host);
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(c.fullName, style: theme.textTheme.titleLarge),
            const SizedBox(height: 4),
            Text('Works with: $names', style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
            const SizedBox(height: 12),
            if (_found == null)
              const Padding(padding: EdgeInsets.all(16), child: LinearProgressIndicator())
            else if (options.isEmpty)
              ListTile(
                leading: const Icon(Icons.search_off_rounded),
                title: const Text('No emulator found'),
                subtitle: Text(_ext.installable(c).isNotEmpty
                    ? 'Download one below, or install $names yourself and press Rescan.'
                    : _ext.canBrowse
                        ? 'Install $names, then press Rescan, or Browse to point RomRunner at it.'
                        : 'Install $names on this device, then press Rescan.'),
              ),
            for (final o in options)
              ListTile(
                leading: Icon(o.id == current?.id ? Icons.radio_button_checked : Icons.radio_button_unchecked,
                    color: o.id == current?.id ? theme.colorScheme.primary : null),
                title: Text(o.label),
                subtitle: o.detail == null ? null : Text(o.detail!, maxLines: 2, overflow: TextOverflow.ellipsis),
                onTap: () => Navigator.pop(context, o),
              ),
            for (final e in _ext.installable(c))
              if (!options.any((o) => o.emulator == e.id && (o.detail ?? '').contains('RomRunner')))
                ListTile(
                  leading: const Icon(Icons.download_rounded),
                  title: Text('Download ${e.name}'),
                  subtitle: Text('Latest official release${_ext.homepage(e).isEmpty ? '' : ' · ${_ext.homepage(e)}'}'),
                  onTap: () async {
                    final result = await showInstallDialog(context, e, c);
                    if (!context.mounted) return;
                    if (result is EmulatorChoice) {
                      Navigator.pop(context, result);
                    } else if (result == true) {
                      _scan(); // Android installer opened; pick it up after it's installed
                    }
                  },
                ),
            if (consoleFilesNote(c) case final note?)
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
                child: Text(note, style: theme.textTheme.bodySmall),
              ),
            OverflowBar(
              alignment: MainAxisAlignment.end,
              children: [
                TextButton.icon(onPressed: _scan, icon: const Icon(Icons.refresh_rounded), label: const Text('Rescan')),
                if (_ext.canBrowse)
                  TextButton.icon(
                    onPressed: () async {
                      final choice = await _ext.browse(c);
                      if (choice != null && context.mounted) Navigator.pop(context, choice);
                    },
                    icon: const Icon(Icons.folder_open_rounded),
                    label: const Text('Browse…'),
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
