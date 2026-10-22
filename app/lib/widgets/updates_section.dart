import 'package:flutter/material.dart';

import '../services/updater.dart';
import 'update_dialog.dart';

/// Settings → Updates: this version, "Check for updates", automatic checks.
class UpdatesSection extends StatefulWidget {
  const UpdatesSection({super.key});

  @override
  State<UpdatesSection> createState() => _UpdatesSectionState();
}

class _UpdatesSectionState extends State<UpdatesSection> {
  final _u = Updater.instance;
  String _version = '';
  bool _auto = true;
  bool _checking = false;
  String? _result;

  @override
  void initState() {
    super.initState();
    _u.currentVersion().then((v) {
      if (mounted) setState(() => _version = v);
    });
    _u.autoCheck().then((v) {
      if (mounted) setState(() => _auto = v);
    });
  }

  Future<void> _check() async {
    setState(() {
      _checking = true;
      _result = null;
    });
    try {
      final r = await _u.check();
      if (!mounted) return;
      if (r == null) {
        setState(() => _result = 'You have the latest version.');
      } else {
        setState(() => _result = null);
        await UpdateDialog.show(context, r);
      }
    } on UpdateException catch (e) {
      if (mounted) setState(() => _result = e.message);
    } finally {
      if (mounted) setState(() => _checking = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('Updates', style: theme.textTheme.titleMedium),
        const SizedBox(height: 8),
        Card(
          margin: EdgeInsets.zero,
          child: Column(
            children: [
              ListTile(
                leading: const Icon(Icons.system_update_rounded),
                title: Text(_version.isEmpty ? 'RomRunner' : 'RomRunner $_version'),
                subtitle: Text(_result ??
                    (_u.platform.isAndroid
                        ? 'Updates download and install in the app'
                        : 'Updates download from GitHub releases')),
                trailing: _checking
                    ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                    : FilledButton.tonal(onPressed: _check, child: const Text('Check for updates')),
              ),
              SwitchListTile(
                secondary: const Icon(Icons.update_rounded),
                title: const Text('Check automatically'),
                subtitle: const Text('When the library opens, at most twice a day'),
                value: _auto,
                onChanged: (v) async {
                  await _u.setAutoCheck(v);
                  if (mounted) setState(() => _auto = v);
                },
              ),
            ],
          ),
        ),
      ],
    );
  }
}
