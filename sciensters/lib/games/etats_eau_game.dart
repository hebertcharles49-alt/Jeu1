import 'package:flutter/material.dart';

import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Mini-jeu Chimie : chauffer ou refroidir l'eau pour la faire changer d'état
/// (glace ↔ eau ↔ vapeur) et libérer Gouttix.
class EtatsEauGame extends StatefulWidget {
  const EtatsEauGame({super.key});

  @override
  State<EtatsEauGame> createState() => _EtatsEauGameState();
}

const _stateNames = ['GLACE', 'EAU', 'VAPEUR'];
const _stateEmojis = ['🧊', '💧', '☁️'];

/// Les 3 défis à réussir : (état de départ, état à atteindre).
const _challenges = [
  (0, 1), // glace -> eau
  (1, 2), // eau -> vapeur
  (2, 0), // vapeur -> glace
];

class _EtatsEauGameState extends State<EtatsEauGame> {
  int challengeIndex = 0;
  int currentState = 0;
  bool transitioning = false;
  bool won = false;

  int get targetState => _challenges[challengeIndex].$2;

  String get consigne {
    if (won) return 'Tu as réussi tous les défis ! 🎉';
    return 'Défi ${challengeIndex + 1}/3 : transforme '
        '${_stateNames[currentState] == 'EAU' ? 'l\'' : 'la '}'
        '${_stateNames[currentState]} en ${_stateNames[targetState]} !';
  }

  String _transitionMessage(int from, int to) {
    if (from == 0 && to == 1) return 'La glace FOND ! 🧊➡️💧';
    if (from == 1 && to == 2) return 'L\'eau s\'ÉVAPORE ! 💧➡️☁️';
    if (from == 2 && to == 1) return 'La vapeur se CONDENSE ! ☁️➡️💧';
    if (from == 1 && to == 0) return 'L\'eau GÈLE ! 💧➡️🧊';
    return '';
  }

  Future<void> _change(int delta) async {
    if (transitioning || won) return;
    final next = (currentState + delta).clamp(0, 2);
    if (next == currentState) {
      showOops(
          context,
          delta > 0
              ? 'C\'est déjà de la vapeur, on ne peut pas chauffer plus ! ☁️'
              : 'C\'est déjà de la glace, on ne peut pas refroidir plus ! 🧊');
      return;
    }
    softFeedback();
    final message = _transitionMessage(currentState, next);
    setState(() {
      currentState = next;
      transitioning = true;
    });
    showOops(context, message);
    await Future<void>.delayed(const Duration(milliseconds: 900));
    if (!mounted) return;

    if (currentState == targetState) {
      if (challengeIndex == _challenges.length - 1) {
        setState(() => won = true);
        await Future<void>.delayed(const Duration(milliseconds: 600));
        if (mounted) {
          await showVictory(context, 'chimie', 1);
        }
        return;
      }
      setState(() {
        challengeIndex++;
        transitioning = false;
      });
    } else {
      setState(() => transitioning = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final zone = zoneForGame('etats_eau');
    return GameScaffold(
      title: zone.gameTitle,
      color: zone.color,
      consigne: consigne,
      hint:
          'Chauffer fait monter la température : la glace devient eau, puis vapeur. Refroidir fait l\'inverse !',
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            // L'état actuel, en grand
            AnimatedSwitcher(
              duration: const Duration(milliseconds: 500),
              transitionBuilder: (child, animation) =>
                  ScaleTransition(scale: animation, child: child),
              child: Container(
                key: ValueKey(currentState),
                padding: const EdgeInsets.all(24),
                decoration: BoxDecoration(
                  color: Colors.white,
                  shape: BoxShape.circle,
                  border: Border.all(color: zone.color, width: 4),
                ),
                child: Text(_stateEmojis[currentState],
                    style: const TextStyle(fontSize: 90)),
              ),
            ),
            const SizedBox(height: 8),
            Text(
              _stateNames[currentState],
              style: const TextStyle(
                  fontSize: 26,
                  fontWeight: FontWeight.bold,
                  letterSpacing: 2),
            ),
            const SizedBox(height: 16),
            // Thermomètre simple : 3 crans
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Text('🌡️', style: TextStyle(fontSize: 30)),
                const SizedBox(width: 8),
                for (var i = 0; i < 3; i++)
                  AnimatedContainer(
                    duration: const Duration(milliseconds: 400),
                    width: 60,
                    height: 18,
                    margin: const EdgeInsets.symmetric(horizontal: 3),
                    decoration: BoxDecoration(
                      color: i <= currentState
                          ? Color.lerp(const Color(0xFF4FC3F7),
                              const Color(0xFFFF7043), i / 2)
                          : Colors.blueGrey.shade100,
                      borderRadius: BorderRadius.circular(9),
                    ),
                  ),
              ],
            ),
            const SizedBox(height: 28),
            // Boutons chauffer / refroidir
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                _actionButton('❄️', 'REFROIDIR', const Color(0xFF4FC3F7),
                    () => _change(-1)),
                _actionButton('🔥', 'CHAUFFER', const Color(0xFFFF7043),
                    () => _change(1)),
              ],
            ),
            const SizedBox(height: 24),
            // Progression des défis
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: List.generate(
                _challenges.length,
                (i) => Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 6),
                  child: Icon(
                    i < challengeIndex || won
                        ? Icons.check_circle_rounded
                        : Icons.circle_outlined,
                    color: i < challengeIndex || won
                        ? const Color(0xFF57B26A)
                        : Colors.blueGrey.shade300,
                    size: 32,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _actionButton(
      String emoji, String label, Color color, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 22, vertical: 16),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(22),
          border: Border.all(color: color, width: 3),
          boxShadow: const [
            BoxShadow(
                color: Colors.black12, blurRadius: 4, offset: Offset(0, 2))
          ],
        ),
        child: Column(
          children: [
            Text(emoji, style: const TextStyle(fontSize: 40)),
            const SizedBox(height: 4),
            Text(label,
                style: TextStyle(
                    fontSize: 16, fontWeight: FontWeight.bold, color: color)),
          ],
        ),
      ),
    );
  }
}
