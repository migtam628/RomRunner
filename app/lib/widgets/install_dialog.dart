import 'dart:async';

import 'package:flutter/foundation.dart' show defaultTargetPlatform, TargetPlatform;
import 'package:flutter/material.dart';

import '../models/console.dart';
import '../services/external_catalog.dart';
import '../services/external_support.dart';

/// Asks to download an emulator, then shows progress. Pops with the new
/// [EmulatorChoice] (desktop), `true` when Android's installer was opened,
/// or null if cancelled or failed.
Future<Object?> showInstallDialog(BuildContext context, ExternalEmulator e, Console c) => showDialog<Object?>(
      context: context,
      barrierDismissible: false,
      builder: (_) => _InstallDialog(emulator: e, console: c),
    );

class _InstallDialog extends StatefulWidget {
  const _InstallDialog({required this.emulator, required this.console});
  final ExternalEmulator emulator;
  final Console console;

  @override
  State<_InstallDialog> createState() => _InstallDialogState();
}

class _InstallDialogState extends State<_InstallDialog> {
  final _ext = ExternalSupport.instance;
  bool _busy = false;
  String _status = '';
  double? _progress;
  String? _error;

  bool get _android => defaultTargetPlatform == TargetPlatform.android;
  bool _builtIn = false;

  @override
  void initState() {
    super.initState();
    _ext.hasBuiltIn(widget.console).then((v) {
      if (mounted) setState(() => _builtIn = v);
    });
  }

  Future<void> _useBuiltIn() async {
    final choice = ExternalSupport.builtInChoice(widget.console);
    await _ext.choose(widget.console, choice);
    if (mounted) Navigator.pop(context, choice);
  }

  Future<void> _install() async {
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      final choice = await _ext.install(widget.emulator, widget.console, onProgress: (status, fraction) {
        if (mounted) {
          setState(() {
            _status = status;
            _progress = fraction;
          });
        }
      });
      if (mounted) Navigator.pop(context, choice ?? true);
    } catch (e) {
      if (mounted) {
        setState(() {
          _busy = false;
          _error = e.toString();
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final e = widget.emulator;
    final note = consoleFilesNote(widget.console);
    final home = _ext.homepage(e);
    return AlertDialog(
      title: Text('Install ${e.name}?'),
      content: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 420),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              '${widget.console.fullName} games open in ${e.name}. RomRunner will download the latest official '
              'release${home.isEmpty ? '' : ' from $home'}${_android ? ' and open Android\'s installer — tap Install' : ' and set it up for you'}.',
            ),
            if (note != null) ...[
              const SizedBox(height: 12),
              Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Icon(Icons.info_outline_rounded, size: 18, color: theme.colorScheme.primary),
                const SizedBox(width: 8),
                Expanded(child: Text(note, style: theme.textTheme.bodySmall)),
              ]),
            ],
            if (_busy) ...[
              const SizedBox(height: 16),
              LinearProgressIndicator(value: _progress),
              const SizedBox(height: 8),
              Text(_status, style: theme.textTheme.bodySmall),
            ],
            if (_error != null) ...[
              const SizedBox(height: 12),
              Text(_error!, style: TextStyle(color: theme.colorScheme.error)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(onPressed: _busy ? null : () => Navigator.pop(context), child: const Text('Cancel')),
        if (_builtIn)
          TextButton(
            onPressed: _busy ? null : _useBuiltIn,
            child: Text('Try RomRunner ${widget.console.name} (experimental)'),
          ),
        FilledButton(
          autofocus: true,
          onPressed: _busy ? null : _install,
          child: Text(_error == null ? 'Install' : 'Try again'),
        ),
      ],
    );
  }
}
