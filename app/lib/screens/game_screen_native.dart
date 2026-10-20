import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:romrunner_core/romrunner_core.dart';

import '../input/key_mapping.dart';
import '../models/console.dart';
import '../models/control_layout.dart';
import '../models/game.dart';
import '../services/control_store.dart';
import '../services/core_manager.dart';
import '../services/paths.dart';
import '../widgets/layout_editor.dart';
import '../widgets/pause_menu.dart';
import '../widgets/touch_controller.dart';

class GameScreen extends StatefulWidget {
  const GameScreen({super.key, required this.game});
  final Game game;

  @override
  State<GameScreen> createState() => _GameScreenState();
}

class _GameScreenState extends State<GameScreen> with SingleTickerProviderStateMixin, WidgetsBindingObserver {
  final _core = RomRunnerCore.instance;
  final _focus = FocusNode(debugLabel: 'game');
  Ticker? _ticker;

  ui.Image? _image;
  int _counter = -1;
  bool _decoding = false;
  double _aspect = 4 / 3;

  bool _running = false;
  bool _menuOpen = false;
  bool _fastForward = false;
  String? _error;
  double? _downloadProgress;

  int _keyMask = 0;
  int _touchMask = 0;
  // ROMRUNNER_TOUCH=1 shows the touch pad on desktop (for trying layouts).
  static final _touchDevice =
      Platform.isAndroid || Platform.isIOS || Platform.environment['ROMRUNNER_TOUCH'] == '1';
  bool _touchVisible = _touchDevice;
  int _slot = 1;

  final _mobile = _touchDevice;
  ControlLayout _portrait = ControlLayout();
  ControlLayout _landscape = ControlLayout();
  bool _editing = false;
  String? _selected;

  Console get _console => widget.game.console;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    // Full screen while playing: the status and navigation bars come back
    // with a swipe from the edge.
    if (Platform.isAndroid || Platform.isIOS) SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    _loadLayouts();
    _boot();
  }

  Future<void> _boot() async {
    try {
      final paths = await AppPaths.get();
      final manager = CoreManager.instance;
      var corePath = await manager.locate(_console);
      if (corePath == null && manager.canDownload) {
        setState(() => _downloadProgress = 0);
        corePath = await manager.download(_console, onProgress: (v) {
          if (mounted) setState(() => _downloadProgress = v);
        });
      }
      if (corePath == null) {
        throw RomRunnerException(
          'The ${_console.core} core is not bundled with this build.\n'
          'Run tool/fetch_cores.sh for this platform and rebuild.',
        );
      }
      if (!mounted) return;
      _core.setDirectories(system: paths.system, saves: paths.saves);
      // iOS forbids JIT, so the PS1 core must use its interpreter there.
      if (Platform.isIOS && _console == Console.ps1) _core.setOption('pcsx_rearmed_drc', 'disabled');
      _core.loadCore(corePath);
      _core.loadGame(widget.game.path!);
      _core.start();
      final a = _core.aspectRatio;
      setState(() {
        _running = true;
        _downloadProgress = null;
        if (a > 0) _aspect = a;
      });
      _ticker = createTicker(_onTick)..start();
    } catch (e) {
      _core.unload();
      if (mounted) setState(() => _error = e.toString());
    }
  }

  Future<void> _loadLayouts() async {
    final store = ControlStore.instance;
    final p = await store.load(_console, true);
    final l = await store.load(_console, false);
    if (mounted) {
      setState(() {
        _portrait = p;
        _landscape = l;
      });
    }
  }

  void _startEditing() {
    _core.paused = true;
    setState(() {
      _editing = true;
      _touchVisible = true;
      _selected = null;
    });
  }

  Future<void> _stopEditing() async {
    setState(() {
      _editing = false;
      _selected = null;
    });
    await ControlStore.instance.save(_console, true, _portrait);
    await ControlStore.instance.save(_console, false, _landscape);
    _core.paused = false;
    _focus.requestFocus();
  }

  void _onTick(Duration _) {
    if (_decoding || !_running) return;
    final frame = _core.copyFrame(_counter);
    if (frame == null) return;
    _counter = frame.counter;
    _decoding = true;
    ui.decodeImageFromPixels(frame.pixels, frame.width, frame.height, ui.PixelFormat.rgba8888, (img) {
      _decoding = false;
      if (!mounted) {
        img.dispose();
        return;
      }
      final old = _image;
      final a = _core.aspectRatio;
      setState(() {
        _image = img;
        if (a > 0 && (a - _aspect).abs() > 0.01) _aspect = a;
      });
      old?.dispose();
    });
  }

  void _pushInput() => _core.setButtons(_keyMask | _touchMask);

  KeyEventResult _onKey(FocusNode node, KeyEvent event) {
    if (_editing) return KeyEventResult.handled;
    final key = event.logicalKey;
    final action = actionKeys[key];
    if (action != null) {
      if (event is KeyDownEvent) {
        switch (action) {
          case KeyAction.openMenu:
            _openMenu();
          case KeyAction.toggleFastForward:
            _setFastForward(!_fastForward);
        }
      }
      return KeyEventResult.handled;
    }
    final pad = padKeys[key];
    if (pad == null) return KeyEventResult.ignored;
    if (event is KeyDownEvent || event is KeyRepeatEvent) {
      _keyMask |= pad.bit;
      // A physical controller/remote is in use: get the touch pad out of the way.
      if (_touchVisible && event is KeyDownEvent) setState(() => _touchVisible = false);
    } else if (event is KeyUpEvent) {
      _keyMask &= ~pad.bit;
    }
    _pushInput();
    return KeyEventResult.handled;
  }

  void _setFastForward(bool on) {
    _core.fastForward = on;
    setState(() => _fastForward = on);
  }

  Future<void> _openMenu() async {
    if (_menuOpen || !_running) return;
    _menuOpen = true;
    _keyMask = 0;
    _touchMask = 0;
    _pushInput();
    _core.paused = true;
    final result = await showDialog<PauseResult>(
      context: context,
      barrierColor: Colors.black.withValues(alpha: 0.6),
      builder: (_) => PauseMenu(
        title: widget.game.title,
        console: _console,
        slot: _slot,
        fastForward: _fastForward,
        touchVisible: _touchVisible,
        showTouchToggle: _mobile,
      ),
    );
    _menuOpen = false;
    if (!mounted) return;
    if (result != null) _slot = result.slot;
    final paths = await AppPaths.get();
    final statePath = paths.statePath(widget.game.id, _slot);
    try {
      switch (result?.action) {
        case PauseAction.saveState:
          _core.saveState(statePath);
          _toast('Saved to slot $_slot');
        case PauseAction.loadState:
          _core.loadState(statePath);
          _toast('Loaded slot $_slot');
        case PauseAction.reset:
          _core.reset();
        case PauseAction.toggleFastForward:
          _setFastForward(!_fastForward);
        case PauseAction.toggleTouch:
          setState(() => _touchVisible = !_touchVisible);
        case PauseAction.editControls:
          _startEditing();
          return; // stays paused until Done
        case PauseAction.quit:
          _quit();
          return;
        case PauseAction.resume || null:
          break;
      }
    } on RomRunnerException catch (e) {
      _toast(e.message);
    }
    _core.paused = false;
    _focus.requestFocus();
  }

  void _quit() {
    _shutdown();
    Navigator.of(context).pop();
  }

  void _toast(String msg) {
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(msg), duration: const Duration(seconds: 2)));
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (!_running) return;
    if (state == AppLifecycleState.paused || state == AppLifecycleState.hidden) {
      _core.paused = true;
      _core.flushSram();
    } else if (state == AppLifecycleState.resumed && !_menuOpen) {
      _core.paused = false;
    }
  }

  void _shutdown() {
    _ticker?.dispose();
    _ticker = null;
    if (_running) {
      _running = false;
      _core.unload();
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _shutdown();
    if (Platform.isAndroid || Platform.isIOS) SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    _image?.dispose();
    _focus.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) {
        if (didPop) return;
        if (_editing) {
          _stopEditing();
        } else if (_running) {
          _openMenu();
        } else {
          _quit();
        }
      },
      child: Scaffold(
        backgroundColor: Colors.black,
        body: Focus(
          focusNode: _focus,
          autofocus: true,
          onKeyEvent: _onKey,
          child: Stack(
            children: [
              Positioned.fill(child: SafeArea(child: LayoutBuilder(builder: (context, box) => _play(box.biggest)))),
              if (_running && (!_touchVisible || _layout(MediaQuery.sizeOf(context)).hiddenOf(menuId)) && !_editing)
                SafeArea(
                  child: Align(
                    alignment: Alignment.topRight,
                    child: Padding(
                      padding: const EdgeInsets.all(8),
                      child: IconButton.filledTonal(
                        tooltip: 'Menu',
                        icon: const Icon(Icons.pause_rounded),
                        onPressed: _openMenu,
                      ),
                    ),
                  ),
                ),
              if (_running && _fastForward && !_editing)
                const SafeArea(
                  child: Align(
                    alignment: Alignment.topCenter,
                    child: Padding(
                      padding: EdgeInsets.only(top: 8),
                      child: Chip(label: Text('⏩ 4×'), visualDensity: VisualDensity.compact),
                    ),
                  ),
                ),
              if (_editing)
                LayoutEditorBar(
                  console: _console,
                  layout: _layout(MediaQuery.sizeOf(context)),
                  selected: _selected,
                  onChanged: () => setState(() {}),
                  onResetAll: () => setState(() {
                    final fresh = ControlLayout();
                    if (MediaQuery.sizeOf(context).height > MediaQuery.sizeOf(context).width) {
                      _portrait = fresh;
                    } else {
                      _landscape = fresh;
                    }
                    _selected = null;
                  }),
                  onDone: _stopEditing,
                ),
              if (!_running && _error == null) _LoadingOverlay(console: _console, progress: _downloadProgress),
              if (_error != null) _ErrorOverlay(message: _error!, onBack: _quit),
            ],
          ),
        ),
      ),
    );
  }

  ControlLayout _layout(Size area) => area.height > area.width ? _portrait : _landscape;

  /// The game picture, the DS touch layer and the touch pad, all placed in
  /// the same [area] so the editor's coordinates line up.
  Widget _play(Size area) {
    final layout = _layout(area);
    final portrait = area.height > area.width;
    final pad = _touchVisible && _running && _mobile || _editing;
    final rect = layout.screenRect(area, _aspect, _image?.height ?? 0, centered: !pad);
    return Stack(
      children: [
        // Behind the pad: a soft gradient (below the picture in portrait,
        // at the sides in landscape).
        if (pad)
          Positioned.fill(
            child: DecoratedBox(
              decoration: BoxDecoration(
                gradient: portrait
                    ? LinearGradient(
                        begin: Alignment.topCenter,
                        end: Alignment.bottomCenter,
                        colors: [Colors.black, Color.lerp(const Color(0xFF14161C), _console.color, 0.06)!],
                      )
                    : RadialGradient(
                        radius: 1.2,
                        colors: [Colors.black, Color.lerp(const Color(0xFF14161C), _console.color, 0.06)!],
                      ),
              ),
            ),
          ),
        Positioned.fromRect(
          rect: rect,
          child: DecoratedBox(
            decoration: BoxDecoration(
              boxShadow: pad
                  ? [BoxShadow(color: _console.color.withValues(alpha: 0.18), blurRadius: 24, spreadRadius: 1)]
                  : null,
            ),
            child: ColoredBox(
              color: Colors.black,
              child: CustomPaint(painter: _FramePainter(_image, smooth: layout.smooth)),
            ),
          ),
        ),
        if (_touchVisible && _running && _mobile || _editing)
          Positioned.fill(
            child: TouchController(
              console: _console,
              layout: layout,
              screenRect: rect,
              fastForward: _fastForward,
              onChanged: (mask) {
                _touchMask = mask;
                _pushInput();
              },
              onMenu: _openMenu,
              onFastForward: () => _setFastForward(!_fastForward),
              editing: _editing,
              selected: _selected,
              onSelect: (id) => setState(() => _selected = id),
              onEdited: () => setState(() {}),
            ),
          ),
        // DS touchscreen: taps and drags on the game image become pointer
        // input. Sits above the touch pad and covers only the frame.
        if (_running && _console.touchscreen && !_editing)
          Positioned.fromRect(
            rect: rect,
            child: Listener(
              behavior: HitTestBehavior.opaque,
              onPointerDown: (e) => _sendPointer(e.localPosition, rect.size, true),
              onPointerMove: (e) => _sendPointer(e.localPosition, rect.size, true),
              onPointerUp: (e) => _sendPointer(e.localPosition, rect.size, false),
              onPointerCancel: (e) => _sendPointer(e.localPosition, rect.size, false),
              child: const SizedBox.expand(),
            ),
          ),
      ],
    );
  }

  void _sendPointer(Offset pos, Size size, bool down) =>
      _core.setPointer((pos.dx / size.width).clamp(0.0, 1.0), (pos.dy / size.height).clamp(0.0, 1.0), down);
}

/// Draws the emulator frame with nearest-neighbour scaling (crisp pixels).
class _FramePainter extends CustomPainter {
  _FramePainter(this.image, {this.smooth = false});
  final ui.Image? image;
  final bool smooth;

  @override
  void paint(Canvas canvas, Size size) {
    final img = image;
    if (img == null) return;
    canvas.drawImageRect(
      img,
      Rect.fromLTWH(0, 0, img.width.toDouble(), img.height.toDouble()),
      Offset.zero & size,
      Paint()..filterQuality = smooth ? FilterQuality.medium : FilterQuality.none,
    );
  }

  @override
  bool shouldRepaint(_FramePainter old) => old.image != image || old.smooth != smooth;
}

class _LoadingOverlay extends StatelessWidget {
  const _LoadingOverlay({required this.console, this.progress});
  final Console console;
  final double? progress;

  @override
  Widget build(BuildContext context) {
    final downloading = progress != null;
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            width: 220,
            child: LinearProgressIndicator(value: downloading && progress! > 0 ? progress : null, color: console.color),
          ),
          const SizedBox(height: 16),
          Text(
            downloading ? 'Downloading ${console.name} core…' : 'Starting ${console.name}…',
            style: const TextStyle(color: Colors.white70),
          ),
        ],
      ),
    );
  }
}

class _ErrorOverlay extends StatelessWidget {
  const _ErrorOverlay({required this.message, required this.onBack});
  final String message;
  final VoidCallback onBack;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.error_outline_rounded, color: Colors.redAccent, size: 48),
            const SizedBox(height: 12),
            Text(message, textAlign: TextAlign.center, style: const TextStyle(color: Colors.white)),
            const SizedBox(height: 20),
            FilledButton(autofocus: true, onPressed: onBack, child: const Text('Back to library')),
          ],
        ),
      ),
    );
  }
}
