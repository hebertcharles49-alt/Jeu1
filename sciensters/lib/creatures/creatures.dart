import 'package:flutter/material.dart';

/// Une zone du monde (= une matière scolaire).
class Zone {
  final String id;
  final String name;
  final String emoji;
  final Color color;
  final String description;
  final String gameId;
  final String gameTitle;

  const Zone({
    required this.id,
    required this.name,
    required this.emoji,
    required this.color,
    required this.description,
    required this.gameId,
    required this.gameTitle,
  });
}

const zones = [
  Zone(
    id: 'physique',
    name: 'Vallée Électrique',
    emoji: '⚡',
    color: Color(0xFF5C9CE6),
    description: 'Le courant est coupé ! Volty est coincé dans le noir.',
    gameId: 'circuit',
    gameTitle: 'Allume l\'ampoule',
  ),
  Zone(
    id: 'chimie',
    name: 'Marais des Potions',
    emoji: '🧪',
    color: Color(0xFF9C6ADE),
    description: 'La rivière est gelée ! Gouttix est prisonnier de la glace.',
    gameId: 'etats_eau',
    gameTitle: 'Les états de l\'eau',
  ),
  Zone(
    id: 'svt',
    name: 'Forêt Vivante',
    emoji: '🌿',
    color: Color(0xFF57B26A),
    description: 'La chaîne alimentaire est cassée ! Renardeau a tout mélangé.',
    gameId: 'chaine',
    gameTitle: 'Qui mange qui ?',
  ),
];

Zone zoneForGame(String gameId) =>
    zones.firstWhere((z) => z.gameId == gameId);

Zone zoneById(String zoneId) => zones.firstWhere((z) => z.id == zoneId);

Creature creatureForZone(String zoneId) =>
    creatureForGame(zoneById(zoneId).gameId);

/// Un stade d'évolution d'un Scienster.
class CreatureStage {
  final String name;
  final String description;
  final List<String> grid;

  const CreatureStage({
    required this.name,
    required this.description,
    required this.grid,
  });
}

/// Un Scienster : une créature liée à un mini-jeu.
class Creature {
  final String id;
  final String gameId;
  final String funFact;
  final Map<String, Color> palette;
  final List<CreatureStage> stages; // 3 stades

  const Creature({
    required this.id,
    required this.gameId,
    required this.funFact,
    required this.palette,
    required this.stages,
  });

  /// stage entre 1 et 3.
  CreatureStage stageInfo(int stage) => stages[(stage.clamp(1, 3)) - 1];
}

const creatures = [
  Creature(
    id: 'volty',
    gameId: 'circuit',
    funFact:
        'Le savais-tu ? L\'électricité voyage presque à la vitesse de la lumière !',
    palette: {
      'Y': Color(0xFFFFE24F),
      'B': Color(0xFF2B2B2B),
      'O': Color(0xFFFF8A3D),
      'C': Color(0xFF4DD0E1),
    },
    stages: [
      CreatureStage(
        name: 'Volty',
        description: 'Un petit éclair timide qui adore les circuits fermés.',
        grid: [
          '....YY....',
          '...YYY....',
          '..YYYYYY..',
          '.YYBYYBYY.',
          '..YYYYYY..',
          '...YYYY...',
          '..YYY.....',
          '.YYY......',
          '..YY......',
          '...Y......',
        ],
      ),
      CreatureStage(
        name: 'Voltar',
        description: 'Il a grandi ! Ses bras crépitent d\'énergie.',
        grid: [
          '.....YYY....',
          '....YYYY....',
          '...YYYYYY...',
          '..YYBYYBYY..',
          '..YYYYYYYY..',
          '.O.YYYYYY.O.',
          '.OO.YYYY.OO.',
          '....YYY.....',
          '...YYY......',
          '..YYY.......',
          '...YY.......',
          '....Y.......',
        ],
      ),
      CreatureStage(
        name: 'Voltrym',
        description: 'Le roi de la foudre. Sa couronne brille dans la nuit.',
        grid: [
          '...C.C.C....',
          '...CCCCC....',
          '....YYYY....',
          '...YYYYYY...',
          '..YYBYYBYY..',
          '..YYYYYYYY..',
          '.O.YYYYYY.O.',
          '.OO.YYYY.OO.',
          '....YYY.....',
          '...YYY......',
          '..YYYY......',
          '...YYY......',
          '....Y.......',
        ],
      ),
    ],
  ),
  Creature(
    id: 'gouttix',
    gameId: 'etats_eau',
    funFact:
        'Le savais-tu ? L\'eau est la seule matière qu\'on trouve naturellement en solide, liquide ET gaz sur Terre !',
    palette: {
      'A': Color(0xFF4FC3F7),
      'K': Color(0xFF2B2B2B),
      'W': Color(0xFFFFFFFF),
      'L': Color(0xFFB3E5FC),
      'R': Color(0xFFF48FB1),
    },
    stages: [
      CreatureStage(
        name: 'Gouttix',
        description: 'Une petite goutte d\'eau toute ronde et très curieuse.',
        grid: [
          '....A.....',
          '....AA....',
          '...AAAA...',
          '..AAAAAA..',
          '.AAKAAKAA.',
          '.AAAAAAAA.',
          '.AWAAAAAA.',
          '..AAAAAA..',
          '...AAAA...',
          '....AA....',
        ],
      ),
      CreatureStage(
        name: 'Vaporix',
        description: 'Il s\'est évaporé ! Il flotte maintenant dans les airs.',
        grid: [
          '..L...L..L..',
          '...L.L..L...',
          '...AAAAAA...',
          '..AAAAAAAA..',
          '.AAKAAAKAA..',
          '.AAAAAAAAA..',
          '.AAAWAAAAA..',
          '..AAAAAAAA..',
          '...AAAAAA...',
          '..L..LL..L..',
        ],
      ),
      CreatureStage(
        name: 'Nuagix',
        description: 'Un nuage majestueux qui fait tomber une pluie arc-en-ciel.',
        grid: [
          '....RRRR....',
          '...R....R...',
          '..WWWWWWWW..',
          '.WWWWWWWWWW.',
          'WWKWWWWKWWW.',
          'WWWWWWWWWWW.',
          '.WWWWWWWWW..',
          '..AA.AA.AA..',
          '..A...A..A..',
        ],
      ),
    ],
  ),
  Creature(
    id: 'renardeau',
    gameId: 'chaine',
    funFact:
        'Le savais-tu ? Sans les plantes, aucune chaîne alimentaire ne pourrait exister : elles fabriquent leur nourriture grâce au soleil !',
    palette: {
      'O': Color(0xFFFF8A50),
      'K': Color(0xFF2B2B2B),
      'W': Color(0xFFFFFFFF),
      'T': Color(0xFFD84315),
      'S': Color(0xFFB0BEC5),
      'C': Color(0xFFFFC93D),
    },
    stages: [
      CreatureStage(
        name: 'Renardeau',
        description: 'Un bébé renard malicieux qui connaît toute la forêt.',
        grid: [
          '.O......O.',
          '.OO....OO.',
          '.OOOOOOOO.',
          '.OKOOOOKO.',
          '.OOOWWOOO.',
          '..OOWWOO..',
          '...OOOO...',
          '..OOOOOO..',
          '..OO..OO..',
          '..O....O..',
        ],
      ),
      CreatureStage(
        name: 'Renardor',
        description: 'Un renard adulte à la queue flamboyante.',
        grid: [
          '.O.......O..',
          '.OO.....OO..',
          '.OOO...OOO..',
          '.OOOOOOOOO..',
          '.OKOOOOOKO..',
          '.OOOOWOOOO..',
          '..OOOWWOO...',
          '...OOOOO..T.',
          '..OOOOOOO.TT',
          '..OOO.OOO.T.',
          '..OO...OO...',
        ],
      ),
      CreatureStage(
        name: 'Renargent',
        description: 'Le légendaire renard argenté, gardien de la Forêt Vivante.',
        grid: [
          '...C.C.C....',
          '...CCCCC....',
          '.S.......S..',
          '.SS.....SS..',
          '.SSS...SSS..',
          '.SSSSSSSSS..',
          '.SKSSSSSKS..',
          '.SSSSWSSSS..',
          '..SSSWWSS...',
          '...SSSSS..W.',
          '..SSSSSSS.WW',
          '..SSS.SSS.W.',
          '..SS...SS...',
        ],
      ),
    ],
  ),
];

Creature creatureForGame(String gameId) =>
    creatures.firstWhere((c) => c.gameId == gameId);
