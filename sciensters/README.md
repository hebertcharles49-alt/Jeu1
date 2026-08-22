# Sciensters 🔬✨

Un jeu éducatif Android (physique, chimie, SVT) pour enfants d'environ 10 ans,
pensé pour un enfant autiste avec TDAH :

- Sessions très courtes (2-3 minutes par défi), récompense immédiate.
- **Jamais de chrono, jamais de game over** : une erreur donne un indice, on réessaie.
- Interface prévisible : même structure sur tous les écrans.
- Consignes en une phrase, gros texte, pictogrammes.
- 100 % hors-ligne, sans publicité, sans compte : rien ne sort du téléphone.

## Le jeu

Le Professeur Pixel a fait exploser son laboratoire : ses créatures, les
**Sciensters**, se sont échappées ! Pour les capturer et les faire évoluer
(façon Pokédex), il faut résoudre des défis scientifiques :

| Zone | Matière | Niveau 1 (manipulation) | Scienster |
|---|---|---|---|
| ⚡ Vallée Électrique | Physique | Fermer un circuit électrique | Volty → Voltar → Voltrym |
| 🧪 Marais des Potions | Chimie | Les états de l'eau (glace/eau/vapeur) | Gouttix → Vaporix → Nuagix |
| 🌿 Forêt Vivante | SVT | Reconstruire une chaîne alimentaire | Renardeau → Renardor → Renargent |

Chaque zone compte **5 niveaux à difficulté croissante** (du CM2 à la 5e),
débloqués un par un et rejouables. Niveau 1 = jeu de manipulation, puis :

- **Physique** : puzzles de câblage sur grille façon redstone (budget de
  câbles, rochers, interrupteurs), jusqu'aux **portes logiques ET / OU**
  validées par une vraie table de vérité animée (bouton TESTER).
- **Chimie** : un labo de potions interactif (bécher animé, thermomètre à
  paliers) — ébullition, dissolution, saturation, tour de densité
  miel/eau/huile, évaporation… et le **dentifrice d'éléphant** en finale !
- **SVT** : une simulation d'écosystème au tour par tour (herbe 🌿,
  lapins 🐰, renards 🦊) : invasion de lapins, renards affamés, grande
  sécheresse — on agit chaque saison pour garder l'équilibre.

La créature évolue aux niveaux 1, 3 et 5, et chaque évolution est reliée
à son équivalent réel (cycle de l'eau, croissance, métamorphose, foudre).
En bonus dans chaque zone : le **Quiz du Professeur** (QCM + jeux de tri),
hors progression. Toujours accessible : un indice partout, une erreur =
un encouragement, jamais de chrono.

## Obtenir l'APK

À chaque push, GitHub Actions compile l'APK automatiquement
(workflow `build-apk.yml`) :

1. Va dans l'onglet **Releases** du dépôt → release **« Sciensters — dernier APK »**.
2. Télécharge `app-release.apk` sur le téléphone Android.
3. Ouvre le fichier et autorise l'installation d'applications inconnues.

(L'APK est aussi disponible en artefact dans l'onglet **Actions**.)

## Développement

Projet Flutter classique. Les dossiers de plateforme (`android/`, etc.) ne
sont pas versionnés : ils sont régénérés par `flutter create .` (la CI le
fait automatiquement).

```
lib/
├── main.dart                  # Point d'entrée + thème
├── core/app_state.dart        # Progression par niveaux + réglages (local)
├── creatures/creatures.dart   # Zones, Sciensters, sprites pixel art
├── content/enigmes.dart       # Énigmes des niveaux 2 à 5 (données pures)
├── ui/                        # Carte-monde, zones, Scienxdex, réglages
└── games/
    ├── game_framework.dart    # Base commune (consigne, indices,
    │                          #   victoire, zéro pression)
    ├── enigme_screen.dart     # Moteur générique QCM + tri (niveaux 2-5)
    ├── circuit_game.dart      # Physique — niveau 1
    ├── etats_eau_game.dart    # Chimie — niveau 1
    └── chaine_alimentaire_game.dart  # SVT — niveau 1
```

Pour ajouter des énigmes : compléter simplement `content/enigmes.dart`
(aucun code d'interface à écrire). Pour un nouveau jeu de manipulation :
créer un écran dans `games/` avec `GameScaffold`.
