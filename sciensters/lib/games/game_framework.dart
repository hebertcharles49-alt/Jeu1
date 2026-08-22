import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../core/app_state.dart';
import '../creatures/creatures.dart';
import '../ui/pixel_art.dart';

/// Petit son doux + vibration légère (si les sons sont activés).
void softFeedback() {
  if (AppState.instance.soundOn) {
    SystemSound.play(SystemSoundType.click);
  }
  HapticFeedback.lightImpact();
}

/// Structure commune à tous les mini-jeux : même disposition partout
/// pour que l'enfant retrouve toujours ses repères.
/// - Bandeau de consigne : UNE phrase courte, gros texte.
/// - Bouton indice (ampoule) toujours au même endroit.
/// - Jamais de chrono, jamais de score négatif.
class GameScaffold extends StatelessWidget {
  final String title;
  final Color color;
  final String consigne;
  final String hint;
  final Widget child;

  const GameScaffold({
    super.key,
    required this.title,
    required this.color,
    required this.consigne,
    required this.hint,
    required this.child,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: color.withOpacity(0.08),
      appBar: AppBar(
        title: Text(title, style: const TextStyle(fontWeight: FontWeight.bold)),
        backgroundColor: color,
        foregroundColor: Colors.white,
        actions: [
          IconButton(
            icon: const Icon(Icons.lightbulb_outline, size: 30),
            tooltip: 'Indice',
            onPressed: () {
              softFeedback();
              showDialog<void>(
                context: context,
                builder: (context) => AlertDialog(
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(24)),
                  title: const Text('💡 Indice'),
                  content: Text(hint, style: const TextStyle(fontSize: 18)),
                  actions: [
                    TextButton(
                      onPressed: () => Navigator.of(context).pop(),
                      child: const Text('Merci !',
                          style: TextStyle(fontSize: 18)),
                    ),
                  ],
                ),
              );
            },
          ),
        ],
      ),
      body: SafeArea(
        child: Column(
          children: [
            Container(
              width: double.infinity,
              margin: const EdgeInsets.all(12),
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(20),
                border: Border.all(color: color, width: 3),
              ),
              child: Text(
                consigne,
                textAlign: TextAlign.center,
                style: const TextStyle(
                    fontSize: 20, fontWeight: FontWeight.bold, height: 1.3),
              ),
            ),
            Expanded(child: child),
          ],
        ),
      ),
    );
  }
}

/// Message d'erreur tout doux : jamais punitif, toujours encourageant.
void showOops(BuildContext context, String message) {
  softFeedback();
  ScaffoldMessenger.of(context)
    ..clearSnackBars()
    ..showSnackBar(
      SnackBar(
        content: Text(message,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 17, color: Colors.black87)),
        backgroundColor: const Color(0xFFFFF3C4),
        behavior: SnackBarBehavior.floating,
        shape:
            RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        duration: const Duration(seconds: 3),
      ),
    );
}

/// Écran de victoire : capture ou évolution du Scienster, puis retour à la carte.
Future<void> showVictory(BuildContext context, String gameId) async {
  final app = AppState.instance;
  final oldStage = app.stageFor(gameId);
  await app.recordWin(gameId);
  final newStage = app.stageFor(gameId);
  final creature = creatureForGame(gameId);
  final stage = creature.stageInfo(newStage == 0 ? 1 : newStage);
  final zone = zoneForGame(gameId);

  final bool captured = oldStage == 0;
  final bool evolved = !captured && newStage > oldStage;
  final String titleText = captured
      ? '✨ ${stage.name} capturé !'
      : evolved
          ? '🌟 Évolution ! Voici ${stage.name} !'
          : '⭐ Bravo, ${stage.name} est ravi !';

  softFeedback();
  if (!context.mounted) return;

  await showDialog<void>(
    context: context,
    barrierDismissible: false,
    barrierColor: Colors.black87,
    builder: (context) => Dialog(
      backgroundColor: Colors.transparent,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          TweenAnimationBuilder<double>(
            tween: Tween(begin: 0.2, end: 1.0),
            duration: const Duration(milliseconds: 700),
            curve: Curves.elasticOut,
            builder: (context, value, child) =>
                Transform.scale(scale: value, child: child),
            child: Container(
              padding: const EdgeInsets.all(24),
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(28),
                border: Border.all(color: zone.color, width: 4),
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  PixelArt(
                    grid: stage.grid,
                    palette: creature.palette,
                    size: 160,
                  ),
                  const SizedBox(height: 16),
                  Text(
                    titleText,
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                        fontSize: 22, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 10),
                  Text(
                    stage.description,
                    textAlign: TextAlign.center,
                    style: const TextStyle(fontSize: 16, height: 1.3),
                  ),
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
                  const SizedBox(height: 16),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: List.generate(
                      3,
                      (i) => Icon(
                        i < AppState.instance.starsFor(gameId)
                            ? Icons.star_rounded
                            : Icons.star_border_rounded,
                        color: const Color(0xFFFFC93D),
                        size: 36,
                      ),
                    ),
                  ),
                  const SizedBox(height: 16),
                  FilledButton(
                    style: FilledButton.styleFrom(
                      backgroundColor: zone.color,
                      padding: const EdgeInsets.symmetric(
                          horizontal: 28, vertical: 14),
                      shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(18)),
                    ),
                    onPressed: () {
                      softFeedback();
                      Navigator.of(context).pop();
                    },
                    child: const Text('Retour à la carte 🗺️',
                        style: TextStyle(fontSize: 18)),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    ),
  );

  // Ferme l'écran du mini-jeu pour revenir à la zone.
  if (context.mounted) {
    Navigator.of(context).pop();
  }
}
