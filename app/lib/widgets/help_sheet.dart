import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';

import '../services/ai_help.dart';

/// The in-game help panel: the paused game's screenshot, a question box with
/// quick questions, and Claude's answers with their sources. The game stays
/// paused while it is open.
class HelpSheet extends StatefulWidget {
  const HelpSheet({super.key, required this.session, required this.screenshot});

  final AiHelpSession session;

  /// The game screen right now (PNG); sent with the next question.
  final Uint8List? screenshot;

  static Future<void> show(BuildContext context, {required AiHelpSession session, Uint8List? screenshot}) {
    return showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      useSafeArea: true,
      showDragHandle: true,
      backgroundColor: Theme.of(context).colorScheme.surfaceContainer,
      constraints: const BoxConstraints(maxWidth: 720),
      builder: (_) => FractionallySizedBox(
        heightFactor: 0.88,
        child: HelpSheet(session: session, screenshot: screenshot),
      ),
    );
  }

  @override
  State<HelpSheet> createState() => _HelpSheetState();
}

class _HelpSheetState extends State<HelpSheet> {
  final _input = TextEditingController();
  final _scroll = ScrollController();
  final _inputFocus = FocusNode();
  late bool _attach = widget.screenshot != null;
  bool _busy = false;
  String? _error;
  AiSettings? _settings;

  static const _quick = [
    'Where do I go next?',
    'How do I beat this?',
    'What should I do here?',
    'How do I solve this puzzle?',
  ];

  @override
  void initState() {
    super.initState();
    AiSettings.load().then((s) {
      if (mounted) setState(() => _settings = s);
    });
  }

  @override
  void dispose() {
    _input.dispose();
    _scroll.dispose();
    _inputFocus.dispose();
    super.dispose();
  }

  Future<void> _ask(String q) async {
    final question = q.trim();
    if (question.isEmpty || _busy) return;
    setState(() {
      _busy = true;
      _error = null;
    });
    _input.clear();
    _toBottom();
    try {
      await widget.session.ask(question, screenshot: _attach ? widget.screenshot : null);
      // The screenshot went with this question; follow-ups refer to it.
      _attach = false;
    } on AiHelpException catch (e) {
      _error = e.message;
    } catch (e) {
      _error = 'Couldn\'t reach Claude: ${e.toString().split('\n').first}';
    }
    if (!mounted) return;
    setState(() => _busy = false);
    _toBottom();
  }

  void _toBottom() => WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scroll.hasClients) {
          _scroll.animateTo(_scroll.position.maxScrollExtent,
              duration: const Duration(milliseconds: 250), curve: Curves.easeOut);
        }
      });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = _settings;
    if (s == null) return const Center(child: CircularProgressIndicator());
    if (!s.configured) return _Setup(onSaved: () => setState(() {}));

    final turns = widget.session.turns;
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 0, 12, 8),
          child: Row(
            children: [
              Icon(Icons.auto_awesome, color: theme.colorScheme.primary),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Game help', style: theme.textTheme.titleMedium),
                    Text('${widget.session.title} · ${widget.session.console.name}',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
                  ],
                ),
              ),
              IconButton(
                tooltip: 'Back to the game',
                icon: const Icon(Icons.close_rounded),
                onPressed: () => Navigator.of(context).pop(),
              ),
            ],
          ),
        ),
        const Divider(height: 1),
        Expanded(
          child: ListView(
            controller: _scroll,
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
            children: [
              for (final t in turns) t.fromUser ? _UserBubble(turn: t) : _Answer(turn: t),
              if (_busy) const _Thinking(),
              if (_error != null)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  child: Card(
                    color: theme.colorScheme.errorContainer,
                    child: Padding(
                      padding: const EdgeInsets.all(12),
                      child: Text(_error!, style: TextStyle(color: theme.colorScheme.onErrorContainer)),
                    ),
                  ),
                ),
              if (turns.isEmpty && !_busy) ...[
                Text(
                  _attach
                      ? 'Claude will see this screen. Ask anything, or pick a question.'
                      : 'Ask anything about this game.',
                  style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant),
                ),
                const SizedBox(height: 12),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [for (final q in _quick) ActionChip(label: Text(q), onPressed: () => _ask(q))],
                ),
              ],
            ],
          ),
        ),
        _Composer(
          controller: _input,
          focus: _inputFocus,
          busy: _busy,
          screenshot: _attach ? widget.screenshot : null,
          canAttach: widget.screenshot != null,
          onToggleAttach: () => setState(() => _attach = !_attach),
          onSend: () => _ask(_input.text),
        ),
      ],
    );
  }
}

class _Composer extends StatelessWidget {
  const _Composer({
    required this.controller,
    required this.focus,
    required this.busy,
    required this.screenshot,
    required this.canAttach,
    required this.onToggleAttach,
    required this.onSend,
  });

  final TextEditingController controller;
  final FocusNode focus;
  final bool busy;
  final Uint8List? screenshot;
  final bool canAttach;
  final VoidCallback onToggleAttach;
  final VoidCallback onSend;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: EdgeInsets.fromLTRB(12, 4, 12, 12 + MediaQuery.viewInsetsOf(context).bottom),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          if (canAttach)
            Tooltip(
              message: screenshot != null ? 'Screenshot attached (tap to remove)' : 'Attach the game screen',
              child: InkWell(
                borderRadius: BorderRadius.circular(10),
                onTap: onToggleAttach,
                child: Container(
                  width: 56,
                  height: 44,
                  margin: const EdgeInsets.only(right: 8, bottom: 2),
                  decoration: BoxDecoration(
                    borderRadius: BorderRadius.circular(10),
                    border: Border.all(
                        color: screenshot != null ? theme.colorScheme.primary : theme.colorScheme.outlineVariant,
                        width: screenshot != null ? 2 : 1),
                  ),
                  clipBehavior: Clip.antiAlias,
                  child: screenshot != null
                      ? Image.memory(screenshot!, fit: BoxFit.cover, filterQuality: FilterQuality.none)
                      : Icon(Icons.add_photo_alternate_outlined, color: theme.colorScheme.onSurfaceVariant),
                ),
              ),
            ),
          Expanded(
            child: TextField(
              controller: controller,
              focusNode: focus,
              enabled: !busy,
              minLines: 1,
              maxLines: 4,
              textInputAction: TextInputAction.send,
              onSubmitted: (_) => onSend(),
              decoration: InputDecoration(
                hintText: 'Where am I stuck?',
                filled: true,
                isDense: true,
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(22), borderSide: BorderSide.none),
                contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
              ),
            ),
          ),
          const SizedBox(width: 8),
          IconButton.filled(
            tooltip: 'Ask',
            onPressed: busy ? null : onSend,
            icon: const Icon(Icons.arrow_upward_rounded),
          ),
        ],
      ),
    );
  }
}

class _UserBubble extends StatelessWidget {
  const _UserBubble({required this.turn});
  final AiTurn turn;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Align(
      alignment: Alignment.centerRight,
      child: Padding(
        padding: const EdgeInsets.only(left: 48, bottom: 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            if (turn.image != null)
              Padding(
                padding: const EdgeInsets.only(bottom: 6),
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(12),
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 220, maxHeight: 180),
                    child: Image.memory(turn.image!, filterQuality: FilterQuality.none),
                  ),
                ),
              ),
            DecoratedBox(
              decoration: BoxDecoration(
                color: theme.colorScheme.primaryContainer,
                borderRadius: BorderRadius.circular(18),
              ),
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                child: Text(turn.text, style: TextStyle(color: theme.colorScheme.onPrimaryContainer)),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _Answer extends StatelessWidget {
  const _Answer({required this.turn});
  final AiTurn turn;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 16, right: 16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SimpleMarkdown(turn.text),
          if (turn.sources.isNotEmpty) ...[
            const SizedBox(height: 8),
            Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (final s in turn.sources.take(5))
                  ActionChip(
                    avatar: const Icon(Icons.link_rounded, size: 16),
                    label: Text(_short(s), overflow: TextOverflow.ellipsis),
                    labelStyle: theme.textTheme.labelSmall,
                    visualDensity: VisualDensity.compact,
                    onPressed: () => launchUrl(Uri.parse(s.url), mode: LaunchMode.externalApplication),
                  ),
              ],
            ),
          ],
        ],
      ),
    );
  }

  static String _short(AiSource s) {
    final host = Uri.tryParse(s.url)?.host.replaceFirst('www.', '') ?? '';
    return host.isNotEmpty ? host : s.title;
  }
}

class _Thinking extends StatelessWidget {
  const _Thinking();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        children: [
          const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)),
          const SizedBox(width: 12),
          Text('Looking at your game…', style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        ],
      ),
    );
  }
}

/// Shown until an API key is set: paste it right here.
class _Setup extends StatefulWidget {
  const _Setup({required this.onSaved});
  final VoidCallback onSaved;

  @override
  State<_Setup> createState() => _SetupState();
}

class _SetupState extends State<_Setup> {
  final _key = TextEditingController();

  @override
  void dispose() {
    _key.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    final s = await AiSettings.load();
    s.apiKey = _key.text.trim();
    await s.save();
    widget.onSaved();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ListView(
      padding: EdgeInsets.fromLTRB(24, 8, 24, 24 + MediaQuery.viewInsetsOf(context).bottom),
      children: [
        Icon(Icons.auto_awesome, size: 40, color: theme.colorScheme.primary),
        const SizedBox(height: 12),
        Text('Get help when you\'re stuck', style: theme.textTheme.titleLarge, textAlign: TextAlign.center),
        const SizedBox(height: 8),
        Text(
          'Claude looks at your game screen and tells you where to go or how to get past it, '
          'checking walkthroughs on the web. It uses your own Anthropic API key '
          '(create one at console.anthropic.com); the key stays on this device.',
          style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant),
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 20),
        TextField(
          controller: _key,
          obscureText: true,
          autocorrect: false,
          decoration: const InputDecoration(labelText: 'Anthropic API key', hintText: 'sk-ant-…', border: OutlineInputBorder()),
          onSubmitted: (_) => _save(),
        ),
        const SizedBox(height: 12),
        FilledButton(onPressed: _save, child: const Text('Save and continue')),
        const SizedBox(height: 8),
        TextButton(
          onPressed: () => launchUrl(Uri.parse('https://console.anthropic.com/settings/keys'), mode: LaunchMode.externalApplication),
          child: const Text('Get an API key'),
        ),
      ],
    );
  }
}

/// Enough Markdown for game tips: headings, bullet and numbered lists,
/// **bold** and *italic*.
class SimpleMarkdown extends StatelessWidget {
  const SimpleMarkdown(this.text, {super.key});
  final String text;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final body = theme.textTheme.bodyMedium!.copyWith(height: 1.4);
    final children = <Widget>[];
    for (final raw in text.split('\n')) {
      final line = raw.trimRight();
      if (line.trim().isEmpty) {
        if (children.isNotEmpty) children.add(const SizedBox(height: 8));
        continue;
      }
      final heading = RegExp(r'^(#{1,6})\s+(.*)$').firstMatch(line);
      final bullet = RegExp(r'^\s*[-*•]\s+(.*)$').firstMatch(line);
      final number = RegExp(r'^\s*(\d+)[.)]\s+(.*)$').firstMatch(line);
      if (heading != null) {
        children.add(Padding(
          padding: const EdgeInsets.only(top: 4, bottom: 2),
          child: Text.rich(_inline(heading.group(2)!, theme.textTheme.titleSmall!.copyWith(fontWeight: FontWeight.w700))),
        ));
      } else if (bullet != null || number != null) {
        final marker = number != null ? '${number.group(1)}.' : '•';
        final content = number != null ? number.group(2)! : bullet!.group(1)!;
        children.add(Padding(
          padding: const EdgeInsets.only(left: 4, bottom: 4),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              SizedBox(width: 24, child: Text(marker, style: body.copyWith(fontWeight: FontWeight.w600))),
              Expanded(child: Text.rich(_inline(content, body))),
            ],
          ),
        ));
      } else {
        children.add(Padding(
          padding: const EdgeInsets.only(bottom: 2),
          child: Text.rich(_inline(line, body)),
        ));
      }
    }
    return SelectionArea(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: children),
    );
  }

  static TextSpan _inline(String s, TextStyle style) {
    final spans = <TextSpan>[];
    final re = RegExp(r'\*\*(.+?)\*\*|\*(.+?)\*');
    var last = 0;
    for (final m in re.allMatches(s)) {
      if (m.start > last) spans.add(TextSpan(text: s.substring(last, m.start)));
      if (m.group(1) != null) {
        spans.add(TextSpan(text: m.group(1), style: const TextStyle(fontWeight: FontWeight.w700)));
      } else {
        spans.add(TextSpan(text: m.group(2), style: const TextStyle(fontStyle: FontStyle.italic)));
      }
      last = m.end;
    }
    if (last < s.length) spans.add(TextSpan(text: s.substring(last)));
    return TextSpan(style: style, children: spans);
  }
}
