import 'package:flutter/material.dart';

import '../content/missions.dart';
import '../creatures/creatures.dart';
import 'game_framework.dart';

/// Le labo de potions : un bécher qu'on chauffe, refroidit et remplit
/// d'ingrédients. Les missions font découvrir les changements d'état,
/// la dissolution, la saturation et l'évaporation en EXPÉRIMENTANT.
class PotionLabGame extends StatefulWidget {
  final Zone zone;
  final int level;

  const PotionLabGame({super.key, required this.zone, required this.level});

  @override
  State<PotionLabGame> createState() => _PotionLabGameState();
}

class _PotionLabGameState extends State<PotionLabGame>
    with SingleTickerProviderStateMixin {
  late final List<PotionMission> missions;
  int missionIndex = 0;
  bool locked = false;

  late PotionState s;
  int boilTaps = 0; // taps de chauffe supplémentaires à 100 °C

  late final AnimationController _bubbles;

  PotionMission get mission => missions[missionIndex];

  @override
  void initState() {
    super.initState();
    missions = potionMissionsByLevel[widget.level]!;
    _resetMission();
    _bubbles = AnimationController(
        vsync: this, duration: const Duration(milliseconds: 900))
      ..repeat(reverse: true);
  }

  @override
  void dispose() {
    _bubbles.dispose();
    super.dispose();
  }

  void _resetMission() {
    s = PotionState(
      temp: mission.startTemp,
      dissolved: mission.startDissolved,
    );
    boilTaps = 0;
  }

  Future<void> _afterAction() async {
    if (locked) return;
    if (mission.goal(s)) {
      setState(() => locked = true);
      showNice(context, mission.successNote);
      await Future<void>.delayed(const Duration(milliseconds: 1600));
      if (!mounted) return;
      if (missionIndex == missions.length - 1) {
        await showVictory(context, widget.zone.id, widget.level);
      } else {
        setState(() {
          missionIndex++;
          locked = false;
          _resetMission();
        });
      }
    }
  }

  void _heat() {
    if (locked) return;
    if (s.waterGone) {
      showOops(context, 'Il n\'y a plus d\'eau à chauffer !');
      return;
    }
    softFeedback();
    setState(() {
      if (s.temp < 100) {
        s.temp += 20;
        if (s.temp > 100) s.temp = 100;
        if (s.temp == 0) {
          showNice(context, 'Palier de fusion : la glace fond à 0 °C ! 🧊➡️💧');
        } else if (s.temp == 100) {
          showNice(context, 'Palier d\'ébullition : ça bout à 100 °C ! 🫧');
        }
      } else {
        boilTaps++;
        if (boilTaps >= 3) {
          s.waterGone = true;
          s.hasOil = false;
          showNice(context, 'Toute l\'eau est partie en vapeur ! ☁️');
        } else {
          showNice(context, 'L\'eau s\'évapore… continue de chauffer ! ♨️');
        }
      }
    });
    _afterAction();
  }

  void _cool() {
    if (locked) return;
    if (s.waterGone) {
      showOops(context, 'Trop tard : il n\'y a plus d\'eau à refroidir !');
      return;
    }
    softFeedback();
    setState(() {
      s.temp -= 20;
      if (s.temp < -40) s.temp = -40;
      boilTaps = 0;
      if (s.temp == 0) {
        showNice(context,
            'Palier de solidification : l\'eau gèle à 0 °C ! 💧➡️🧊');
      }
    });
    _afterAction();
  }

  void _addSalt() {
    if (locked) return;
    if (s.waterGone) {
      showOops(context, 'Plus d\'eau ! Le sel resterait tout sec.');
      return;
    }
    if (s.temp <= 0) {
      showOops(context,
          'La glace ne peut rien dissoudre ! Fais fondre d\'abord. 🔥');
      return;
    }
    softFeedback();
    setState(() {
      if (s.dissolved < 4) {
        s.dissolved++;
      } else {
        s.deposit++;
        showNice(context, 'Saturé ! Ce sel-là tombe au fond. ⬇️');
      }
    });
    _afterAction();
  }

  /// Vérifie que la potion est liquide avant d'ajouter un ingrédient.
  bool _liquidCheck() {
    if (locked) return false;
    if (s.waterGone) {
      showOops(context, 'Plus d\'eau dans le bécher ! 😅');
      return false;
    }
    if (s.temp <= 0) {
      showOops(context, 'Tout est gelé ! Fais fondre d\'abord. 🔥');
      return false;
    }
    return true;
  }

  void _addOil() {
    if (!_liquidCheck()) return;
    softFeedback();
    setState(() => s.hasOil = true);
    _afterAction();
  }

  void _addHoney() {
    if (!_liquidCheck()) return;
    softFeedback();
    setState(() => s.hasHoney = true);
    showNice(context, 'Gloup ! Le miel coule tout au fond… 🍯⬇️');
    _afterAction();
  }

  void _addSoap() {
    if (!_liquidCheck()) return;
    softFeedback();
    setState(() {
      s.hasSoap = true;
      if (s.hasPeroxide) s.foam = true;
    });
    if (s.foam) {
      showNice(context, 'PSSSHHH !!! 🫧🫧🫧');
    }
    _afterAction();
  }

  void _addPeroxide() {
    if (!_liquidCheck()) return;
    softFeedback();
    setState(() {
      s.hasPeroxide = true;
      if (s.hasSoap) s.foam = true;
    });
    if (s.foam) {
      showNice(context, 'PSSSHHH !!! 🫧🫧🫧');
    } else {
      showNice(context, 'Eau oxygénée versée… il manque un ingrédient ! 🤔');
    }
    _afterAction();
  }

  /// Ingrédients disponibles selon le niveau (pour ne pas surcharger).
  List<String> get _ingredients {
    switch (widget.level) {
      case 3:
        return ['sel', 'huile', 'miel'];
      case 4:
        return ['sel'];
      case 5:
        return ['sel', 'savon', 'oxy'];
      default:
        return [];
    }
  }

  @override
  Widget build(BuildContext context) {
    final meta = levelMeta['chimie']![widget.level]!;
    return GameScaffold(
      title: '${meta.emoji}  ${meta.title}',
      color: widget.zone.color,
      consigne: mission.consigne,
      hint: mission.hint,
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(12),
        child: Column(
          children: [
            // Progression des missions du niveau
            if (missions.length > 1)
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: List.generate(
                  missions.length,
                  (i) => Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 4),
                    child: Icon(
                      i < missionIndex || (i == missionIndex && locked)
                          ? Icons.check_circle_rounded
                          : i == missionIndex
                              ? Icons.radio_button_checked
                              : Icons.circle_outlined,
                      size: 24,
                      color: i < missionIndex || (i == missionIndex && locked)
                          ? const Color(0xFF57B26A)
                          : widget.zone.color,
                    ),
                  ),
                ),
              ),
            const SizedBox(height: 8),
            Row(
              crossAxisAlignment: CrossAxisAlignment.center,
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _thermometer(),
                const SizedBox(width: 24),
                _beaker(),
              ],
            ),
            const SizedBox(height: 20),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              alignment: WrapAlignment.center,
              children: [
                _actionButton('🔥', 'CHAUFFER', const Color(0xFFFF7043), _heat),
                _actionButton(
                    '❄️', 'REFROIDIR', const Color(0xFF4FC3F7), _cool),
                if (_ingredients.contains('sel'))
                  _actionButton('🧂', 'SEL', const Color(0xFF9C6ADE), _addSalt),
                if (_ingredients.contains('huile'))
                  _actionButton(
                      '🫒', 'HUILE', const Color(0xFFB8912E), _addOil),
                if (_ingredients.contains('miel'))
                  _actionButton(
                      '🍯', 'MIEL', const Color(0xFFE0A030), _addHoney),
                if (_ingredients.contains('savon'))
                  _actionButton(
                      '🧼', 'SAVON', const Color(0xFF5C9CE6), _addSoap),
                if (_ingredients.contains('oxy'))
                  _actionButton('⚗️', 'EAU OXYGÉNÉE',
                      const Color(0xFF57B26A), _addPeroxide),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _thermometer() {
    final fraction = ((s.temp + 40) / 140).clamp(0.0, 1.0);
    final isPalier = s.temp == 0 || s.temp == 100;
    return Column(
      children: [
        Text('${s.temp} °C',
            style:
                const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
        const SizedBox(height: 6),
        Container(
          width: 26,
          height: 180,
          decoration: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.circular(13),
            border: Border.all(color: Colors.blueGrey, width: 2),
          ),
          child: Align(
            alignment: Alignment.bottomCenter,
            child: AnimatedContainer(
              duration: const Duration(milliseconds: 350),
              width: 26,
              height: 180 * fraction,
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(13),
                color: Color.lerp(const Color(0xFF4FC3F7),
                    const Color(0xFFFF7043), fraction),
              ),
            ),
          ),
        ),
        const SizedBox(height: 6),
        SizedBox(
          height: 30,
          child: isPalier
              ? Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 10, vertical: 4),
                  decoration: BoxDecoration(
                    color: const Color(0xFFFFF3C4),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: const Text('PALIER !',
                      style: TextStyle(
                          fontSize: 13, fontWeight: FontWeight.bold)),
                )
              : null,
        ),
      ],
    );
  }

  Widget _beaker() {
    final frozen = s.temp <= 0 && !s.waterGone;
    final boiling = s.temp >= 100 && !s.waterGone;
    return Column(
      children: [
        // Vapeur ou mousse géante au-dessus
        SizedBox(
          height: 44,
          child: s.foam
              ? ScaleTransition(
                  scale: Tween(begin: 0.8, end: 1.15).animate(_bubbles),
                  child: const Text('🫧🫧🫧\n🫧🫧',
                      textAlign: TextAlign.center,
                      style: TextStyle(fontSize: 17)),
                )
              : (boiling || s.waterGone)
                  ? FadeTransition(
                      opacity:
                          Tween(begin: 0.35, end: 1.0).animate(_bubbles),
                      child: const Text('☁️ ☁️',
                          style: TextStyle(fontSize: 22)),
                    )
                  : null,
        ),
        Container(
          width: 130,
          height: 170,
          decoration: BoxDecoration(
            color: Colors.white,
            border: Border.all(color: Colors.blueGrey, width: 3),
            borderRadius: const BorderRadius.only(
              bottomLeft: Radius.circular(24),
              bottomRight: Radius.circular(24),
              topLeft: Radius.circular(4),
              topRight: Radius.circular(4),
            ),
          ),
          child: Stack(
            alignment: Alignment.bottomCenter,
            children: [
              // Le contenu
              if (!s.waterGone)
                Positioned(
                  bottom: 0,
                  left: 0,
                  right: 0,
                  child: AnimatedContainer(
                    duration: const Duration(milliseconds: 400),
                    height: 120,
                    decoration: BoxDecoration(
                      color: frozen
                          ? const Color(0xFFD6F0FA)
                          : s.dissolved > 0
                              ? const Color(0xFF9AD1EC)
                              : const Color(0xFFAEDCF2),
                      borderRadius: const BorderRadius.only(
                        bottomLeft: Radius.circular(21),
                        bottomRight: Radius.circular(21),
                      ),
                    ),
                  ),
                ),
              // Glaçons
              if (frozen)
                const Positioned(
                  bottom: 40,
                  child: Text('🧊 🧊 🧊', style: TextStyle(fontSize: 24)),
                ),
              // Bulles d'ébullition
              if (boiling)
                Positioned(
                  bottom: 30,
                  child: FadeTransition(
                    opacity: Tween(begin: 0.2, end: 1.0).animate(_bubbles),
                    child: const Text('🫧  🫧\n 🫧',
                        textAlign: TextAlign.center,
                        style: TextStyle(fontSize: 20)),
                  ),
                ),
              // Couche de miel au fond (le plus dense)
              if (s.hasHoney && !frozen && !s.waterGone)
                Positioned(
                  bottom: 0,
                  left: 3,
                  right: 3,
                  child: Container(
                    height: 24,
                    decoration: const BoxDecoration(
                      color: Color(0xFFD68910),
                      borderRadius: BorderRadius.only(
                        bottomLeft: Radius.circular(20),
                        bottomRight: Radius.circular(20),
                      ),
                    ),
                  ),
                ),
              // Mousse qui déborde !
              if (s.foam)
                Positioned(
                  top: 0,
                  child: ScaleTransition(
                    scale: Tween(begin: 0.9, end: 1.1).animate(_bubbles),
                    child: const Text('🫧🫧🫧🫧\n🫧🫧🫧',
                        textAlign: TextAlign.center,
                        style: TextStyle(fontSize: 18)),
                  ),
                ),
              // Couche d'huile
              if (s.hasOil && !frozen && !s.waterGone)
                Positioned(
                  bottom: 108,
                  left: 4,
                  right: 4,
                  child: Container(
                    height: 16,
                    decoration: BoxDecoration(
                      color: const Color(0xFFF2D272),
                      borderRadius: BorderRadius.circular(8),
                    ),
                  ),
                ),
              // Dépôt de sel au fond
              if (s.deposit > 0 && !s.waterGone)
                Positioned(
                  bottom: 4,
                  child: Text('▫' * (s.deposit * 2).clamp(2, 8),
                      style: const TextStyle(
                          fontSize: 16, color: Colors.white)),
                ),
              // Cristaux après évaporation
              if (s.waterGone && (s.dissolved + s.deposit) > 0)
                const Positioned(
                  bottom: 8,
                  child: Text('✨🧂✨', style: TextStyle(fontSize: 26)),
                ),
            ],
          ),
        ),
        const SizedBox(height: 6),
        Text(
          s.foam
              ? 'MOUSSE GÉANTE !!! 🐘'
              : s.waterGone
                  ? 'Eau évaporée !'
                  : frozen
                      ? 'GLACE'
                      : boiling
                          ? 'ÇA BOUT !'
                          : s.hasHoney && s.hasOil
                              ? 'Tour à 3 étages !'
                              : s.dissolved > 0
                                  ? 'Eau salée (invisible !)'
                                  : 'Eau liquide',
          style: const TextStyle(fontSize: 15, fontWeight: FontWeight.bold),
        ),
      ],
    );
  }

  Widget _actionButton(
      String emoji, String label, Color color, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(18),
          border: Border.all(color: color, width: 3),
          boxShadow: const [
            BoxShadow(
                color: Colors.black12, blurRadius: 4, offset: Offset(0, 2))
          ],
        ),
        child: Column(
          children: [
            Text(emoji, style: const TextStyle(fontSize: 30)),
            Text(label,
                style: TextStyle(
                    fontSize: 13, fontWeight: FontWeight.bold, color: color)),
          ],
        ),
      ),
    );
  }
}
