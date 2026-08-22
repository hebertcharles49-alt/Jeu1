import 'package:flutter/material.dart';

import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Mini-jeu Physique : glisser les bons éléments pour fermer le circuit
/// et allumer l'ampoule qui libère Volty.
class CircuitGame extends StatefulWidget {
  const CircuitGame({super.key});

  @override
  State<CircuitGame> createState() => _CircuitGameState();
}

class _Item {
  final String id;
  final String emoji;
  final String label;
  const _Item(this.id, this.emoji, this.label);
}

const _items = [
  _Item('pile', '🔋', 'Pile'),
  _Item('fil', '🔌', 'Fil'),
  _Item('interrupteur', '🎚️', 'Interrupteur'),
  _Item('banane', '🍌', 'Banane'),
];

const _oopsMessages = {
  'banane': 'Hihi, une banane ne conduit pas le courant ! 🍌',
  'pile': 'La pile va dans sa case à elle. Regarde bien les images !',
  'fil': 'Le fil va dans sa case à lui. Regarde bien les images !',
  'interrupteur': 'L\'interrupteur va dans sa case à lui. Essaie encore !',
};

class _CircuitGameState extends State<CircuitGame> {
  final Map<String, bool> placed = {
    'pile': false,
    'fil': false,
    'interrupteur': false,
  };
  bool switchOn = false;
  bool won = false;

  bool get allPlaced => placed.values.every((v) => v);
  bool get lit => allPlaced && switchOn;

  void _onCorrectDrop(String id) {
    softFeedback();
    setState(() => placed[id] = true);
    if (allPlaced) {
      showOops(context, 'Tout est branché ! Appuie sur l\'interrupteur ! 🎚️');
    }
  }

  Future<void> _toggleSwitch() async {
    if (!allPlaced || won) return;
    softFeedback();
    setState(() => switchOn = !switchOn);
    if (lit) {
      won = true;
      await Future<void>.delayed(const Duration(milliseconds: 900));
      if (mounted) {
        await showVictory(context, 'circuit');
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final zone = zoneForGame('circuit');
    return GameScaffold(
      title: zone.gameTitle,
      color: zone.color,
      consigne: allPlaced
          ? 'Appuie sur l\'interrupteur pour allumer !'
          : 'Glisse la pile, le fil et l\'interrupteur dans les bonnes cases !',
      hint:
          'Pour que le courant passe, il faut une pile (l\'énergie), un fil (le chemin) et un interrupteur (la porte). La banane, elle, se mange ! 🍌',
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            // L'ampoule
            AnimatedContainer(
              duration: const Duration(milliseconds: 500),
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: lit ? const Color(0xFFFFF6C9) : Colors.white,
                boxShadow: lit
                    ? [
                        BoxShadow(
                          color: const Color(0xFFFFE24F).withOpacity(0.9),
                          blurRadius: 40,
                          spreadRadius: 10,
                        )
                      ]
                    : [],
              ),
              child: Icon(
                Icons.lightbulb,
                size: 80,
                color: lit ? const Color(0xFFFFC93D) : Colors.blueGrey.shade300,
              ),
            ),
            const SizedBox(height: 20),
            // Les trois cases du circuit
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                _slot('pile'),
                _slot('fil'),
                _slot('interrupteur'),
              ],
            ),
            const SizedBox(height: 24),
            if (allPlaced)
              GestureDetector(
                onTap: _toggleSwitch,
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 300),
                  padding: const EdgeInsets.symmetric(
                      horizontal: 32, vertical: 18),
                  decoration: BoxDecoration(
                    color: switchOn ? zone.color : Colors.white,
                    borderRadius: BorderRadius.circular(24),
                    border: Border.all(color: zone.color, width: 3),
                  ),
                  child: Text(
                    switchOn ? 'ALLUMÉ  ✨' : 'APPUIE ICI  🎚️',
                    style: TextStyle(
                      fontSize: 22,
                      fontWeight: FontWeight.bold,
                      color: switchOn ? Colors.white : zone.color,
                    ),
                  ),
                ),
              )
            else ...[
              const Text('Mes objets :',
                  style:
                      TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
              const SizedBox(height: 12),
              Wrap(
                spacing: 12,
                runSpacing: 12,
                alignment: WrapAlignment.center,
                children: [
                  for (final item in _items)
                    if (item.id == 'banane' || !(placed[item.id] ?? false))
                      _draggable(item),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _slot(String id) {
    final item = _items.firstWhere((i) => i.id == id);
    final isPlaced = placed[id] ?? false;
    return DragTarget<String>(
      onWillAcceptWithDetails: (details) => !isPlaced,
      onAcceptWithDetails: (details) {
        if (details.data == id) {
          _onCorrectDrop(id);
        } else {
          showOops(context,
              _oopsMessages[details.data] ?? 'Essaie une autre case !');
        }
      },
      builder: (context, candidates, rejected) {
        return Container(
          width: 100,
          height: 110,
          decoration: BoxDecoration(
            color: isPlaced
                ? const Color(0xFFE3F6E8)
                : candidates.isNotEmpty
                    ? const Color(0xFFE3F0FB)
                    : Colors.white,
            borderRadius: BorderRadius.circular(18),
            border: Border.all(
              color: isPlaced ? const Color(0xFF57B26A) : Colors.blueGrey,
              width: 3,
            ),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              if (isPlaced)
                Text(item.emoji, style: const TextStyle(fontSize: 38))
              else
                Icon(Icons.add_circle_outline,
                    color: Colors.blueGrey.shade300, size: 38),
              const SizedBox(height: 4),
              Text(item.label,
                  style: const TextStyle(
                      fontSize: 14, fontWeight: FontWeight.bold)),
            ],
          ),
        );
      },
    );
  }

  Widget _draggable(_Item item) {
    final chip = Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
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
          Text(item.emoji, style: const TextStyle(fontSize: 36)),
          Text(item.label,
              style:
                  const TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        ],
      ),
    );
    return Draggable<String>(
      data: item.id,
      feedback: Material(color: Colors.transparent, child: chip),
      childWhenDragging: Opacity(opacity: 0.3, child: chip),
      child: chip,
    );
  }
}
