import 'package:flutter/material.dart';

import '../content/enigmes.dart';
import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Écran générique d'une série d'énigmes : QCM à gros boutons ou jeux
/// de tri, enchaînés un par un. Utilisé pour le "Quiz du Professeur"
/// (bonus, hors progression). Une erreur affiche un encouragement
/// + un indice, jamais d'échec.
class EnigmeScreen extends StatefulWidget {
  final Zone zone;
  final EnigmeLevel data;
  final bool bonus;

  const EnigmeScreen({
    super.key,
    required this.zone,
    required this.data,
    this.bonus = true,
  });

  @override
  State<EnigmeScreen> createState() => _EnigmeScreenState();
}

class _EnigmeScreenState extends State<EnigmeScreen> {
  int index = 0;
  bool locked = false; // bloque les taps pendant les transitions

  // État du tri en cours : ids (index d'item) déjà placés.
  final Set<int> sorted = {};

  EnigmeLevel get data => widget.data;
  Enigme get current => data.enigmes[index];

  Future<void> _advance() async {
    await Future<void>.delayed(const Duration(milliseconds: 1300));
    if (!mounted) return;
    if (index == data.enigmes.length - 1) {
      if (widget.bonus) {
        await _bonusDone();
      } else {
        await showVictory(context, widget.zone.id, data.level);
      }
    } else {
      setState(() {
        index++;
        locked = false;
        sorted.clear();
      });
    }
  }

  Future<void> _bonusDone() async {
    await showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        shape:
            RoundedRectangleBorder(borderRadius: BorderRadius.circular(24)),
        title: const Text('🧠 Quiz terminé !', textAlign: TextAlign.center),
        content: const Text(
          'Le Professeur Pixel est impressionné : tu connais ta science '
          'sur le bout des doigts ! 🎓',
          textAlign: TextAlign.center,
          style: TextStyle(fontSize: 17, height: 1.4),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Merci !', style: TextStyle(fontSize: 18)),
          ),
        ],
      ),
    );
    if (mounted) Navigator.of(context).pop();
  }

  void _onQcmTap(int choice) {
    if (locked) return;
    if (choice == current.correctIndex) {
      softFeedback();
      setState(() => locked = true);
      showNice(context, current.successNote);
      _advance();
    } else {
      showOops(context, 'Presque ! 💪  ${current.hint}');
    }
  }

  void _onSortDrop(int itemIndex, bool droppedInA) {
    if (locked || sorted.contains(itemIndex)) return;
    final item = current.items[itemIndex];
    if (item.inA == droppedInA) {
      softFeedback();
      setState(() => sorted.add(itemIndex));
      if (sorted.length == current.items.length) {
        setState(() => locked = true);
        showNice(context, current.successNote);
        _advance();
      }
    } else {
      showOops(context, 'Presque ! 💪  ${current.hint}');
    }
  }

  @override
  Widget build(BuildContext context) {
    return GameScaffold(
      title: '${data.emoji}  ${data.title}',
      color: widget.zone.color,
      consigne: current.question,
      hint: current.hint,
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            // Progression : une pastille par énigme.
            Wrap(
              alignment: WrapAlignment.center,
              children: List.generate(
                data.enigmes.length,
                (i) => Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 4),
                  child: Icon(
                    i < index || (i == index && locked)
                        ? Icons.check_circle_rounded
                        : i == index
                            ? Icons.radio_button_checked
                            : Icons.circle_outlined,
                    size: 26,
                    color: i < index || (i == index && locked)
                        ? const Color(0xFF57B26A)
                        : i == index
                            ? widget.zone.color
                            : Colors.blueGrey.shade300,
                  ),
                ),
              ),
            ),
            const SizedBox(height: 16),
            if (current.type == 'qcm') _qcm() else _tri(),
          ],
        ),
      ),
    );
  }

  // ─────────────────────────── QCM ───────────────────────────

  Widget _qcm() {
    return Column(
      children: [
        for (var i = 0; i < current.choices.length; i++)
          Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: SizedBox(
              width: double.infinity,
              child: GestureDetector(
                onTap: () => _onQcmTap(i),
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 300),
                  padding: const EdgeInsets.symmetric(
                      horizontal: 18, vertical: 18),
                  decoration: BoxDecoration(
                    color: locked && i == current.correctIndex
                        ? const Color(0xFFD9F2DD)
                        : Colors.white,
                    borderRadius: BorderRadius.circular(20),
                    border: Border.all(
                      color: locked && i == current.correctIndex
                          ? const Color(0xFF57B26A)
                          : widget.zone.color,
                      width: 3,
                    ),
                  ),
                  child: Text(
                    current.choices[i],
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                        fontSize: 19, fontWeight: FontWeight.bold),
                  ),
                ),
              ),
            ),
          ),
      ],
    );
  }

  // ─────────────────────────── TRI ───────────────────────────

  Widget _tri() {
    return Column(
      children: [
        Row(
          children: [
            Expanded(child: _bin(true)),
            const SizedBox(width: 10),
            Expanded(child: _bin(false)),
          ],
        ),
        const SizedBox(height: 18),
        Wrap(
          spacing: 10,
          runSpacing: 10,
          alignment: WrapAlignment.center,
          children: [
            for (var i = 0; i < current.items.length; i++)
              if (!sorted.contains(i)) _draggableItem(i),
          ],
        ),
        const SizedBox(height: 12),
      ],
    );
  }

  Widget _bin(bool isA) {
    final label = isA ? current.catA : current.catB;
    final placedItems = [
      for (var i = 0; i < current.items.length; i++)
        if (sorted.contains(i) && current.items[i].inA == isA)
          current.items[i],
    ];
    return DragTarget<int>(
      onWillAcceptWithDetails: (details) => !sorted.contains(details.data),
      onAcceptWithDetails: (details) => _onSortDrop(details.data, isA),
      builder: (context, candidates, rejected) {
        return Container(
          constraints: const BoxConstraints(minHeight: 150),
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(
            color: candidates.isNotEmpty
                ? const Color(0xFFE3F0FB)
                : Colors.white,
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: widget.zone.color, width: 3),
          ),
          child: Column(
            children: [
              Text(
                label,
                textAlign: TextAlign.center,
                style: const TextStyle(
                    fontSize: 17, fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 6,
                runSpacing: 6,
                alignment: WrapAlignment.center,
                children: [
                  for (final item in placedItems)
                    Tooltip(
                      message: item.label,
                      child: Text(item.emoji,
                          style: const TextStyle(fontSize: 30)),
                    ),
                ],
              ),
            ],
          ),
        );
      },
    );
  }

  Widget _draggableItem(int i) {
    final item = current.items[i];
    final chip = Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
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
          Text(item.emoji, style: const TextStyle(fontSize: 32)),
          Text(item.label,
              style:
                  const TextStyle(fontSize: 13, fontWeight: FontWeight.bold)),
        ],
      ),
    );
    return Draggable<int>(
      data: i,
      feedback: Material(color: Colors.transparent, child: chip),
      childWhenDragging: Opacity(opacity: 0.3, child: chip),
      child: chip,
    );
  }
}
