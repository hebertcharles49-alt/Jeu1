/// Contenu des niveaux 2 à 5 de chaque zone.
/// Difficulté croissante : niveau 2 ≈ CM2/6e … niveau 5 ≈ 5e.
/// Règles d'accessibilité : une question = une phrase courte,
/// toujours un indice, jamais de chrono, réponse fausse = encouragement.
library;

/// Un objet à trier dans l'une des deux catégories.
class SortItem {
  final String emoji;
  final String label;
  final bool inA;
  const SortItem(this.emoji, this.label, this.inA);
}

/// Une énigme : QCM ('qcm') ou jeu de tri ('tri').
class Enigme {
  final String type;
  final String question;
  final String hint;
  final String successNote;
  // QCM
  final List<String> choices;
  final int correctIndex;
  // Tri
  final String catA;
  final String catB;
  final List<SortItem> items;

  const Enigme.qcm({
    required this.question,
    required this.hint,
    required this.successNote,
    required this.choices,
    required this.correctIndex,
  })  : type = 'qcm',
        catA = '',
        catB = '',
        items = const [];

  const Enigme.tri({
    required this.question,
    required this.hint,
    required this.successNote,
    required this.catA,
    required this.catB,
    required this.items,
  })  : type = 'tri',
        choices = const [],
        correctIndex = -1;
}

/// Un niveau d'énigmes d'une zone.
class EnigmeLevel {
  final int level;
  final String title;
  final String emoji;
  final List<Enigme> enigmes;
  const EnigmeLevel({
    required this.level,
    required this.title,
    required this.emoji,
    required this.enigmes,
  });
}

const Map<String, List<EnigmeLevel>> enigmesByZone = {
  // ─────────────────────────── PHYSIQUE ───────────────────────────
  'physique': [
    EnigmeLevel(
      level: 2,
      title: 'Conducteur ou isolant ?',
      emoji: '🔑',
      enigmes: [
        Enigme.qcm(
          question: 'Le courant électrique passe facilement à travers…',
          hint: 'Pense aux fils électriques : ils sont en cuivre !',
          successNote: 'Oui ! Les métaux laissent passer le courant : '
              'ce sont des conducteurs.',
          choices: ['Le métal', 'Le bois', 'Le plastique'],
          correctIndex: 0,
        ),
        Enigme.tri(
          question: 'Trie les objets : conducteur ou isolant ?',
          hint: 'Si c\'est en métal, le courant passe. Sinon, il est bloqué !',
          successNote: 'Bravo ! Métal = conducteur, '
              'plastique, bois et caoutchouc = isolants.',
          catA: '⚡ Conducteur',
          catB: '🚫 Isolant',
          items: [
            SortItem('🔑', 'Clé en métal', true),
            SortItem('📏', 'Règle en plastique', false),
            SortItem('🥄', 'Cuillère en métal', true),
            SortItem('🪵', 'Bâton de bois', false),
            SortItem('🔩', 'Vis en fer', true),
            SortItem('🎈', 'Ballon en caoutchouc', false),
          ],
        ),
      ],
    ),
    EnigmeLevel(
      level: 3,
      title: 'Le circuit en série',
      emoji: '💡',
      enigmes: [
        Enigme.qcm(
          question: 'Qui donne l\'énergie au circuit ?',
          hint: 'C\'est elle qu\'on remplace quand la télécommande '
              'ne marche plus !',
          successNote: 'Exact ! La pile est le générateur : '
              'sans elle, rien ne fonctionne.',
          choices: ['L\'ampoule', 'La pile', 'Le fil'],
          correctIndex: 1,
        ),
        Enigme.qcm(
          question: 'On OUVRE l\'interrupteur. Que fait l\'ampoule ?',
          hint: 'Ouvrir l\'interrupteur, c\'est couper le chemin du courant.',
          successNote: 'Oui ! Circuit ouvert = le courant ne passe plus, '
              'l\'ampoule s\'éteint.',
          choices: ['Elle brille plus fort', 'Rien ne change', 'Elle s\'éteint'],
          correctIndex: 2,
        ),
        Enigme.qcm(
          question: 'En série, une ampoule grille. L\'autre ampoule…',
          hint: 'En série, le courant n\'a qu\'un seul chemin, '
              'comme un seul couloir.',
          successNote: 'Bien vu ! En série, si le chemin est coupé quelque '
              'part, tout s\'éteint.',
          choices: ['S\'éteint aussi', 'Reste allumée', 'Clignote'],
          correctIndex: 0,
        ),
      ],
    ),
    EnigmeLevel(
      level: 4,
      title: 'Les symboles électriques',
      emoji: '✏️',
      enigmes: [
        Enigme.qcm(
          question: 'Dans un schéma, comment dessine-t-on une lampe ?',
          hint: 'C\'est rond, avec une croix à l\'intérieur.',
          successNote: 'Oui ! La lampe se dessine : un rond avec une croix ⊗.',
          choices: ['Un rond avec une croix', 'Un carré noir', 'Un triangle'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Et la pile, comment la dessine-t-on ?',
          hint: 'Un trait pour le +, un trait pour le −… '
              'l\'un est plus grand que l\'autre.',
          successNote: 'Exact ! Deux traits : le long c\'est le +, '
              'le court c\'est le −.',
          choices: ['Un cercle', 'Une étoile', 'Un trait long et un trait court'],
          correctIndex: 2,
        ),
        Enigme.qcm(
          question: 'L\'interrupteur ouvert ressemble à…',
          hint: 'Comme un pont-levis levé : le chemin est coupé !',
          successNote: 'Bravo ! Un petit trait qui se soulève, '
              'comme une barrière ouverte.',
          choices: ['Une barrière soulevée', 'Un rond plein', 'Une flèche'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Les fils, dans un schéma, sont dessinés…',
          hint: 'On utilise une règle pour les tracer.',
          successNote: 'Oui ! Des traits bien droits, à la règle, '
              'qui relient les symboles.',
          choices: ['En zigzag', 'Par des traits droits', 'En pointillés'],
          correctIndex: 1,
        ),
      ],
    ),
    EnigmeLevel(
      level: 5,
      title: 'Maître de l\'électricité',
      emoji: '🏆',
      enigmes: [
        Enigme.qcm(
          question: 'En DÉRIVATION, une ampoule grille. L\'autre…',
          hint: 'En dérivation, chaque ampoule a son propre chemin.',
          successNote: 'Excellent ! Chaque boucle est indépendante : '
              'l\'autre ampoule reste allumée. C\'est comme ça chez toi !',
          choices: ['S\'éteint aussi', 'Reste allumée', 'Grille aussi'],
          correctIndex: 1,
        ),
        Enigme.qcm(
          question: 'Le sens conventionnel du courant va…',
          hint: 'Il sort par la grande borne de la pile.',
          successNote: 'Oui ! À l\'extérieur de la pile, le courant va '
              'de la borne + vers la borne −.',
          choices: ['Du + vers le −', 'Du − vers le +', 'Dans les deux sens'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Un court-circuit de la pile, c\'est dangereux car…',
          hint: 'Touche une pile qui a été court-circuitée… elle est brûlante !',
          successNote: 'Exact ! La pile chauffe très fort et peut s\'abîmer : '
              'on ne relie jamais ses deux bornes par un simple fil.',
          choices: [
            'La pile chauffe très fort',
            'L\'ampoule devient verte',
            'Rien, c\'est sans danger'
          ],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Pourquoi les fils sont-ils recouverts de plastique ?',
          hint: 'Le plastique est un isolant…',
          successNote: 'Bravo ! Le plastique isolant nous protège : '
              'le courant reste bien à l\'intérieur du fil.',
          choices: [
            'Pour être plus jolis',
            'Pour nous protéger du courant',
            'Pour aller plus vite'
          ],
          correctIndex: 1,
        ),
      ],
    ),
  ],
  // ─────────────────────────── CHIMIE ───────────────────────────
  'chimie': [
    EnigmeLevel(
      level: 2,
      title: 'Le nom des transformations',
      emoji: '📖',
      enigmes: [
        Enigme.qcm(
          question: 'La glace devient de l\'eau liquide : c\'est…',
          hint: 'Le fromage sur la pizza fait pareil au four : il fond !',
          successNote: 'Oui ! Solide → liquide, c\'est la FUSION.',
          choices: ['La fusion', 'La solidification', 'L\'évaporation'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'L\'eau liquide devient de la glace : c\'est…',
          hint: 'Ça se passe dans le congélateur !',
          successNote: 'Exact ! Liquide → solide, c\'est la SOLIDIFICATION.',
          choices: ['La fusion', 'La condensation', 'La solidification'],
          correctIndex: 2,
        ),
        Enigme.qcm(
          question: 'L\'eau liquide devient de la vapeur : c\'est…',
          hint: 'Ça se passe dans la casserole qui bout !',
          successNote: 'Bravo ! Liquide → gaz, c\'est la VAPORISATION.',
          choices: ['La vaporisation', 'La fusion', 'La solidification'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'La vapeur redevient des gouttes d\'eau : c\'est…',
          hint: 'C\'est la buée sur le miroir après la douche !',
          successNote: 'Oui ! Gaz → liquide, c\'est la LIQUÉFACTION '
              '(ou condensation).',
          choices: ['La fusion', 'La liquéfaction', 'La vaporisation'],
          correctIndex: 1,
        ),
      ],
    ),
    EnigmeLevel(
      level: 3,
      title: 'Les températures magiques',
      emoji: '🌡️',
      enigmes: [
        Enigme.qcm(
          question: 'La glace fond à quelle température ?',
          hint: 'C\'est un tout petit nombre… rond comme un glaçon !',
          successNote: 'Oui ! La glace fond à 0 °C, toujours.',
          choices: ['0 °C', '50 °C', '100 °C'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'L\'eau bout à quelle température ?',
          hint: 'C\'est un nombre à trois chiffres, tout rond.',
          successNote: 'Exact ! L\'eau bout à 100 °C.',
          choices: ['10 °C', '100 °C', '1000 °C'],
          correctIndex: 1,
        ),
        Enigme.qcm(
          question: 'Pendant que la glace fond, sa température…',
          hint: 'C\'est le secret du palier : le thermomètre semble bloqué !',
          successNote: 'Bien joué, c\'est le plus difficile ! Pendant toute '
              'la fusion, la température reste bloquée à 0 °C.',
          choices: ['Monte très vite', 'Descend', 'Reste à 0 °C'],
          correctIndex: 2,
        ),
      ],
    ),
    EnigmeLevel(
      level: 4,
      title: 'Mélanges de potions',
      emoji: '🥤',
      enigmes: [
        Enigme.qcm(
          question: 'Un mélange HOMOGÈNE, c\'est quand…',
          hint: 'Homogène = tout pareil partout, on ne distingue rien.',
          successNote: 'Oui ! Homogène : on ne voit qu\'une seule chose. '
              'Hétérogène : on distingue plusieurs parties.',
          choices: [
            'On ne distingue qu\'un seul liquide',
            'On voit plusieurs couches',
            'Ça fait des bulles'
          ],
          correctIndex: 0,
        ),
        Enigme.tri(
          question: 'Trie les potions : homogène ou hétérogène ?',
          hint: 'Si on voit des morceaux ou des couches → hétérogène !',
          successNote: 'Bravo, maître des potions ! L\'huile et le sable ne se '
              'mélangent pas à l\'eau, mais le sel et le sirop si.',
          catA: '🥛 Homogène',
          catB: '🧃 Hétérogène',
          items: [
            SortItem('🍹', 'Eau + sirop mélangé', true),
            SortItem('🫒', 'Eau + huile', false),
            SortItem('🏖️', 'Eau + sable', false),
            SortItem('🧂', 'Eau + sel dissous', true),
            SortItem('🍊', 'Jus avec pulpe', false),
            SortItem('💨', 'L\'air qu\'on respire', true),
          ],
        ),
      ],
    ),
    EnigmeLevel(
      level: 5,
      title: 'Maître des potions',
      emoji: '🏆',
      enigmes: [
        Enigme.qcm(
          question: 'Le sel "disparaît" dans l\'eau. En vrai, il…',
          hint: 'Goûte l\'eau… elle est salée ! Alors, vraiment disparu ?',
          successNote: 'Excellent ! Le sel est DISSOUS : invisible mais '
              'toujours là, en tout petits morceaux.',
          choices: [
            'A disparu pour toujours',
            'Est dissous, toujours là',
            'S\'est évaporé'
          ],
          correctIndex: 1,
        ),
        Enigme.qcm(
          question: '100 g d\'eau + 10 g de sel dissous. Masse totale ?',
          hint: 'Rien ne se perd : additionne !',
          successNote: 'Oui ! 110 g : la masse se conserve, même quand '
              'le sel devient invisible.',
          choices: ['100 g', '105 g', '110 g'],
          correctIndex: 2,
        ),
        Enigme.qcm(
          question: 'On met BEAUCOUP trop de sel dans l\'eau…',
          hint: 'L\'eau ne peut pas tout dissoudre, elle a une limite.',
          successNote: 'Bravo ! Quand l\'eau ne peut plus dissoudre, le '
              'surplus reste au fond : la solution est SATURÉE.',
          choices: [
            'Le surplus reste au fond',
            'Tout se dissout quand même',
            'L\'eau déborde en mousse'
          ],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Le sucre est soluble dans l\'eau. L\'huile est…',
          hint: 'Regarde une vinaigrette : l\'huile reste au-dessus !',
          successNote: 'Exact ! L\'huile est INSOLUBLE : elle ne se dissout '
              'pas et flotte sur l\'eau.',
          choices: ['Insoluble', 'Soluble aussi', 'Transformée en sucre'],
          correctIndex: 0,
        ),
      ],
    ),
  ],
  // ─────────────────────────── SVT ───────────────────────────
  'svt': [
    EnigmeLevel(
      level: 2,
      title: 'Producteurs et consommateurs',
      emoji: '🌱',
      enigmes: [
        Enigme.qcm(
          question: 'Qui fabrique sa nourriture grâce au soleil ?',
          hint: 'Ils sont verts et ne courent pas très vite… 😄',
          successNote: 'Oui ! Les plantes sont des PRODUCTEURS : elles '
              'fabriquent leur matière avec la lumière.',
          choices: ['Les renards', 'Les plantes', 'Les champignons'],
          correctIndex: 1,
        ),
        Enigme.tri(
          question: 'Trie : producteur ou consommateur ?',
          hint: 'Producteur = fabrique sa nourriture (plantes). '
              'Consommateur = mange les autres !',
          successNote: 'Bravo ! Les plantes produisent, '
              'les animaux consomment.',
          catA: '🌱 Producteur',
          catB: '🍽️ Consommateur',
          items: [
            SortItem('🌿', 'Herbe', true),
            SortItem('🐰', 'Lapin', false),
            SortItem('🌳', 'Chêne', true),
            SortItem('🦊', 'Renard', false),
            SortItem('🪸', 'Algue', true),
            SortItem('🐦', 'Mésange', false),
          ],
        ),
      ],
    ),
    EnigmeLevel(
      level: 3,
      title: 'Les régimes alimentaires',
      emoji: '🍽️',
      enigmes: [
        Enigme.qcm(
          question: 'Le lapin ne mange que des plantes : il est…',
          hint: '« Herbe » se cache dans le mot !',
          successNote: 'Oui ! Herbivore = mangeur de plantes.',
          choices: ['Carnivore', 'Omnivore', 'Herbivore'],
          correctIndex: 2,
        ),
        Enigme.qcm(
          question: 'Le renard mange surtout d\'autres animaux : il est…',
          hint: '« Carne » veut dire viande.',
          successNote: 'Exact ! Carnivore = mangeur de viande.',
          choices: ['Carnivore', 'Herbivore', 'Végétarien'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'L\'ours mange des baies ET des poissons : il est…',
          hint: '« Omni » veut dire tout !',
          successNote: 'Bravo ! Omnivore = qui mange de tout. '
              'Comme les humains !',
          choices: ['Herbivore', 'Omnivore', 'Carnivore'],
          correctIndex: 1,
        ),
        Enigme.qcm(
          question: 'Qui recycle les feuilles mortes en bonne terre ?',
          hint: 'Ils travaillent cachés sous nos pieds…',
          successNote: 'Oui ! Vers, champignons et bactéries sont les '
              'DÉCOMPOSEURS : les recycleurs de la nature.',
          choices: [
            'Les vers et les champignons',
            'Les oiseaux',
            'Les renards'
          ],
          correctIndex: 0,
        ),
      ],
    ),
    EnigmeLevel(
      level: 4,
      title: 'Classer les animaux',
      emoji: '🔍',
      enigmes: [
        Enigme.qcm(
          question: 'Un VERTÉBRÉ possède forcément…',
          hint: 'Passe ta main dans ton dos… tu la sens ?',
          successNote: 'Oui ! Une colonne vertébrale, avec un squelette '
              'à l\'intérieur du corps.',
          choices: [
            'Une colonne vertébrale',
            'Des plumes',
            'Quatre pattes'
          ],
          correctIndex: 0,
        ),
        Enigme.tri(
          question: 'Trie : vertébré ou invertébré ?',
          hint: 'Squelette avec colonne vertébrale → vertébré. '
              'Corps mou ou carapace → invertébré !',
          successNote: 'Bravo ! Poissons, amphibiens, reptiles, oiseaux et '
              'mammifères sont les 5 groupes de vertébrés.',
          catA: '🦴 Vertébré',
          catB: '🐌 Invertébré',
          items: [
            SortItem('🐱', 'Chat', true),
            SortItem('🐌', 'Escargot', false),
            SortItem('🐟', 'Truite', true),
            SortItem('🦋', 'Papillon', false),
            SortItem('🐸', 'Grenouille', true),
            SortItem('🪱', 'Ver de terre', false),
          ],
        ),
      ],
    ),
    EnigmeLevel(
      level: 5,
      title: 'Maître de la nature',
      emoji: '🏆',
      enigmes: [
        Enigme.qcm(
          question: 'Pour fabriquer sa matière, une plante a besoin de…',
          hint: 'Trois ingrédients : un qui brille, un qui coule, '
              'un qu\'on souffle.',
          successNote: 'Excellent ! Lumière + eau + dioxyde de carbone : '
              'c\'est la photosynthèse.',
          choices: [
            'Lumière, eau et CO₂',
            'Viande et croquettes',
            'Seulement de la terre'
          ],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Pour respirer, tous les êtres vivants prennent…',
          hint: 'C\'est un gaz de l\'air, celui qui nous fait vivre.',
          successNote: 'Oui ! Le dioxygène (O₂). Animaux ET plantes '
              'respirent, jour et nuit.',
          choices: ['Du dioxygène (O₂)', 'De l\'azote', 'De la fumée'],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'La nuit, sans lumière, les plantes…',
          hint: 'Plus de photosynthèse la nuit… mais elles restent vivantes !',
          successNote: 'Bien vu, question de champion ! La nuit, les plantes '
              'respirent comme nous, mais ne font plus de photosynthèse.',
          choices: [
            'Respirent comme nous',
            'Font la photosynthèse',
            'S\'arrêtent de vivre'
          ],
          correctIndex: 0,
        ),
        Enigme.qcm(
          question: 'Si tous les lapins disparaissent, les renards…',
          hint: 'Le renard mange le lapin… plus de lapins = ?',
          successNote: 'Exact ! Toute la chaîne est touchée : dans la nature, '
              'tout le monde dépend de tout le monde.',
          choices: [
            'Ne changent rien',
            'Se mettent à manger l\'herbe',
            'Auront moins à manger'
          ],
          correctIndex: 2,
        ),
      ],
    ),
  ],
};

EnigmeLevel enigmeLevelFor(String zoneId, int level) =>
    enigmesByZone[zoneId]!.firstWhere((l) => l.level == level);
