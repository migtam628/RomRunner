import 'package:flutter/material.dart';

import '../models/console.dart';

enum PauseAction { resume, help, saveState, loadState, reset, toggleFastForward, toggleTouch, editControls, quit }

class PauseResult {
  const PauseResult(this.action, this.slot);
  final PauseAction action;
  final int slot;
}

/// In-game menu. Every control is focusable so it works with a TV remote.
class PauseMenu extends StatefulWidget {
  const PauseMenu({
    super.key,
    required this.title,
    required this.console,
    required this.slot,
    this.saved = const {},
    required this.fastForward,
    required this.touchVisible,
    required this.showTouchToggle,
  });

  final String title;
  final Console console;
  final int slot;

  /// Slots that hold a save state, with when each was saved.
  final Map<int, DateTime> saved;
  final bool fastForward;
  final bool touchVisible;
  final bool showTouchToggle;

  @override
  State<PauseMenu> createState() => _PauseMenuState();
}

class _PauseMenuState extends State<PauseMenu> {
  late int _slot = widget.slot;

  void _done(PauseAction a) => Navigator.of(context).pop(PauseResult(a, _slot));

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    Widget item(IconData icon, String label, PauseAction action, {bool autofocus = false, String? subtitle, bool enabled = true}) => ListTile(
          autofocus: autofocus,
          enabled: enabled,
          leading: Icon(icon),
          title: Text(label),
          subtitle: subtitle == null ? null : Text(subtitle),
          onTap: () => _done(action),
        );
    final savedAt = widget.saved[_slot];

    return Dialog(
      insetPadding: const EdgeInsets.all(24),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 420),
        child: SingleChildScrollView(
          padding: const EdgeInsets.symmetric(vertical: 16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(24, 4, 24, 12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(widget.console.name.toUpperCase(),
                        style: theme.textTheme.labelSmall?.copyWith(color: widget.console.color, letterSpacing: 1.5)),
                    const SizedBox(height: 4),
                    Text(widget.title, style: theme.textTheme.titleLarge, maxLines: 2, overflow: TextOverflow.ellipsis),
                  ],
                ),
              ),
              item(Icons.play_arrow_rounded, 'Resume', PauseAction.resume, autofocus: true),
              item(Icons.auto_awesome, 'Ask for help (AI)', PauseAction.help),
              const Divider(),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 4),
                child: Row(
                  children: [
                    Text('State slot', style: theme.textTheme.bodyMedium),
                    const Spacer(),
                    for (var s = 1; s <= 4; s++)
                      Padding(
                        padding: const EdgeInsets.only(left: 6),
                        child: ChoiceChip(
                          // A dot marks slots that hold a state.
                          avatar: widget.saved.containsKey(s)
                              ? Icon(Icons.circle, size: 8, color: theme.colorScheme.primary)
                              : null,
                          label: Text('$s'),
                          selected: _slot == s,
                          onSelected: (_) => setState(() => _slot = s),
                        ),
                      ),
                  ],
                ),
              ),
              item(Icons.save_rounded, 'Save state', PauseAction.saveState,
                  subtitle: savedAt != null ? 'Replaces the state in slot $_slot' : null),
              item(Icons.restore_rounded, 'Load state', PauseAction.loadState,
                  enabled: savedAt != null,
                  subtitle: savedAt != null ? 'Saved ${_ago(savedAt)}' : 'Slot $_slot is empty'),
              const Divider(),
              item(Icons.fast_forward_rounded, widget.fastForward ? 'Fast-forward: on' : 'Fast-forward: off',
                  PauseAction.toggleFastForward),
              if (widget.showTouchToggle)
                item(Icons.videogame_asset_rounded, widget.touchVisible ? 'Hide touch controls' : 'Show touch controls',
                    PauseAction.toggleTouch),
              if (widget.showTouchToggle) item(Icons.dashboard_customize_rounded, 'Edit controls & screen', PauseAction.editControls),
              item(Icons.replay_rounded, 'Reset game', PauseAction.reset),
              item(Icons.logout_rounded, 'Quit to library', PauseAction.quit),
            ],
          ),
        ),
      ),
    );
  }
}

String _ago(DateTime t) {
  final d = DateTime.now().difference(t);
  if (d.inMinutes < 1) return 'just now';
  if (d.inHours < 1) return '${d.inMinutes} min ago';
  if (d.inDays < 1) return '${d.inHours} h ago';
  if (d.inDays < 7) return '${d.inDays} days ago';
  return '${t.year}-${t.month.toString().padLeft(2, '0')}-${t.day.toString().padLeft(2, '0')}';
}
