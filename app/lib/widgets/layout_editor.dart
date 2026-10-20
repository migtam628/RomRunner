import 'package:flutter/material.dart';

import '../models/console.dart';
import '../models/control_layout.dart';

/// Floating toolbar shown while editing the touch layout: what's selected,
/// its size, hide/show, layout-wide settings, reset and done.
class LayoutEditorBar extends StatefulWidget {
  const LayoutEditorBar({
    super.key,
    required this.console,
    required this.layout,
    required this.selected,
    required this.onChanged,
    required this.onResetAll,
    required this.onDone,
  });

  final Console console;
  final ControlLayout layout;
  final String? selected;
  final VoidCallback onChanged;
  final VoidCallback onResetAll;
  final VoidCallback onDone;

  @override
  State<LayoutEditorBar> createState() => _LayoutEditorBarState();
}

class _LayoutEditorBarState extends State<LayoutEditorBar> {
  bool _atBottom = false;

  ControlLayout get _l => widget.layout;

  String get _title {
    final id = widget.selected;
    if (id == null) return 'Drag to move · pinch to resize';
    if (id == screenId) return 'Game screen';
    return controlSpecs(widget.console).where((s) => s.id == id).firstOrNull?.name ?? id;
  }

  void _changed() {
    widget.onChanged();
    setState(() {});
  }

  Future<void> _settings() async {
    await showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      backgroundColor: const Color(0xF0181A20),
      builder: (context) => StatefulBuilder(builder: (context, set) {
        void update(void Function() f) {
          f();
          set(() {});
          _changed();
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(20, 0, 20, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Controls', style: Theme.of(context).textTheme.titleMedium),
                _SliderRow(
                  icon: Icons.opacity_rounded,
                  label: 'Opacity',
                  value: _l.opacity,
                  min: 0.15,
                  max: 1,
                  text: '${(_l.opacity * 100).round()}%',
                  onChanged: (v) => update(() => _l.opacity = v),
                ),
                _SliderRow(
                  icon: Icons.zoom_out_map_rounded,
                  label: 'Button size',
                  value: _l.size,
                  min: 0.6,
                  max: 1.6,
                  text: '${(_l.size * 100).round()}%',
                  onChanged: (v) => update(() => _l.size = v),
                ),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  secondary: const Icon(Icons.palette_outlined),
                  title: const Text('Classic button colours'),
                  value: _l.colored,
                  onChanged: (v) => update(() => _l.colored = v),
                ),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  secondary: const Icon(Icons.vibration_rounded),
                  title: const Text('Vibrate on press'),
                  value: _l.haptics,
                  onChanged: (v) => update(() => _l.haptics = v),
                ),
                const Divider(height: 24),
                Text('Screen', style: Theme.of(context).textTheme.titleMedium),
                const SizedBox(height: 8),
                _FitPicker(layout: _l, onChanged: () => update(() {})),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  secondary: const Icon(Icons.blur_on_rounded),
                  title: const Text('Smooth picture'),
                  subtitle: const Text('Off keeps pixels sharp'),
                  value: _l.smooth,
                  onChanged: (v) => update(() => _l.smooth = v),
                ),
              ],
            ),
          ),
        );
      }),
    );
  }

  void _resetSelected() {
    final id = widget.selected;
    if (id == screenId) {
      _l.resetScreen();
    } else if (id != null) {
      _l.items.remove(id);
    }
    _changed();
  }

  @override
  Widget build(BuildContext context) {
    final id = widget.selected;
    final isScreen = id == screenId;
    final control = id != null && !isScreen;
    final theme = Theme.of(context);

    final bar = ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 560),
      child: Material(
        color: const Color(0xE6181A20),
        elevation: 8,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(18),
          side: BorderSide(color: widget.console.color.withValues(alpha: 0.5)),
        ),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(14, 6, 6, 6),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Row(
                children: [
                  Icon(Icons.open_with_rounded, size: 18, color: widget.console.color),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(_title, style: theme.textTheme.titleSmall, overflow: TextOverflow.ellipsis),
                  ),
                  IconButton(
                    tooltip: _atBottom ? 'Move bar to top' : 'Move bar to bottom',
                    icon: Icon(_atBottom ? Icons.vertical_align_top_rounded : Icons.vertical_align_bottom_rounded),
                    onPressed: () => setState(() => _atBottom = !_atBottom),
                  ),
                  IconButton(tooltip: 'Settings', icon: const Icon(Icons.tune_rounded), onPressed: _settings),
                  PopupMenuButton<String>(
                    tooltip: 'Reset',
                    icon: const Icon(Icons.restart_alt_rounded),
                    onSelected: (v) => v == 'all' ? widget.onResetAll() : _resetSelected(),
                    itemBuilder: (_) => [
                      if (id != null) const PopupMenuItem(value: 'one', child: Text('Reset selected')),
                      const PopupMenuItem(value: 'all', child: Text('Reset whole layout')),
                    ],
                  ),
                  const SizedBox(width: 4),
                  FilledButton(onPressed: widget.onDone, child: const Text('Done')),
                ],
              ),
              if (control)
                Row(
                  children: [
                    Expanded(
                      child: _SliderRow(
                        icon: Icons.photo_size_select_small_rounded,
                        label: 'Size',
                        value: _l.scaleOf(id),
                        min: 0.5,
                        max: 2.2,
                        text: '${(_l.scaleOf(id) * 100).round()}%',
                        onChanged: (v) {
                          _l.items[id]?.scale = v;
                          _changed();
                        },
                      ),
                    ),
                    IconButton(
                      tooltip: _l.hiddenOf(id) ? 'Show' : 'Hide',
                      icon: Icon(_l.hiddenOf(id) ? Icons.visibility_off_rounded : Icons.visibility_rounded),
                      onPressed: () {
                        final p = _l.items[id];
                        if (p != null) p.hidden = !p.hidden;
                        _changed();
                      },
                    ),
                  ],
                ),
              if (isScreen) ...[
                _SliderRow(
                  icon: Icons.aspect_ratio_rounded,
                  label: 'Size',
                  value: _l.fit == ScreenFit.fit ? _l.screenScale : 1,
                  min: ControlLayout.minScreenScale,
                  max: 1,
                  text: _l.fit == ScreenFit.fit ? '${(_l.screenScale * 100).round()}%' : _l.fit.name,
                  onChanged: (v) {
                    _l.fit = ScreenFit.fit;
                    _l.screenScale = v;
                    _changed();
                  },
                ),
                Padding(
                  padding: const EdgeInsets.only(right: 8, bottom: 4),
                  child: _FitPicker(layout: _l, onChanged: _changed),
                ),
              ],
            ],
          ),
        ),
      ),
    );

    return SafeArea(
      child: Align(
        alignment: _atBottom ? Alignment.bottomCenter : Alignment.topCenter,
        child: Padding(padding: const EdgeInsets.all(8), child: bar),
      ),
    );
  }
}

class _FitPicker extends StatelessWidget {
  const _FitPicker({required this.layout, required this.onChanged});
  final ControlLayout layout;
  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    return SegmentedButton<ScreenFit>(
      showSelectedIcon: false,
      segments: const [
        ButtonSegment(value: ScreenFit.fit, label: Text('Fit'), icon: Icon(Icons.fit_screen_rounded)),
        ButtonSegment(value: ScreenFit.integer, label: Text('Pixel'), icon: Icon(Icons.grid_on_rounded)),
        ButtonSegment(value: ScreenFit.stretch, label: Text('Stretch'), icon: Icon(Icons.open_in_full_rounded)),
      ],
      selected: {layout.fit},
      onSelectionChanged: (s) {
        layout.fit = s.first;
        onChanged();
      },
    );
  }
}

class _SliderRow extends StatelessWidget {
  const _SliderRow({
    required this.icon,
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.text,
    required this.onChanged,
  });

  final IconData icon;
  final String label;
  final double value, min, max;
  final String text;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 18, color: Colors.white70),
        const SizedBox(width: 8),
        SizedBox(width: 84, child: Text(label)),
        Expanded(child: Slider(value: value.clamp(min, max), min: min, max: max, onChanged: onChanged)),
        SizedBox(width: 60, child: Text(text, textAlign: TextAlign.end, style: const TextStyle(color: Colors.white70))),
      ],
    );
  }
}
