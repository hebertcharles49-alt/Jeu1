import 'package:flutter/material.dart';

import '../content/enigmes.dart';
import '../core/app_state.dart';
import '../creatures/creatures.dart';
import '../games/chaine_alimentaire_game.dart';
import '../games/circuit_game.dart';
import '../games/enigme_screen.dart';
import '../games/etats_eau_game.dart';
import '../games/game_framework.dart';
import 'pixel_art.dart';

/// Écran d'une zone : le Scienster de la zone + les 5 niveaux
/// (déblocage progressif, difficulté croissante du CM2 à la 5e).
class ZoneScreen extends StatelessWidget {
  final Zone zone;

  const ZoneScreen({super.key, required this.zone});

  Widget _level1Game() {
    switch (zone.gameId) {
      case 'circuit':
        return const CircuitGame();
      case 'etats_eau':
        return const EtatsEauGame();
      case 'chaine':
      default:
        return const ChaineAlimentaireGame();
    }
  }

  void _openLevel(BuildContext context, int level) {
    softFeedback();
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => level == 1
            ? _level1Game()
            : EnigmeScreen(zone: zone, level: level),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: zone.color.withValues(alpha: 0.08),
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
            final creature = creatureForZone(zone.id);
            final stage = app.stageForZone(zone.id);
            final captured = stage > 0;
            return SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  // En-tête : le Scienster de la zone
                  Container(
                    width: double.infinity,
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: Colors.white,
                      borderRadius: BorderRadius.circular(24),
                      border: Border.all(color: zone.color, width: 3),
                    ),
                    child: Row(
                      children: [
                        PixelArt(
                          grid: creature.stageInfo(captured ? stage : 1).grid,
                          palette: creature.palette,
                          size: 90,
                          silhouette: !captured,
                        ),
                        const SizedBox(width: 14),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                captured
                                    ? creature.stageInfo(stage).name
                                    : '??? (à capturer !)',
                                style: const TextStyle(
                                    fontSize: 20,
                                    fontWeight: FontWeight.bold),
                              ),
                              const SizedBox(height: 4),
                              Text(
                                zone.description,
                                style: const TextStyle(
                                    fontSize: 15, height: 1.3),
                              ),
                              if (captured && stage < 3)
                                Padding(
                                  padding: const EdgeInsets.only(top: 4),
                                  child: Text(
                                    'Monte les niveaux pour le faire évoluer !',
                                    style: TextStyle(
                                        fontSize: 13,
                                        color: Colors.grey.shade600,
                                        fontStyle: FontStyle.italic),
                                  ),
                                ),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 16),
                  // Les 5 niveaux
                  for (var level = 1; level <= AppState.maxLevel; level++)
                    _levelCard(context, level),
                ],
              ),
            );
          },
        ),
      ),
    );
  }

  Widget _levelCard(BuildContext context, int level) {
    final app = AppState.instance;
    final unlocked = app.isLevelUnlocked(zone.id, level);
    final done = app.isLevelDone(zone.id, level);
    final String title;
    final String emoji;
    if (level == 1) {
      title = zone.gameTitle;
      emoji = '🎮';
    } else {
      final data = enigmeLevelFor(zone.id, level);
      title = data.title;
      emoji = data.emoji;
    }

    return GestureDetector(
      onTap: unlocked ? () => _openLevel(context, level) : null,
      child: Container(
        margin: const EdgeInsets.only(bottom: 12),
        padding: const EdgeInsets.all(14),
        decoration: BoxDecoration(
          color: unlocked ? Colors.white : Colors.white.withValues(alpha: 0.5),
          borderRadius: BorderRadius.circular(20),
          border: Border.all(
            color: done
                ? const Color(0xFF57B26A)
                : unlocked
                    ? zone.color
                    : Colors.blueGrey.shade200,
            width: 3,
          ),
        ),
        child: Row(
          children: [
            Text(unlocked ? emoji : '🔒',
                style: const TextStyle(fontSize: 32)),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Niveau $level — $title',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.bold,
                      color: unlocked ? Colors.black87 : Colors.blueGrey,
                    ),
                  ),
                  Text(
                    done
                        ? 'Réussi ! Tu peux rejouer quand tu veux.'
                        : unlocked
                            ? level == 1
                                ? 'Un défi à manipuler !'
                                : 'Des énigmes de plus en plus fortes !'
                            : 'Termine le niveau ${level - 1} pour l\'ouvrir.',
                    style: TextStyle(
                        fontSize: 13,
                        color: done
                            ? const Color(0xFF57B26A)
                            : Colors.blueGrey),
                  ),
                ],
              ),
            ),
            if (done)
              const Icon(Icons.check_circle_rounded,
                  color: Color(0xFF57B26A), size: 30)
            else if (unlocked)
              Icon(Icons.play_circle_fill_rounded,
                  color: zone.color, size: 34),
          ],
        ),
      ),
    );
  }
}
