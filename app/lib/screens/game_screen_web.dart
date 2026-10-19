import 'dart:convert';
import 'dart:js_interop';
import 'dart:ui_web' as ui_web;

import 'package:flutter/material.dart';
import 'package:web/web.dart' as web;

import '../models/game.dart';

/// Web build: runs the same libretro cores compiled to WebAssembly through
/// EmulatorJS, inside an iframe. EmulatorJS handles input, gamepads, audio,
/// save states and its own in-game menu.
class GameScreen extends StatefulWidget {
  const GameScreen({super.key, required this.game});
  final Game game;

  @override
  State<GameScreen> createState() => _GameScreenState();
}

class _GameScreenState extends State<GameScreen> {
  static const _ejsData = 'https://cdn.emulatorjs.org/stable/data/';
  static int _nextId = 0;

  late final String _viewType = 'migplay-ejs-${_nextId++}';
  String? _blobUrl;

  @override
  void initState() {
    super.initState();
    final bytes = widget.game.bytes!;
    final blob = web.Blob([bytes.toJS].toJS, web.BlobPropertyBag(type: 'application/octet-stream'));
    _blobUrl = web.URL.createObjectURL(blob);

    final html = _page(
      core: widget.game.console.ejsCore,
      url: _blobUrl!,
      name: widget.game.fileName,
    );
    ui_web.platformViewRegistry.registerViewFactory(_viewType, (int _) {
      final frame = web.HTMLIFrameElement()
        ..allow = 'gamepad; autoplay; fullscreen'
        ..srcdoc = html.toJS;
      frame.style
        ..border = 'none'
        ..width = '100%'
        ..height = '100%';
      return frame;
    });
  }

  /// The srcdoc iframe shares this page's origin, so it can read the blob URL.
  String _page({required String core, required String url, required String name}) => '''
<!doctype html>
<html><head><meta charset="utf-8">
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}#game{width:100%;height:100%}</style>
</head><body><div id="game"></div>
<script>
  EJS_player = '#game';
  EJS_core = ${jsonEncode(core)};
  EJS_gameUrl = ${jsonEncode(url)};
  EJS_gameName = ${jsonEncode(name)};
  EJS_pathtodata = ${jsonEncode(_ejsData)};
  EJS_startOnLoaded = true;
  EJS_color = '#7C5CFF';
</script>
<script src="${_ejsData}loader.js"></script>
</body></html>''';

  @override
  void dispose() {
    if (_blobUrl != null) web.URL.revokeObjectURL(_blobUrl!);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(
        children: [
          Positioned.fill(child: HtmlElementView(viewType: _viewType)),
          SafeArea(
            child: Padding(
              padding: const EdgeInsets.all(8),
              child: IconButton.filledTonal(
                tooltip: 'Back to library',
                icon: const Icon(Icons.arrow_back_rounded),
                onPressed: () => Navigator.of(context).pop(),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
