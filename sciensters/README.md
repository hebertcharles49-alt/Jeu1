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

| Zone | Matière | Défi | Scienster |
|---|---|---|---|
| ⚡ Vallée Électrique | Physique | Fermer un circuit électrique | Volty → Voltar → Voltrym |
| 🧪 Marais des Potions | Chimie | Les états de l'eau (glace/eau/vapeur) | Gouttix → Vaporix → Nuagix |
| 🌿 Forêt Vivante | SVT | Reconstruire une chaîne alimentaire | Renardeau → Renardor → Renargent |

Chaque victoire donne une étoile ⭐ et fait évoluer la créature (3 stades).

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
├── core/app_state.dart        # Progression + réglages (sauvegarde locale)
├── creatures/creatures.dart   # Zones, Sciensters, sprites pixel art
├── ui/                        # Carte-monde, zones, Scienxdex, réglages
└── games/
    ├── game_framework.dart    # Base commune des mini-jeux (consigne,
    │                          #   indices, victoire, zéro pression)
    ├── circuit_game.dart      # Physique
    ├── etats_eau_game.dart    # Chimie
    └── chaine_alimentaire_game.dart  # SVT
```

Pour ajouter un mini-jeu : créer un écran dans `games/` avec `GameScaffold`,
déclarer son `gameId` dans `AppState.gameIds`, sa zone et sa créature dans
`creatures.dart`.
