import 'package:flutter/material.dart';

import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Mini-jeu SVT : reconstruire la chaîne alimentaire dans le bon ordre
/// pour aider Renardeau à retrouver son dîner.
class ChaineAlimentaireGame extends StatefulWidget {
  const ChaineAlimentaireGame({super.key});

  @override
  State<ChaineAlimentaireGame> createState() => _ChaineAlimentaireGameState();
}

class _Being {
  final String id;
  final String emoji;
  final String label;
  const _Being(this.id, this.emoji, this.label);
}

const _soleil = _Being('soleil', '☀️', 'Soleil');

/// L'ordre correct après le soleil.
const _chain = [
  _Being('herbe', '🌿', 'Herbe'),
  _Being('lapin', '🐰', 'Lapin'),
  _Being('renard', '🦊', 'Renard'),
];

const _slotHints = [
  'Qui a besoin du soleil pour pousser ? 🌱',
  'Qui adore grignoter l\'herbe ? 🌿',
  'Qui chasse les lapins dans la forêt ? 🐰',
];

class _ChaineAlimentaireGameState extends State<ChaineAlimentaireGame> {
  final List<bool> filled = [false, false, false];
  bool won = false;

  @override
  Widget build(BuildContext context) {
    final zone = zoneForGame('chaine');
    return GameScaffold(
      title: zone.gameTitle,
      color: zone.color,
      consigne: 'Remets la chaîne dans l\'ordre : qui mange qui ?',
      hint:
          'Tout commence par le soleil : il fait pousser l\'herbe. Puis chacun est mangé par le suivant !',
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            _fixedCard(_soleil),
            for (var i = 0; i < _chain.length; i++) ...[
              _arrow(),
              _slot(i),
            ],
            const SizedBox(height: 20),
            const Text('Mes animaux et plantes :',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
            const SizedBox(height: 12),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              alignment: WrapAlignment.center,
              children: [
                for (var i = _chain.length - 1; i >= 0; i--)
                  if (!filled[i]) _draggable(_chain[i]),
              ],
            ),
            const SizedBox(height: 12),
          ],
        ),
      ),
    );
  }

  Widget _arrow() {
    return const Padding(
      padding: EdgeInsets.symmetric(vertical: 2),
      child: Column(
        children: [
          Icon(Icons.arrow_downward_rounded, size: 26, color: Colors.blueGrey),
          Text('est mangé par…',
              style: TextStyle(fontSize: 13, color: Colors.blueGrey)),
        ],
      ),
    );
  }

  Widget _fixedCard(_Being being) {
    return Container(
      width: 220,
      padding: const EdgeInsets.symmetric(vertical: 10),
      decoration: BoxDecoration(
        color: const Color(0xFFFFF6C9),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: const Color(0xFFFFC93D), width: 3),
      ),
      child: Column(
        children: [
          Text(being.emoji, style: const TextStyle(fontSize: 40)),
          Text(being.label,
              style:
                  const TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
          const Text('(donne son énergie)',
              style: TextStyle(fontSize: 12, color: Colors.blueGrey)),
        ],
      ),
    );
  }

  Widget _slot(int index) {
    final expected = _chain[index];
    final isFilled = filled[index];
    return DragTarget<String>(
      onWillAcceptWithDetails: (details) => !isFilled,
      onAcceptWithDetails: (details) async {
        if (details.data == expected.id) {
          softFeedback();
          setState(() => filled[index] = true);
          if (filled.every((f) => f) && !won) {
            won = true;
            await Future<void>.delayed(const Duration(milliseconds: 800));
            if (mounted) {
              await showVictory(context, 'svt', 1);
            }
          }
        } else {
          showOops(context, _slotHints[index]);
        }
      },
      builder: (context, candidates, rejected) {
        return Container(
          width: 220,
          height: 82,
          decoration: BoxDecoration(
            color: isFilled
                ? const Color(0xFFE3F6E8)
                : candidates.isNotEmpty
                    ? const Color(0xFFE3F0FB)
                    : Colors.white,
            borderRadius: BorderRadius.circular(18),
            border: Border.all(
              color: isFilled ? const Color(0xFF57B26A) : Colors.blueGrey,
              width: 3,
            ),
          ),
          child: isFilled
              ? Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(expected.emoji, style: const TextStyle(fontSize: 36)),
                    Text(expected.label,
                        style: const TextStyle(
                            fontSize: 14, fontWeight: FontWeight.bold)),
                  ],
                )
              : Center(
                  child: Icon(Icons.add_circle_outline,
                      color: Colors.blueGrey.shade300, size: 34),
                ),
        );
      },
    );
  }

  Widget _draggable(_Being being) {
    final chip = Container(
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.blueGrey.shade200, width: 2),
        boxShadow: const [
          BoxShadow(color: Colors.black12, blurRadius: 4, offset: Offset(0, 2))
        ],
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(being.emoji, style: const TextStyle(fontSize: 36)),
          Text(being.label,
              style:
                  const TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        ],
      ),
    );
    return Draggable<String>(
      data: being.id,
      feedback: Material(color: Colors.transparent, child: chip),
      childWhenDragging: Opacity(opacity: 0.3, child: chip),
      child: chip,
    );
  }
}
