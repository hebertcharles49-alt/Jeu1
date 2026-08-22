import 'package:flutter/material.dart';

import '../core/app_state.dart';
import '../creatures/creatures.dart';
import '../games/game_framework.dart';
import 'collection_screen.dart';
import 'pixel_art.dart';
import 'settings_screen.dart';
import 'zone_screen.dart';

/// Écran d'accueil : la carte du monde avec les trois zones.
class WorldMapScreen extends StatelessWidget {
  const WorldMapScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Color(0xFFDFF3FF), Color(0xFFEAF7EC)],
          ),
        ),
        child: SafeArea(
          child: ListenableBuilder(
            listenable: AppState.instance,
            builder: (context, _) {
              final app = AppState.instance;
              return SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  children: [
                    const SizedBox(height: 8),
                    const Text(
                      'SCIENSTERS',
                      style: TextStyle(
                        fontSize: 40,
                        fontWeight: FontWeight.w900,
                        letterSpacing: 4,
                        color: Color(0xFF2E5E8C),
                      ),
                    ),
                    const Text(
                      'Aide le Professeur Pixel à retrouver ses créatures !',
                      textAlign: TextAlign.center,
                      style: TextStyle(fontSize: 16, color: Colors.blueGrey),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const Icon(Icons.star_rounded,
                            color: Color(0xFFFFC93D), size: 28),
                        Text(
                          ' ${app.totalLevelsDone} / 15 niveaux réussis',
                          style: const TextStyle(
                              fontSize: 18, fontWeight: FontWeight.bold),
                        ),
                      ],
                    ),
                    const SizedBox(height: 16),
                    for (final zone in zones) _zoneCard(context, zone),
                    const SizedBox(height: 12),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                      children: [
                        _bigButton(
                          context,
                          emoji: '📔',
                          label: 'Scienxdex',
                          color: const Color(0xFFE07A5F),
                          onTap: () => Navigator.of(context).push(
                            MaterialPageRoute<void>(
                                builder: (_) => const CollectionScreen()),
                          ),
                        ),
                        _bigButton(
                          context,
                          emoji: '⚙️',
                          label: 'Réglages',
                          color: const Color(0xFF78909C),
                          onTap: () => Navigator.of(context).push(
                            MaterialPageRoute<void>(
                                builder: (_) => const SettingsScreen()),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                  ],
                ),
              );
            },
          ),
        ),
      ),
    );
  }

  Widget _zoneCard(BuildContext context, Zone zone) {
    final app = AppState.instance;
    final creature = creatureForZone(zone.id);
    final stage = app.stageForZone(zone.id);
    final captured = stage > 0;
    return GestureDetector(
      onTap: () {
        softFeedback();
        Navigator.of(context).push(
          MaterialPageRoute<void>(builder: (_) => ZoneScreen(zone: zone)),
        );
      },
      child: Container(
        margin: const EdgeInsets.only(bottom: 14),
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(24),
          border: Border.all(color: zone.color, width: 3),
          boxShadow: const [
            BoxShadow(
                color: Colors.black12, blurRadius: 6, offset: Offset(0, 3))
          ],
        ),
        child: Row(
          children: [
            Text(zone.emoji, style: const TextStyle(fontSize: 44)),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    zone.name,
                    style: TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.bold,
                        color: zone.color),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: List.generate(
                      AppState.maxLevel,
                      (i) => Icon(
                        i < app.levelReached(zone.id)
                            ? Icons.star_rounded
                            : Icons.star_border_rounded,
                        color: const Color(0xFFFFC93D),
                        size: 22,
                      ),
                    ),
                  ),
                ],
              ),
            ),
            PixelArt(
              grid: creature.stageInfo(captured ? stage : 1).grid,
              palette: creature.palette,
              size: 56,
              silhouette: !captured,
            ),
          ],
        ),
      ),
    );
  }

  Widget _bigButton(
    BuildContext context, {
    required String emoji,
    required String label,
    required Color color,
    required VoidCallback onTap,
  }) {
    return GestureDetector(
      onTap: () {
        softFeedback();
        onTap();
      },
      child: Container(
        width: 150,
        padding: const EdgeInsets.symmetric(vertical: 14),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(20),
          border: Border.all(color: color, width: 3),
        ),
        child: Column(
          children: [
            Text(emoji, style: const TextStyle(fontSize: 34)),
            const SizedBox(height: 4),
            Text(label,
                style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.bold,
                    color: color)),
          ],
        ),
      ),
    );
  }
}
