import 'package:flutter/material.dart';

import '../services/ai_help.dart';

/// Settings → AI help: the API key, model, web search, the on-screen button
/// and an optional custom endpoint.
class AiSettingsSection extends StatefulWidget {
  const AiSettingsSection({super.key});

  @override
  State<AiSettingsSection> createState() => _AiSettingsSectionState();
}

class _AiSettingsSectionState extends State<AiSettingsSection> {
  AiSettings? _s;

  @override
  void initState() {
    super.initState();
    AiSettings.load().then((s) {
      if (mounted) setState(() => _s = s);
    });
  }

  Future<void> _update(void Function(AiSettings s) change) async {
    final s = _s!;
    change(s);
    await s.save();
    if (mounted) setState(() {});
  }

  Future<String?> _edit(String title, String initial, {String? hint, bool secret = false, String? help}) {
    final c = TextEditingController(text: initial);
    return showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(title),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextField(
              controller: c,
              autofocus: true,
              obscureText: secret,
              autocorrect: false,
              decoration: InputDecoration(hintText: hint, border: const OutlineInputBorder()),
              onSubmitted: (v) => Navigator.of(ctx).pop(v),
            ),
            if (help != null) ...[
              const SizedBox(height: 8),
              Text(help, style: Theme.of(ctx).textTheme.bodySmall),
            ],
          ],
        ),
        actions: [
          TextButton(onPressed: () => Navigator.of(ctx).pop(), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.of(ctx).pop(c.text), child: const Text('Save')),
        ],
      ),
    );
  }

  static String _masked(String key) =>
      key.length <= 12 ? '•' * key.length : '${key.substring(0, 7)}…${key.substring(key.length - 4)}';

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = _s;
    if (s == null) return const SizedBox.shrink();
    final muted = theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('AI help', style: theme.textTheme.titleMedium),
        const SizedBox(height: 8),
        Text(
          'Stuck in a game? Tap ✨ (or press F2) and Claude looks at the paused screen and tells you where to go '
          'or how to get past it, checking walkthroughs on the web. Uses your own Anthropic API key, '
          'which stays on this device; each question costs a fraction of a cent to a few cents.',
          style: muted,
        ),
        const SizedBox(height: 8),
        Card(
          margin: EdgeInsets.zero,
          child: Column(
            children: [
              ListTile(
                leading: const Icon(Icons.key_rounded),
                title: const Text('Anthropic API key'),
                subtitle: Text(s.apiKey.isEmpty ? 'Not set: get one at console.anthropic.com' : _masked(s.apiKey)),
                trailing: s.apiKey.isEmpty
                    ? null
                    : IconButton(
                        tooltip: 'Remove key',
                        icon: const Icon(Icons.delete_outline),
                        onPressed: () => _update((s) => s.apiKey = ''),
                      ),
                onTap: () async {
                  final v = await _edit('Anthropic API key', '', hint: 'sk-ant-…', secret: true);
                  if (v != null && v.trim().isNotEmpty) await _update((s) => s.apiKey = v.trim());
                },
              ),
              ListTile(
                leading: const Icon(Icons.psychology_outlined),
                title: const Text('Model'),
                subtitle: Text(AiSettings.modelName(s.model)),
                onTap: () async {
                  final id = await showModalBottomSheet<String>(
                    context: context,
                    showDragHandle: true,
                    builder: (ctx) => SafeArea(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          for (final m in AiSettings.models)
                            ListTile(
                              title: Text(m.$2),
                              subtitle: Text(m.$3),
                              trailing: m.$1 == s.model ? const Icon(Icons.check_rounded) : null,
                              onTap: () => Navigator.of(ctx).pop(m.$1),
                            ),
                        ],
                      ),
                    ),
                  );
                  if (id != null) await _update((s) => s.model = id);
                },
              ),
              SwitchListTile(
                secondary: const Icon(Icons.travel_explore_rounded),
                title: const Text('Search the web for guides'),
                subtitle: const Text('More accurate for specific games; answers list their sources'),
                value: s.webSearch,
                onChanged: (v) => _update((s) => s.webSearch = v),
              ),
              SwitchListTile(
                secondary: const Icon(Icons.auto_awesome),
                title: const Text('Show the help button while playing'),
                subtitle: const Text('Help is also in the pause menu'),
                value: s.showButton,
                onChanged: (v) => _update((s) => s.showButton = v),
              ),
              ListTile(
                leading: const Icon(Icons.dns_outlined),
                title: const Text('Custom endpoint (advanced)'),
                subtitle: Text(s.endpoint.isEmpty ? 'Anthropic API' : s.endpoint),
                onTap: () async {
                  final v = await _edit('Custom endpoint', s.endpoint,
                      hint: 'https://…/v1/messages',
                      help: 'A server that speaks the Anthropic Messages API, e.g. your own proxy that adds the '
                          'API key so it doesn\'t have to be on each device. Leave empty for Anthropic.');
                  if (v != null) await _update((s) => s.endpoint = v.trim());
                },
              ),
            ],
          ),
        ),
      ],
    );
  }
}
