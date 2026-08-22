import 'package:flutter/material.dart';

import 'core/app_state.dart';
import 'ui/world_map_screen.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppState.instance.load();
  runApp(const ScienstersApp());
}

class ScienstersApp extends StatelessWidget {
  const ScienstersApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Sciensters',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF5C9CE6)),
        scaffoldBackgroundColor: const Color(0xFFF4F9FD),
      ),
      home: const WorldMapScreen(),
    );
  }
}
