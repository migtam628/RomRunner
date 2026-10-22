import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';

import '../services/updater.dart';
import 'help_sheet.dart' show SimpleMarkdown;

/// "RomRunner x.y.z is available": the release notes and one button that
/// downloads and installs (Android) or downloads (desktop / iOS) the build
/// for this device.
class UpdateDialog extends StatefulWidget {
  const UpdateDialog({super.key, required this.release, required this.current});

  final AppRelease release;
  final String current;

  static Future<void> show(BuildContext context, AppRelease release) async {
    final current = await Updater.instance.currentVersion();
    if (!context.mounted) return;
    await showDialog<void>(context: context, builder: (_) => UpdateDialog(release: release, current: current));
  }

  @override
  State<UpdateDialog> createState() => _UpdateDialogState();
}

class _UpdateDialogState extends State<UpdateDialog> {
  final _updater = Updater.instance;
  bool _busy = false;
  double? _progress;
  String? _status;
  String? _error;

  ReleaseFile? get _file => _updater.fileFor(widget.release);

  Future<void> _update() async {
    final f = _file;
    if (f == null) {
      await launchUrl(Uri.parse(widget.release.page), mode: LaunchMode.externalApplication);
      return;
    }
    final android = _updater.platform.isAndroid;
    setState(() {
      _busy = true;
      _error = null;
      _progress = null;
      _status = android ? 'Downloading ${_size(f.size)}…' : 'Opening the download…';
    });
    try {
      final outcome = await _updater.install(f, onProgress: (v) {
        if (mounted) setState(() => _progress = v);
      });
      if (!mounted) return;
      switch (outcome) {
        case UpdateOutcome.installerOpened:
          setState(() => _status = 'Tap Install. RomRunner restarts on the new version; your games, saves and settings stay.');
        case UpdateOutcome.needsPermission:
          setState(() => _status = 'Allow RomRunner to install apps in the screen that opened, then come back and tap Update again.');
        case UpdateOutcome.downloaded:
          setState(() => _status = _desktopHint());
      }
    } on UpdateException catch (e) {
      if (mounted) setState(() => _error = e.message);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  String _desktopHint() => switch (_updater.platform) {
        UpdatePlatform.macos => 'Downloading in your browser. Unzip it and move RomRunner.app to Applications, replacing the old one.',
        UpdatePlatform.windows => 'Downloading in your browser. Unzip it over your RomRunner folder (or anywhere) and run RomRunner.exe.',
        UpdatePlatform.linux => 'Downloading in your browser. Extract it over your RomRunner folder and run romrunner.',
        UpdatePlatform.ios => 'Downloading the IPA. Install it with Sideloadly or AltStore as before.',
        _ => 'Downloading in your browser.',
      };

  static String _size(int bytes) => bytes <= 0 ? '' : '(${(bytes / (1024 * 1024)).toStringAsFixed(0)} MB)';

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final r = widget.release;
    final notes = r.whatsNew;
    return AlertDialog(
      icon: Icon(Icons.system_update_rounded, color: theme.colorScheme.primary),
      title: Text('RomRunner ${r.version} is available'),
      content: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 480, maxHeight: 420),
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('You have ${widget.current}.', style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
              const SizedBox(height: 12),
              if (notes.isNotEmpty) ...[
                Text('What\'s new', style: theme.textTheme.titleSmall),
                const SizedBox(height: 6),
                SimpleMarkdown(notes.length > 4000 ? '${notes.substring(0, 4000)}…' : notes),
                const SizedBox(height: 4),
              ],
              TextButton.icon(
                style: TextButton.styleFrom(padding: EdgeInsets.zero, visualDensity: VisualDensity.compact),
                onPressed: () => launchUrl(Uri.parse(r.page), mode: LaunchMode.externalApplication),
                icon: const Icon(Icons.open_in_new_rounded, size: 16),
                label: const Text('Release page'),
              ),
              if (_busy || _status != null) ...[
                const SizedBox(height: 16),
                if (_busy) LinearProgressIndicator(value: _progress),
                if (_status != null) ...[
                  const SizedBox(height: 8),
                  Text(_status!, style: theme.textTheme.bodyMedium),
                ],
              ],
              if (_error != null) ...[
                const SizedBox(height: 12),
                Text(_error!, style: TextStyle(color: theme.colorScheme.error)),
              ],
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: _busy
              ? null
              : () async {
                  await _updater.skip(r.version);
                  if (context.mounted) Navigator.of(context).pop();
                },
          child: const Text('Skip this version'),
        ),
        TextButton(onPressed: () => Navigator.of(context).pop(), child: const Text('Later')),
        FilledButton.icon(
          autofocus: true,
          onPressed: _busy ? null : _update,
          icon: Icon(_file == null ? Icons.open_in_new_rounded : Icons.download_rounded),
          label: Text(_file == null ? 'Open release page' : _updater.platform.isAndroid ? 'Update' : 'Download'),
        ),
      ],
    );
  }
}
