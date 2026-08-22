import 'package:flutter/material.dart';

import '../content/missions.dart';
import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Puzzle de câblage façon "blocs" : poser des câbles sur la grille
/// (budget limité) pour relier toutes les ampoules à la pile,
/// en contournant les rochers et en fermant les interrupteurs.
class CircuitPuzzleGame extends StatefulWidget {
  final Zone zone;
  final int level;

  const CircuitPuzzleGame({super.key, required this.zone, required this.level});

  @override
  State<CircuitPuzzleGame> createState() => _CircuitPuzzleGameState();
}

class _CircuitPuzzleGameState extends State<CircuitPuzzleGame> {
  late final List<CircuitPuzzle> puzzles;
  int puzzleIndex = 0;
  bool locked = false;

  final Set<int> wires = {};
  final Map<int, bool> switchClosed = {};

  // Mode "porte logique" : résultats du test de la table de vérité.
  List<bool?> testResults = [];
  bool testing = false;

  CircuitPuzzle get puzzle => puzzles[puzzleIndex];
  int get cols => puzzle.grid.first.length;
  int get rows => puzzle.grid.length;
  bool get logicMode => puzzle.truthTable.isNotEmpty;

  /// Index des interrupteurs, dans l'ordre de lecture.
  List<int> get switchIndices {
    final list = switchClosed.keys.toList()..sort();
    return list;
  }

  @override
  void initState() {
    super.initState();
    puzzles = circuitPuzzlesByLevel[widget.level]!;
    _resetPuzzle();
  }

  void _resetPuzzle() {
    wires.clear();
    switchClosed.clear();
    for (var y = 0; y < rows; y++) {
      for (var x = 0; x < cols; x++) {
        if (puzzle.grid[y][x] == 'I') switchClosed[y * cols + x] = false;
      }
    }
    testResults = List<bool?>.filled(puzzle.truthTable.length, null);
  }

  String _cellChar(int index) => puzzle.grid[index ~/ cols][index % cols];

  int get wiresLeft => puzzle.wireBudget - wires.length;

  /// Cases atteintes par le courant depuis la pile.
  Set<int> _powered() {
    bool conducts(int i) {
      final c = _cellChar(i);
      if (c == 'P' || c == 'A') return true;
      if (c == 'I') return switchClosed[i] ?? false;
      if (c == 'R') return false;
      return wires.contains(i);
    }

    final reached = <int>{};
    final queue = <int>[];
    for (var i = 0; i < cols * rows; i++) {
      if (_cellChar(i) == 'P') {
        reached.add(i);
        queue.add(i);
      }
    }
    while (queue.isNotEmpty) {
      final i = queue.removeLast();
      final x = i % cols, y = i ~/ cols;
      for (final (nx, ny) in [(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)]) {
        if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
        final n = ny * cols + nx;
        if (!reached.contains(n) && conducts(n)) {
          reached.add(n);
          queue.add(n);
        }
      }
    }
    return reached;
  }

  bool _allBulbsLit(Set<int> powered) {
    for (var i = 0; i < cols * rows; i++) {
      if (_cellChar(i) == 'A' && !powered.contains(i)) return false;
    }
    return true;
  }

  Future<void> _win() async {
    setState(() => locked = true);
    showNice(
        context,
        logicMode
            ? 'Ta porte logique fonctionne parfaitement ! 🧠⚡'
            : 'Toutes les ampoules brillent ! ⚡');
    await Future<void>.delayed(const Duration(milliseconds: 1300));
    if (!mounted) return;
    if (puzzleIndex == puzzles.length - 1) {
      await showVictory(context, widget.zone.id, widget.level);
    } else {
      setState(() {
        puzzleIndex++;
        locked = false;
        _resetPuzzle();
      });
    }
  }

  /// Mode porte logique : essaie toutes les combinaisons d'interrupteurs.
  Future<void> _runTest() async {
    if (locked || testing) return;
    softFeedback();
    setState(() {
      testing = true;
      testResults = List<bool?>.filled(puzzle.truthTable.length, null);
    });
    var allPass = true;
    for (var row = 0; row < puzzle.truthTable.length; row++) {
      final (combo, expected) = puzzle.truthTable[row];
      setState(() {
        final sw = switchIndices;
        for (var k = 0; k < sw.length && k < combo.length; k++) {
          switchClosed[sw[k]] = combo[k] == '1';
        }
      });
      await Future<void>.delayed(const Duration(milliseconds: 700));
      if (!mounted) return;
      final lit = _allBulbsLit(_powered());
      final pass = lit == expected;
      if (!pass) allPass = false;
      setState(() => testResults[row] = pass);
      await Future<void>.delayed(const Duration(milliseconds: 350));
      if (!mounted) return;
    }
    setState(() => testing = false);
    if (allPass) {
      await _win();
    } else {
      showOops(context,
          'Presque ! Regarde les lignes ❌ du tableau, puis modifie ton '
          'câblage et reteste. 💪');
    }
  }

  Future<void> _checkWin() async {
    if (locked || logicMode) return;
    if (_allBulbsLit(_powered())) {
      await _win();
    }
  }

  void _tapCell(int index) {
    if (locked || testing) return;
    final c = _cellChar(index);
    if (c == 'I') {
      softFeedback();
      setState(() => switchClosed[index] = !(switchClosed[index] ?? false));
      _checkWin();
    } else if (c == '.') {
      if (wires.contains(index)) {
        softFeedback();
        setState(() => wires.remove(index));
      } else if (wiresLeft > 0) {
        softFeedback();
        setState(() => wires.add(index));
        _checkWin();
      } else {
        showOops(context,
            'Plus de câble ! Tape sur un câble posé pour le récupérer.');
      }
    } else if (c == 'R') {
      showOops(context, 'Un rocher ! Le courant ne passe pas par là. 🪨');
    }
  }

  @override
  Widget build(BuildContext context) {
    final powered = _powered();
    return GameScaffold(
      title: '${levelMeta['physique']![widget.level]!.emoji}  '
          '${levelMeta['physique']![widget.level]!.title}',
      color: widget.zone.color,
      consigne: puzzle.consigne,
      hint: puzzle.hint,
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            // Progression + budget + reset
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Row(
                  children: List.generate(
                    puzzles.length,
                    (i) => Padding(
                      padding: const EdgeInsets.only(right: 4),
                      child: Icon(
                        i < puzzleIndex || (i == puzzleIndex && locked)
                            ? Icons.check_circle_rounded
                            : i == puzzleIndex
                                ? Icons.radio_button_checked
                                : Icons.circle_outlined,
                        size: 24,
                        color: i < puzzleIndex || (i == puzzleIndex && locked)
                            ? const Color(0xFF57B26A)
                            : widget.zone.color,
                      ),
                    ),
                  ),
                ),
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: Colors.white,
                    borderRadius: BorderRadius.circular(14),
                    border: Border.all(
                        color: wiresLeft == 0
                            ? const Color(0xFFE07A5F)
                            : widget.zone.color,
                        width: 2),
                  ),
                  child: Text('🧵 Câbles : $wiresLeft',
                      style: const TextStyle(
                          fontSize: 16, fontWeight: FontWeight.bold)),
                ),
                IconButton(
                  onPressed: locked
                      ? null
                      : () {
                          softFeedback();
                          setState(_resetPuzzle);
                        },
                  icon: const Icon(Icons.refresh_rounded, size: 28),
                  tooltip: 'Recommencer ce tableau',
                ),
              ],
            ),
            const SizedBox(height: 12),
            // La grille
            LayoutBuilder(
              builder: (context, constraints) {
                final cell =
                    (constraints.maxWidth - 8) / cols < 60.0
                        ? (constraints.maxWidth - 8) / cols
                        : 60.0;
                return Container(
                  padding: const EdgeInsets.all(4),
                  decoration: BoxDecoration(
                    color: const Color(0xFF2E4057),
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      for (var y = 0; y < rows; y++)
                        Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            for (var x = 0; x < cols; x++)
                              _cellWidget(y * cols + x, cell, powered),
                          ],
                        ),
                    ],
                  ),
                );
              },
            ),
            const SizedBox(height: 14),
            if (logicMode) ...[
              _truthTableWidget(),
              const SizedBox(height: 12),
              FilledButton(
                style: FilledButton.styleFrom(
                  backgroundColor: widget.zone.color,
                  padding: const EdgeInsets.symmetric(
                      horizontal: 36, vertical: 14),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(20)),
                ),
                onPressed: (locked || testing) ? null : _runTest,
                child: Text(
                  testing ? 'Test en cours…' : '🧪  TESTER ma porte !',
                  style: const TextStyle(
                      fontSize: 18, fontWeight: FontWeight.bold),
                ),
              ),
            ] else
              const Text(
                'Tape une case vide pour poser un câble 🟨\n'
                'Retape dessus pour le reprendre',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 14, color: Colors.blueGrey),
              ),
          ],
        ),
      ),
    );
  }

  Widget _truthTableWidget() {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: widget.zone.color, width: 2),
      ),
      child: Column(
        children: [
          const Text('La règle de ma porte :',
              style: TextStyle(fontSize: 15, fontWeight: FontWeight.bold)),
          const SizedBox(height: 6),
          for (var row = 0; row < puzzle.truthTable.length; row++)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 3),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  for (var k = 0; k < puzzle.truthTable[row].$1.length; k++)
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 3),
                      child: Text(
                        puzzle.truthTable[row].$1[k] == '1'
                            ? '🎚️ fermé'
                            : '🔀 ouvert',
                        style: const TextStyle(fontSize: 14),
                      ),
                    ),
                  const Text('  ➜  ', style: TextStyle(fontSize: 14)),
                  Icon(
                    Icons.lightbulb,
                    size: 20,
                    color: puzzle.truthTable[row].$2
                        ? const Color(0xFFFFB300)
                        : Colors.blueGrey.shade300,
                  ),
                  Text(
                    puzzle.truthTable[row].$2 ? ' allumée ' : ' éteinte ',
                    style: const TextStyle(fontSize: 14),
                  ),
                  SizedBox(
                    width: 26,
                    child: testResults.length > row &&
                            testResults[row] != null
                        ? Text(testResults[row]! ? '✅' : '❌',
                            style: const TextStyle(fontSize: 16))
                        : null,
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }

  Widget _cellWidget(int index, double size, Set<int> powered) {
    final c = _cellChar(index);
    final isWire = wires.contains(index);
    final isPowered = powered.contains(index);

    Color bg;
    Widget? content;
    switch (c) {
      case 'P':
        bg = const Color(0xFF57B26A);
        content = const Text('🔋', style: TextStyle(fontSize: 26));
      case 'A':
        bg = isPowered ? const Color(0xFFFFF6C9) : const Color(0xFF44546A);
        content = Icon(Icons.lightbulb,
            size: size * 0.55,
            color: isPowered
                ? const Color(0xFFFFB300)
                : Colors.blueGrey.shade300);
      case 'I':
        final closed = switchClosed[index] ?? false;
        bg = closed ? const Color(0xFF8BC7F0) : const Color(0xFF44546A);
        content = Text(closed ? '🎚️' : '🔀',
            style: TextStyle(fontSize: size * 0.4));
      case 'R':
        bg = const Color(0xFF6B7A8F);
        content = Text('🪨', style: TextStyle(fontSize: size * 0.4));
      default:
        if (isWire) {
          bg = isPowered ? const Color(0xFFFFC93D) : const Color(0xFFB8912E);
          content = null;
        } else {
          bg = const Color(0xFF3A4E68);
          content = null;
        }
    }

    return GestureDetector(
      onTap: () => _tapCell(index),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 250),
        width: size,
        height: size,
        margin: const EdgeInsets.all(2),
        decoration: BoxDecoration(
          color: bg,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: isWire && isPowered
                ? const Color(0xFFFFE24F)
                : Colors.black26,
            width: 2,
          ),
          boxShadow: (c == 'A' && isPowered)
              ? [
                  BoxShadow(
                      color: const Color(0xFFFFE24F).withValues(alpha: 0.8),
                      blurRadius: 14,
                      spreadRadius: 2)
                ]
              : null,
        ),
        child: Center(child: content),
      ),
    );
  }
}
