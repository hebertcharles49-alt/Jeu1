import 'package:flutter/material.dart';

import '../content/missions.dart';
import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Simulation d'écosystème au tour par tour : herbe 🌿, lapins 🐰,
/// renards 🦊. Chaque saison, la nature évolue ; le joueur dispose
/// d'actions pour maintenir l'équilibre. Aucun chrono : on réfléchit
/// autant qu'on veut avant de passer la saison.
class EcosystemGame extends StatefulWidget {
  final Zone zone;
  final int level;

  const EcosystemGame({super.key, required this.zone, required this.level});

  @override
  State<EcosystemGame> createState() => _EcosystemGameState();
}

class _EcosystemGameState extends State<EcosystemGame> {
  late final EcoMission mission;
  late int herbe;
  late int lapins;
  late int renards;
  late int season;
  late int actionsLeft;
  List<String> log = [];
  bool locked = false;

  @override
  void initState() {
    super.initState();
    mission = ecoMissionsByLevel[widget.level]!;
    _reset();
  }

  void _reset() {
    herbe = mission.herbe;
    lapins = mission.lapins;
    renards = mission.renards;
    season = 1;
    actionsLeft = mission.actionsPerSeason;
    log = ['La saison 1 commence. Observe bien ton écosystème !'];
  }

  void _action(void Function() apply, String message) {
    if (locked) return;
    if (actionsLeft <= 0) {
      showOops(context,
          'Plus d\'action cette saison ! Passe à la saison suivante. ☀️');
      return;
    }
    softFeedback();
    setState(() {
      apply();
      actionsLeft--;
      log = [message, ...log.take(2)];
    });
  }

  Future<void> _nextSeason() async {
    if (locked) return;
    softFeedback();
    final events = <String>[];

    // 1. L'herbe pousse.
    final growth = mission.herbeGrowth;
    herbe = (herbe + growth).clamp(0, 15);
    events.add(growth <= 1
        ? '☀️ Sécheresse : l\'herbe pousse à peine (+$growth).'
        : '🌿 L\'herbe a poussé (+$growth).');

    // 2. Les lapins mangent l'herbe, puis se reproduisent.
    final lapinsNourris = lapins < herbe ? lapins : herbe;
    final lapinsPartis = lapins - lapinsNourris;
    herbe -= lapinsNourris;
    lapins = lapinsNourris;
    if (lapinsNourris > 0) {
      events.add('🐰 Les lapins ont mangé $lapinsNourris herbe(s).');
    }
    if (lapinsPartis > 0) {
      events.add('😟 $lapinsPartis lapin(s) sont partis : plus assez d\'herbe !');
    }
    if (lapins >= 2) {
      final naissances = lapins ~/ 2;
      lapins = (lapins + naissances).clamp(0, 15);
      events.add('🍼 $naissances bébé(s) lapin(s) sont nés !');
    }

    // 3. Les renards chassent, puis se reproduisent.
    final renardsNourris = renards < lapins ? renards : lapins;
    final renardsPartis = renards - renardsNourris;
    lapins -= renardsNourris;
    renards = renardsNourris;
    if (renardsNourris > 0) {
      events.add('🦊 Les renards ont chassé $renardsNourris lapin(s).');
    }
    if (renardsPartis > 0) {
      events.add('😟 $renardsPartis renard(s) sont partis : plus assez de lapins !');
    }
    if (renards >= 2) {
      renards = (renards + 1).clamp(0, 8);
      events.add('🍼 Un bébé renard est né !');
    }

    setState(() {
      season++;
      actionsLeft = mission.actionsPerSeason;
      log = events;
    });

    // Défaite douce : un maillon a disparu → on propose de réessayer.
    if (lapins == 0 || renards == 0) {
      final missing = lapins == 0 ? 'lapins' : 'renards';
      await Future<void>.delayed(const Duration(milliseconds: 600));
      if (!mounted) return;
      await showDialog<void>(
        context: context,
        barrierDismissible: false,
        builder: (context) => AlertDialog(
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(24)),
          title: const Text('Oh non ! 🍂'),
          content: Text(
            'Il n\'y a plus de $missing dans la forêt… La chaîne est '
            'cassée ! Ce n\'est pas grave : on remonte le temps et '
            'on réessaie. Tu vas y arriver ! 💪',
            style: const TextStyle(fontSize: 17, height: 1.4),
          ),
          actions: [
            TextButton(
              onPressed: () {
                softFeedback();
                Navigator.of(context).pop();
              },
              child: const Text('Je réessaie !',
                  style: TextStyle(fontSize: 18)),
            ),
          ],
        ),
      );
      if (mounted) setState(_reset);
      return;
    }

    // Victoire : on a tenu le bon nombre de saisons.
    if (season > mission.seasons) {
      setState(() => locked = true);
      showNice(context, mission.successNote);
      await Future<void>.delayed(const Duration(milliseconds: 1600));
      if (mounted) {
        await showVictory(context, widget.zone.id, widget.level);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final meta = levelMeta['svt']![widget.level]!;
    return GameScaffold(
      title: '${meta.emoji}  ${meta.title}',
      color: widget.zone.color,
      consigne: mission.consigne,
      hint: mission.hint,
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            // Compteur de saisons
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: List.generate(
                mission.seasons,
                (i) => Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 3),
                  child: Icon(
                    i < season - 1 || locked
                        ? Icons.wb_sunny_rounded
                        : Icons.wb_sunny_outlined,
                    size: 26,
                    color: i < season - 1 || locked
                        ? const Color(0xFFFFC93D)
                        : Colors.blueGrey.shade300,
                  ),
                ),
              ),
            ),
            Text(
              locked
                  ? 'Mission accomplie !'
                  : 'Saison $season / ${mission.seasons}',
              style:
                  const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 10),
            // La prairie
            _populationRow('🌿', herbe, const Color(0xFFE3F6E8)),
            _populationRow('🐰', lapins, const Color(0xFFFDF3EC)),
            _populationRow('🦊', renards, const Color(0xFFFFEFE0)),
            const SizedBox(height: 10),
            // Journal de la saison
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(16),
                border: Border.all(color: Colors.blueGrey.shade200, width: 2),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  for (final line in log)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 4),
                      child: Text(line,
                          style:
                              const TextStyle(fontSize: 14, height: 1.3)),
                    ),
                ],
              ),
            ),
            const SizedBox(height: 12),
            // Actions du joueur
            Text('Mes actions : $actionsLeft restante(s)',
                style: const TextStyle(
                    fontSize: 15, fontWeight: FontWeight.bold)),
            const SizedBox(height: 8),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              alignment: WrapAlignment.center,
              children: [
                _actionChip('🌱', 'Planter\n+3 herbes', () {
                  _action(() => herbe = (herbe + 3).clamp(0, 15),
                      '🌱 Tu as planté 3 herbes !');
                }),
                _actionChip('🐰', 'Accueillir\nun lapin', () {
                  _action(() => lapins = (lapins + 1).clamp(0, 15),
                      '🐰 Un lapin rejoint la forêt !');
                }),
                _actionChip('🦊', 'Accueillir\nun renard', () {
                  _action(() => renards = (renards + 1).clamp(0, 8),
                      '🦊 Un renard rejoint la forêt !');
                }),
                _actionChip('🧺', 'Déplacer\nun lapin', () {
                  if (lapins <= 0) {
                    showOops(context, 'Il n\'y a pas de lapin à déplacer !');
                    return;
                  }
                  _action(() => lapins--,
                      '🧺 Un lapin part vers une autre forêt.');
                }),
                _actionChip('🚚', 'Déplacer\nun renard', () {
                  if (renards <= 0) {
                    showOops(context, 'Il n\'y a pas de renard à déplacer !');
                    return;
                  }
                  _action(() => renards--,
                      '🚚 Un renard part vers une autre forêt.');
                }),
              ],
            ),
            const SizedBox(height: 16),
            FilledButton(
              style: FilledButton.styleFrom(
                backgroundColor: widget.zone.color,
                padding:
                    const EdgeInsets.symmetric(horizontal: 36, vertical: 16),
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(20)),
              ),
              onPressed: locked ? null : _nextSeason,
              child: const Text('☀️  Saison suivante',
                  style:
                      TextStyle(fontSize: 19, fontWeight: FontWeight.bold)),
            ),
            const SizedBox(height: 10),
          ],
        ),
      ),
    );
  }

  Widget _populationRow(String emoji, int count, Color bg) {
    return Container(
      width: double.infinity,
      margin: const EdgeInsets.only(bottom: 6),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
      decoration: BoxDecoration(
        color: bg,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Row(
        children: [
          SizedBox(
            width: 44,
            child: Text('$emoji $count',
                style: const TextStyle(
                    fontSize: 16, fontWeight: FontWeight.bold)),
          ),
          Expanded(
            child: Text(
              count > 0 ? emoji * count : '—',
              style: const TextStyle(fontSize: 18),
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }

  Widget _actionChip(String emoji, String label, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 96,
        padding: const EdgeInsets.symmetric(vertical: 10),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: widget.zone.color, width: 2),
        ),
        child: Column(
          children: [
            Text(emoji, style: const TextStyle(fontSize: 26)),
            const SizedBox(height: 2),
            Text(label,
                textAlign: TextAlign.center,
                style: const TextStyle(
                    fontSize: 12, fontWeight: FontWeight.bold)),
          ],
        ),
      ),
    );
  }
}
