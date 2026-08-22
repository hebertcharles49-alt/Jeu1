import 'package:flutter/material.dart';

import '../core/app_state.dart';
import '../creatures/creatures.dart';
import '../games/game_framework.dart';
import 'pixel_art.dart';

/// Le Scienxdex : la collection de créatures capturées.
class CollectionScreen extends StatelessWidget {
  const CollectionScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFFFDF3EC),
      appBar: AppBar(
        title: const Text('📔 Scienxdex',
            style: TextStyle(fontWeight: FontWeight.bold)),
        backgroundColor: const Color(0xFFE07A5F),
        foregroundColor: Colors.white,
      ),
      body: SafeArea(
        child: ListenableBuilder(
          listenable: AppState.instance,
          builder: (context, _) {
            final app = AppState.instance;
            final capturedCount = creatures
                .where((c) =>
                    app.stageForZone(zoneForGame(c.gameId).id) > 0)
                .length;
            return SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  Text(
                    '$capturedCount / ${creatures.length} Sciensters capturés',
                    style: const TextStyle(
                        fontSize: 19, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 16),
                  for (final creature in creatures)
                    _creatureCard(context, creature),
                ],
              ),
            );
          },
        ),
      ),
    );
  }

  Widget _creatureCard(BuildContext context, Creature creature) {
    final app = AppState.instance;
    final zone = zoneForGame(creature.gameId);
    final stage = app.stageForZone(zone.id);
    final captured = stage > 0;
    final info = creature.stageInfo(captured ? stage : 1);

    return GestureDetector(
      onTap: captured
          ? () {
              softFeedback();
              showDialog<void>(
                context: context,
                builder: (context) => AlertDialog(
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(24)),
                  title: Text('${zone.emoji}  ${info.name}',
                      textAlign: TextAlign.center),
                  content: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      PixelArt(
                          grid: info.grid,
                          palette: creature.palette,
                          size: 150),
                      const SizedBox(height: 12),
                      Text(info.description,
                          textAlign: TextAlign.center,
                          style: const TextStyle(fontSize: 16, height: 1.3)),
                      const SizedBox(height: 10),
                      Text(
                        creature.funFact,
                        textAlign: TextAlign.center,
                        style: TextStyle(
                            fontSize: 14,
                            fontStyle: FontStyle.italic,
                            color: Colors.grey.shade700,
                            height: 1.3),
                      ),
                    ],
                  ),
                  actions: [
                    TextButton(
                      onPressed: () => Navigator.of(context).pop(),
                      child:
                          const Text('Fermer', style: TextStyle(fontSize: 17)),
                    ),
                  ],
                ),
              );
            }
          : null,
      child: Container(
        margin: const EdgeInsets.only(bottom: 14),
        padding: const EdgeInsets.all(14),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(22),
          border: Border.all(
              color: captured ? zone.color : Colors.blueGrey.shade200,
              width: 3),
        ),
        child: Row(
          children: [
            PixelArt(
              grid: info.grid,
              palette: creature.palette,
              size: 80,
              silhouette: !captured,
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    captured ? info.name : '???',
                    style: const TextStyle(
                        fontSize: 20, fontWeight: FontWeight.bold),
                  ),
                  Text(
                    '${zone.emoji} ${zone.name}',
                    style: const TextStyle(
                        fontSize: 14, color: Colors.blueGrey),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      ...List.generate(
                        AppState.maxLevel,
                        (i) => Icon(
                          i < app.levelReached(zone.id)
                              ? Icons.star_rounded
                              : Icons.star_border_rounded,
                          color: const Color(0xFFFFC93D),
                          size: 20,
                        ),
                      ),
                      const SizedBox(width: 8),
                      if (captured)
                        Text(
                          'Stade $stage/3',
                          style: TextStyle(
                              fontSize: 13, color: Colors.grey.shade600),
                        ),
                    ],
                  ),
                  if (!captured)
                    const Text(
                      'Réussis son défi pour le capturer !',
                      style: TextStyle(
                          fontSize: 13,
                          fontStyle: FontStyle.italic,
                          color: Colors.blueGrey),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
