/// Contenu des niveaux 2 à 5 : puzzles de câblage (physique),
/// missions du labo de potions (chimie), missions d'écosystème (SVT).
/// Difficulté croissante du CM2 à la 5e, toujours accessible :
/// indice permanent, aucune limite de temps, on recommence sans punition.
library;

// ─────────────────────── Titres des niveaux ───────────────────────

class LevelMeta {
  final String title;
  final String emoji;
  const LevelMeta(this.emoji, this.title);
}

const Map<String, Map<int, LevelMeta>> levelMeta = {
  'physique': {
    2: LevelMeta('🔌', 'Câble le courant'),
    3: LevelMeta('💡', 'Deux ampoules'),
    4: LevelMeta('🧰', 'Le câble est précieux'),
    5: LevelMeta('🏆', 'Portes logiques ET / OU'),
  },
  'chimie': {
    2: LevelMeta('🫧', 'Ébullition !'),
    3: LevelMeta('🥤', 'Potions et densité'),
    4: LevelMeta('🧂', 'Trop de sel !'),
    5: LevelMeta('🏆', 'Expériences magiques'),
  },
  'svt': {
    2: LevelMeta('⚖️', 'Garde l\'équilibre'),
    3: LevelMeta('🐰', 'Invasion de lapins'),
    4: LevelMeta('🦊', 'Sauve les renards'),
    5: LevelMeta('🏆', 'La grande sécheresse'),
  },
};

// ─────────────────────── Physique : puzzles de câblage ───────────────────────
// Grille : 'P' pile, 'A' ampoule, 'I' interrupteur, 'R' rocher, '.' vide.
// Le joueur pose des blocs de câble (budget limité) pour relier
// toutes les ampoules à la pile. Les interrupteurs se ferment d'un tap.

class CircuitPuzzle {
  final String consigne;
  final String hint;
  final int wireBudget;
  final List<String> grid;

  /// Table de vérité à respecter (mode "porte logique").
  /// Chaque entrée : états des interrupteurs (dans l'ordre de lecture,
  /// '1' = fermé) → l'ampoule doit-elle être allumée ?
  /// Vide = mode normal (gagné dès que tout brille).
  final List<(String, bool)> truthTable;

  const CircuitPuzzle({
    required this.consigne,
    required this.hint,
    required this.wireBudget,
    required this.grid,
    this.truthTable = const [],
  });
}

const Map<int, List<CircuitPuzzle>> circuitPuzzlesByLevel = {
  2: [
    CircuitPuzzle(
      consigne: 'Pose des câbles pour relier l\'ampoule à la pile !',
      hint: 'Tape sur une case vide pour poser un câble. '
          'Le courant passe de case en case, en évitant les rochers.',
      wireBudget: 8,
      grid: [
        'P.R.A',
        '..R..',
        '.....',
      ],
    ),
    CircuitPuzzle(
      consigne: 'Câble le circuit, puis FERME l\'interrupteur !',
      hint: 'Tape sur l\'interrupteur 🎚️ pour le fermer : '
          'ouvert, il bloque le courant.',
      wireBudget: 3,
      grid: [
        'P.I.A',
        '.....',
      ],
    ),
  ],
  3: [
    CircuitPuzzle(
      consigne: 'Allume les DEUX ampoules en même temps !',
      hint: 'La pile est au milieu : tire un câble de chaque côté.',
      wireBudget: 4,
      grid: [
        'A.P.A',
        '.....',
      ],
    ),
    CircuitPuzzle(
      consigne: 'Deux ampoules, un interrupteur : allume tout !',
      hint: 'Un câble sous l\'interrupteur peut distribuer le courant '
          'vers le bas. Et n\'oublie pas de fermer l\'interrupteur !',
      wireBudget: 4,
      grid: [
        'P.I.A',
        '.....',
        '..A..',
      ],
    ),
  ],
  4: [
    CircuitPuzzle(
      consigne: 'Attention : le câble est compté juste !',
      hint: 'Cherche le chemin le plus COURT qui contourne les rochers. '
          'Tape sur un câble pour le reprendre.',
      wireBudget: 6,
      grid: [
        'P.R..',
        '..R.A',
        '.....',
      ],
    ),
    CircuitPuzzle(
      consigne: 'Trois ampoules… avec seulement 2 câbles !',
      hint: 'Un seul câble bien placé peut toucher plusieurs choses '
          'à la fois : regarde ses 4 voisins !',
      wireBudget: 2,
      grid: [
        '.A...',
        'A.P.A',
        '.....',
      ],
    ),
  ],
  5: [
    CircuitPuzzle(
      consigne: 'Porte ET : la lampe s\'allume SEULEMENT si les DEUX '
          'interrupteurs sont fermés !',
      hint: 'Mets les deux interrupteurs sur le MÊME chemin, l\'un après '
          'l\'autre (en série). Puis appuie sur TESTER : le jeu essaiera '
          'toutes les combinaisons ! Attention : un câble qui contourne '
          'un interrupteur fera rater le test.',
      wireBudget: 3,
      grid: [
        'P.I.I.A',
        '.......',
      ],
      truthTable: [
        ('00', false),
        ('10', false),
        ('01', false),
        ('11', true),
      ],
    ),
    CircuitPuzzle(
      consigne: 'Porte OU : CHAQUE interrupteur doit pouvoir allumer '
          'la lampe tout seul !',
      hint: 'Fais DEUX chemins séparés entre la pile et la lampe, '
          'un par interrupteur (en dérivation). Puis TESTER !',
      wireBudget: 8,
      grid: [
        '.I...',
        'P...A',
        '.I...',
      ],
      truthTable: [
        ('00', false),
        ('10', true),
        ('01', true),
        ('11', true),
      ],
    ),
    CircuitPuzzle(
      consigne: 'Le défi final : allume les TROIS ampoules !',
      hint: 'Avance étape par étape : d\'abord l\'ampoule du haut avec '
          'son interrupteur, puis celle de gauche, puis la dernière.',
      wireBudget: 12,
      grid: [
        'P.I..A',
        '......',
        '.R.R..',
        'A..I.A',
      ],
    ),
  ],
};

// ─────────────────────── Chimie : labo de potions ───────────────────────

class PotionState {
  int temp;
  int dissolved;
  int deposit;
  bool hasOil;
  bool hasHoney;
  bool hasSoap;
  bool hasPeroxide;
  bool foam; // mousse géante (dentifrice d'éléphant !)
  bool waterGone;
  PotionState({
    this.temp = 20,
    this.dissolved = 0,
    this.deposit = 0,
    this.hasOil = false,
    this.hasHoney = false,
    this.hasSoap = false,
    this.hasPeroxide = false,
    this.foam = false,
    this.waterGone = false,
  });
}

class PotionMission {
  final String consigne;
  final String hint;
  final String successNote;
  final int startTemp;
  final int startDissolved;
  final bool Function(PotionState) goal;
  const PotionMission({
    required this.consigne,
    required this.hint,
    required this.successNote,
    required this.goal,
    this.startTemp = 20,
    this.startDissolved = 0,
  });
}

final Map<int, List<PotionMission>> potionMissionsByLevel = {
  2: [
    PotionMission(
      consigne: 'Fais fondre la glace, puis fais BOUILLIR l\'eau !',
      hint: 'Chauffe ! La glace fond à 0 °C, l\'eau bout à 100 °C. '
          'Regarde le thermomètre : il marque une pause aux paliers.',
      successNote: 'Ça bout ! 🫧 Fusion à 0 °C, ébullition à 100 °C : '
          'les deux paliers de l\'eau.',
      startTemp: -20,
      goal: _goalBoil,
    ),
    PotionMission(
      consigne: 'Maintenant, gèle toute la potion !',
      hint: 'Refroidis jusqu\'à passer sous 0 °C.',
      successNote: 'Tout est gelé ! Liquide → solide : '
          'c\'est la solidification.',
      startTemp: 40,
      goal: _goalFreeze,
    ),
  ],
  3: [
    PotionMission(
      consigne: 'Potion invisible : dissous 2 sels dans l\'eau !',
      hint: 'Le sel ne se dissout que dans l\'eau LIQUIDE. '
          'Si c\'est gelé, chauffe d\'abord !',
      successNote: 'Le sel a disparu… mais il est toujours là, dissous ! '
          'Le mélange est HOMOGÈNE : on ne voit qu\'un liquide.',
      goal: _goalDissolve2,
    ),
    PotionMission(
      consigne: 'Verse l\'huile… et observe bien !',
      hint: 'L\'huile ne se dissout pas : appuie sur le bouton huile.',
      successNote: 'L\'huile flotte en couche au-dessus : le mélange est '
          'HÉTÉROGÈNE, on voit deux parties.',
      goal: _goalOil,
    ),
    PotionMission(
      consigne: 'Tour de densité : verse le miel ET l\'huile !',
      hint: 'Chaque liquide a sa densité : le plus lourd coule, '
          'le plus léger flotte. Verse les deux et regarde les étages !',
      successNote: 'Trois étages ! Le miel (dense) coule au fond, l\'eau au '
          'milieu, l\'huile (légère) flotte : c\'est la DENSITÉ.',
      goal: _goalDensityTower,
    ),
  ],
  4: [
    PotionMission(
      consigne: 'Ajoute du sel jusqu\'à ce qu\'un dépôt apparaisse au fond !',
      hint: 'Continue d\'ajouter du sel… l\'eau a une limite !',
      successNote: 'L\'eau ne peut plus rien dissoudre : la solution est '
          'SATURÉE, le sel en trop tombe au fond.',
      goal: _goalSaturate,
    ),
  ],
  5: [
    PotionMission(
      consigne: 'Du sel est dissous, invisible… Récupère-le !',
      hint: 'Fais bouillir LONGTEMPS : quand toute l\'eau sera partie en '
          'vapeur, que restera-t-il ?',
      successNote: 'Magique ! L\'eau s\'est évaporée, le sel est resté en '
          'cristaux : l\'ÉVAPORATION permet de séparer un mélange.',
      startDissolved: 3,
      goal: _goalEvaporate,
    ),
    PotionMission(
      consigne: 'DENTIFRICE D\'ÉLÉPHANT : mélange l\'eau oxygénée '
          'et le savon !',
      hint: 'Verse les deux ingrédients spéciaux ⚗️ et 🧼… '
          'et recule-toi ! 😄',
      successNote: 'PSSSHHH ! 🫧 La réaction chimique libère plein de gaz '
          'd\'un coup, et le savon le piège en mousse géante. Cette '
          'expérience existe pour de vrai dans les labos !',
      goal: _goalElephantToothpaste,
    ),
  ],
};

bool _goalBoil(PotionState s) => s.temp >= 100 && !s.waterGone;
bool _goalFreeze(PotionState s) => s.temp <= 0 && !s.waterGone;
bool _goalDissolve2(PotionState s) => s.dissolved >= 2;
bool _goalOil(PotionState s) => s.hasOil;
bool _goalSaturate(PotionState s) => s.deposit >= 1;
bool _goalEvaporate(PotionState s) =>
    s.waterGone && (s.dissolved + s.deposit) > 0;
bool _goalDensityTower(PotionState s) => s.hasOil && s.hasHoney;
bool _goalElephantToothpaste(PotionState s) => s.foam;

// ─────────────────────── SVT : simulation d'écosystème ───────────────────────

class EcoMission {
  final String consigne;
  final String hint;
  final String successNote;
  final int herbe;
  final int lapins;
  final int renards;
  final int seasons; // nombre de saisons à tenir
  final int actionsPerSeason;
  final int herbeGrowth; // pousse de l'herbe par saison
  const EcoMission({
    required this.consigne,
    required this.hint,
    required this.successNote,
    required this.herbe,
    required this.lapins,
    required this.renards,
    required this.seasons,
    this.actionsPerSeason = 2,
    this.herbeGrowth = 3,
  });
}

const Map<int, EcoMission> ecoMissionsByLevel = {
  2: EcoMission(
    consigne: 'Garde herbe, lapins et renards en vie pendant 4 saisons !',
    hint: 'Chaque saison : l\'herbe pousse, les lapins mangent l\'herbe, '
        'les renards mangent des lapins. Observe, puis agis si besoin !',
    successNote: 'Bravo ! Quand chacun a de quoi manger, '
        'l\'écosystème est en ÉQUILIBRE.',
    herbe: 6,
    lapins: 3,
    renards: 1,
    seasons: 4,
  ),
  3: EcoMission(
    consigne: 'Trop de lapins ! Rétablis l\'équilibre pendant 5 saisons.',
    hint: 'Huit lapins, ça mange énormément d\'herbe… Plante de l\'herbe, '
        'déplace des lapins, ou accueille un renard !',
    successNote: 'Bien joué ! Sans prédateurs, les lapins deviennent trop '
        'nombreux et l\'herbe disparaît : tout est lié.',
    herbe: 4,
    lapins: 8,
    renards: 2,
    seasons: 5,
  ),
  4: EcoMission(
    consigne: 'Les renards ont faim ! Fais tenir tout le monde 5 saisons.',
    hint: 'Trois renards mais très peu de lapins… Accueille vite des '
        'lapins, et pense à planter de l\'herbe pour les nourrir !',
    successNote: 'Sauvés ! Un prédateur disparaît quand ses proies '
        'manquent : protéger les proies protège aussi le prédateur.',
    herbe: 8,
    lapins: 2,
    renards: 3,
    seasons: 5,
  ),
  5: EcoMission(
    consigne: 'Grande sécheresse : l\'herbe pousse à peine ! Tiens 6 saisons.',
    hint: 'Avec la sécheresse, l\'herbe pousse très lentement : plante '
        'souvent, et garde peu de lapins pour économiser l\'herbe.',
    successNote: 'Champion ! Quand le climat change, toute la chaîne '
        'alimentaire est bousculée : voilà pourquoi on protège la nature.',
    herbe: 6,
    lapins: 3,
    renards: 1,
    seasons: 6,
    herbeGrowth: 1,
  ),
};
