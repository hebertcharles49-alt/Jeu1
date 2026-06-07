# SCPS — Moteur de grande stratégie

Moteur de simulation de civilisations en C99 (**S**phères **C**ulturelles et
**P**erméabilité **S**ystémique), headless + visualiseur SDL2/OpenGL. Le monde
est généré **par** le modèle : la culture, la prospérité, la stabilité et la
crise de fin sont calculées par les équations SCPS, pas par des règles ad hoc.

Principe fondateur : **on lit des coordonnées, on n'assigne jamais de
modificateur.** Une culture *est* une fiche (vecteur d'axes) ; les distances,
la prospérité et l'ordre se **lisent** entre fiches.

## Build & test

```sh
make            # construit core_demo (banc d'essai du moteur §2, SANS SDL)
./core_demo     # 35 contrôles vérifiés à 0.01 près ; sortie ≠ 0 si échec

make scps_dump && ./scps_dump 42        # génération de monde → images PPM (headless)
make prosperity_demo && ./prosperity_demo 42 20
make scps && ./scps_viewer              # visualiseur interactif (requiert SDL2/OpenGL)
```

`stb_perlin.h` est vendorisé : le moteur est entièrement autonome (aucune
dépendance hors SDL2, et SDL2 n'est requis que pour le visualiseur).

## Modules

| Module | Rôle |
|---|---|
| `scps_core` | **Cœur vérifié (§2 + annexe)** : distance culturelle, prospérité de contact, ordre interne et modes d'effondrement |
| `scps_world` | Génération de monde causale : géologie → climat → biomes → provinces → régions → pays |
| `scps_culture` | Pools culturels & traits (un trait = une coordonnée d'axe) |
| `scps_econ` / `scps_trade` | Économie régionale (classes sociales, production, marché), commerce inter-régional |
| `scps_tech` | Arbre de technologies sur l'axe résilience ↔ faustien |
| `scps_prosperity` | Générateur de prospérité PE/SI |
| `scps_render` / `viewer` | Rendu de carte (PPM headless ou SDL2/OpenGL) |

Le document de conception fixe les principes et les équations vérifiées ; le
cœur `scps_core` en est la colonne vertébrale testable.
