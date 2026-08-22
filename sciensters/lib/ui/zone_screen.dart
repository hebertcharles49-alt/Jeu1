import 'package:flutter/material.dart';

import '../core/app_state.dart';
import '../creatures/creatures.dart';
import '../games/chaine_alimentaire_game.dart';
import '../games/circuit_game.dart';
import '../games/etats_eau_game.dart';
import '../games/game_framework.dart';
import 'pixel_art.dart';

/// Écran d'une zone : présente le défi et le Scienster à capturer.
class ZoneScreen extends StatelessWidget {
  final Zone zone;

  const ZoneScreen({super.key, required this.zone});

  Widget _gameFor(String gameId) {
    switch (gameId) {
      case 'circuit':
        return const CircuitGame();
      case 'etats_eau':
        return const EtatsEauGame();
      case 'chaine':
      default:
        return const ChaineAlimentaireGame();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: zone.color.withOpacity(0.08),
      appBar: AppBar(
        title: Text('${zone.emoji}  ${zone.name}',
            style: const TextStyle(fontWeight: FontWeight.bold)),
        backgroundColor: zone.color,
        foregroundColor: Colors.white,
      ),
      body: SafeArea(
        child: ListenableBuilder(
          listenable: AppState.instance,
          builder: (context, _) {
            final app = AppState.instance;
            final creature = creatureForGame(zone.gameId);
            final stage = app.stageFor(zone.gameId);
            final captured = stage > 0;
            return SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  Container(
                    width: double.infinity,
                    padding: const EdgeInsets.all(18),
                    decoration: BoxDecoration(
                      color: Colors.white,
                      borderRadius: BorderRadius.circular(24),
                      border: Border.all(color: zone.color, width: 3),
                    ),
                    child: Column(
                      children: [
                        Text(
                          zone.description,
                          textAlign: TextAlign.center,
                          style: const TextStyle(fontSize: 18, height: 1.4),
                        ),
                        const SizedBox(height: 16),
                        PixelArt(
                          grid: creature.stageInfo(captured ? stage : 1).grid,
                          palette: creature.palette,
                          size: 120,
                          silhouette: !captured,
                        ),
                        const SizedBox(height: 8),
                        Text(
                          captured
                              ? creature.stageInfo(stage).name
                              : '??? (à capturer !)',
                          style: const TextStyle(
                              fontSize: 20, fontWeight: FontWeight.bold),
                        ),
                        const SizedBox(height: 8),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: List.generate(
                            3,
                            (i) => Icon(
                              i < app.starsFor(zone.gameId)
                                  ? Icons.star_rounded
                                  : Icons.star_border_rounded,
                              color: const Color(0xFFFFC93D),
                              size: 30,
                            ),
                          ),
                        ),
                        if (captured && stage < 3)
                          Padding(
                            padding: const EdgeInsets.only(top: 6),
                            child: Text(
                              'Rejoue pour le faire évoluer !',
                              style: TextStyle(
                                  fontSize: 15,
                                  color: Colors.grey.shade600,
                                  fontStyle: FontStyle.italic),
                            ),
                          ),
                        const SizedBox(height: 16),
                        FilledButton(
                          style: FilledButton.styleFrom(
                            backgroundColor: zone.color,
                            padding: const EdgeInsets.symmetric(
                                horizontal: 40, vertical: 16),
                            shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(20)),
                          ),
                          onPressed: () {
                            softFeedback();
                            Navigator.of(context).push(
                              MaterialPageRoute<void>(
                                  builder: (_) => _gameFor(zone.gameId)),
                            );
                          },
                          child: Text(
                            '▶  ${zone.gameTitle}',
                            style: const TextStyle(
                                fontSize: 20, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 14),
                  Container(
                    width: double.infinity,
                    padding: const EdgeInsets.all(18),
                    decoration: BoxDecoration(
                      color: Colors.white.withOpacity(0.6),
                      borderRadius: BorderRadius.circular(24),
                      border: Border.all(color: Colors.blueGrey.shade200,
                          width: 2),
                    ),
                    child: const Column(
                      children: [
                        Text('🔒', style: TextStyle(fontSize: 32)),
                        SizedBox(height: 6),
                        Text(
                          'D\'autres défis arrivent bientôt…',
                          style: TextStyle(
                              fontSize: 16, color: Colors.blueGrey),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            );
          },
        ),
      ),
    );
  }
}
