import 'package:flutter/material.dart';

import '../models/console.dart';
import '../services/switch_support.dart';

/// Settings. Today it holds the Nintendo Switch emulator choice; the
/// structure leaves room for core options, controls and so on.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final _sw = SwitchSupport.instance;
  EmulatorChoice? _current;
  List<EmulatorChoice> _found = const [];
  bool _scanning = true;

  /// Set when games were added, so the library refreshes on return.
  bool _changed = false;

  @override
  void initState() {
    super.initState();
    _scan();
  }

  Future<void> _scan() async {
    setState(() => _scanning = true);
    final current = await _sw.current();
    var found = const <EmulatorChoice>[];
    if (_sw.supported) {
      try {
        found = await _sw.detect();
      } catch (_) {
        // Detection is best-effort; Browse still works.
      }
    }
    if (!mounted) return;
    setState(() {
      _current = current;
      _found = found;
      _scanning = false;
    });
  }

  Future<void> _pick(EmulatorChoice choice) async {
    await _sw.choose(choice);
    if (mounted) setState(() => _current = choice);
  }

  Future<void> _browse() async {
    final choice = await _sw.browse();
    if (choice != null) await _pick(choice);
  }

  Future<void> _addGames() async {
    try {
      final added = await _sw.addGames();
      if (!mounted) return;
      if (added.isNotEmpty) _changed = true;
      _snack(added.isEmpty
          ? 'No .nsp, .xci or .nro files in that selection'
          : added.length == 1
              ? 'Added ${added.first.title}'
              : 'Added ${added.length} Switch games');
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

    // Current choice first, then everything detected that isn't it.
    final options = <EmulatorChoice>[
      ?_current,
      ..._found.where((f) => f.id != _current?.id),
      if (!_sw.canBrowse && _current?.id != SwitchSupport.chooseEachTime)
        const EmulatorChoice(SwitchSupport.chooseEachTime, 'Ask each time', detail: 'Pick an app from the Open with sheet'),
    ];

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
            Row(children: [
              Container(
                width: 10,
                height: 10,
                decoration: BoxDecoration(color: Console.nswitch.color, shape: BoxShape.circle),
              ),
              const SizedBox(width: 10),
              Text('Nintendo Switch', style: theme.textTheme.titleMedium),
            ]),
            const SizedBox(height: 8),
            if (!_sw.supported)
              Text(_sw.unsupportedReason, style: muted)
            else ...[
              Text(
                'RomRunner doesn\'t emulate Switch itself. It keeps your Switch games in the library and '
                'opens them in a Switch emulator you install and set up separately (Eden, Citron or '
                'Ryujinx), using keys and firmware dumped from your own console.',
                style: muted,
              ),
              const SizedBox(height: 20),
              Text('Emulator', style: theme.textTheme.labelLarge),
              const SizedBox(height: 4),
              Card(
                margin: EdgeInsets.zero,
                child: Column(
                  children: [
                    if (_scanning)
                      const Padding(padding: EdgeInsets.all(16), child: LinearProgressIndicator())
                    else if (options.isEmpty)
                      ListTile(
                        leading: const Icon(Icons.search_off_rounded),
                        title: const Text('No Switch emulator found'),
                        subtitle: Text(_sw.canBrowse
                            ? 'Install Eden, Citron or Ryujinx, then press Browse to point RomRunner at it.'
                            : 'Install Eden or Citron on this device, then press Rescan.'),
                      ),
                    for (final o in options)
                      ListTile(
                        leading: Icon(o.id == _current?.id ? Icons.radio_button_checked : Icons.radio_button_unchecked,
                            color: o.id == _current?.id ? theme.colorScheme.primary : null),
                        title: Text(o.label),
                        subtitle: o.detail == null ? null : Text(o.detail!, maxLines: 2, overflow: TextOverflow.ellipsis),
                        onTap: () => _pick(o),
                      ),
                    OverflowBar(
                      alignment: MainAxisAlignment.end,
                      children: [
                        TextButton.icon(onPressed: _scan, icon: const Icon(Icons.refresh_rounded), label: const Text('Rescan')),
                        if (_sw.canBrowse)
                          TextButton.icon(
                            onPressed: _browse,
                            icon: const Icon(Icons.folder_open_rounded),
                            label: const Text('Browse…'),
                          ),
                      ],
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 20),
              Text('Games', style: theme.textTheme.labelLarge),
              const SizedBox(height: 4),
              Text(
                'Switch games (.nsp, .xci, .nro) are linked where they are, never copied. '
                'Moving or deleting a file removes it from what RomRunner can open.',
                style: muted,
              ),
              const SizedBox(height: 12),
              Align(
                alignment: Alignment.centerLeft,
                child: FilledButton.icon(
                  onPressed: _addGames,
                  icon: const Icon(Icons.add_rounded),
                  label: const Text('Add Switch games'),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
