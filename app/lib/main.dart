import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'screens/library_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MigPlayApp());
}

class MigPlayApp extends StatelessWidget {
  const MigPlayApp({super.key});

  @override
  Widget build(BuildContext context) {
    final scheme = ColorScheme.fromSeed(
      seedColor: const Color(0xFF7C5CFF),
      brightness: Brightness.dark,
      surface: const Color(0xFF0F1015),
    );
    return MaterialApp(
      title: 'MigPlay',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: scheme,
        scaffoldBackgroundColor: scheme.surface,
        appBarTheme: AppBarTheme(backgroundColor: scheme.surface, scrolledUnderElevation: 0),
        // Visible focus rings for TV remotes and keyboards.
        focusColor: scheme.primary.withValues(alpha: 0.24),
      ),
      // TV remotes and gamepads activate the focused item.
      shortcuts: {
        ...WidgetsApp.defaultShortcuts,
        const SingleActivator(LogicalKeyboardKey.select): const ActivateIntent(),
        const SingleActivator(LogicalKeyboardKey.gameButtonA): const ActivateIntent(),
        const SingleActivator(LogicalKeyboardKey.gameButtonB): const DismissIntent(),
      },
      home: const LibraryScreen(),
    );
  }
}
