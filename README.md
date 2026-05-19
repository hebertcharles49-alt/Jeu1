# Element Dungeon

Roguelite élémentaire en C99 + SDL2 + OpenGL 3.3. Vue 3e personne voxel, donjon procédural sur 10 étages, combat à la souris, combos d'éléments greffés sur les armes, ARPG (loot, raretés, fusion 3-en-1) et méta-progression entre les runs.

> Pour installer et lancer (Windows pas-à-pas) : voir [`Readme.txt`](Readme.txt).

## Boucle de jeu

Tu apparais dans le cimetière (HUB walkable) avec 5 bâtiments interactifs : **Donjon** (lance la run), **Temple** (stats permanentes), **Forge** (améliorations d'armes), **Taverne** (recrute un héros), **Liche** (modificateurs de run). Tu descends 10 étages, chaque étage finit par un boss multi-phases. Entre les étages, une boutique de recettes à effet double-tranchant. À la mort, tu gagnes des éclats d'âme à dépenser au cimetière.

## Combat

- **Attaque à la souris** : maintenir clic gauche pour enchaîner. Chaque arme a son geste, son timing et son son propre.
  - **Épée** — slash transversal rapide, motion-blur 6 segments, swing alterné gauche-droite
  - **Hache** — chop overhead, lift puis chute, landing burst de poussière, impact lourd
  - **Arc** — bow visible (2 limbes + corde en V), flèche sur la corde pendant le draw, snap release + corde vibre
  - **Baguette** — la pointe trace une ellipse, trail des 8 dernières positions, sparkles magiques
  - **Bouclier** — bash + ring de lumière, reflète les projectiles
  - **Poings** — jab rapide (arme de départ)
- **Esquive** : ESPACE = dash avec i-frames, anneau jaune au sol pendant l'invuln
- **Knockback dans la direction d'impact** + sparks rouges + dust kick + flash rouge plein écran avec vignette + RGB-split
- **Halo low-HP** : pulse rouge battement de cœur quand PV < 25%, sync avec SFX_HEARTBEAT

## Éléments

7 éléments originels (Feu, Eau, Terre, Foudre, Air, Vide, Fée) + 3 neutres (Acier, Ténèbres, Sacré). Jusqu'à **3 greffés par arme**, ~30 combos nommés uniques (Vapeur, Plasma, Lave, Tempête, Phénix, …). Sensibilités x2 / x0.5 (table 11×11).

Triples combos déclenchent une **boucle de feedback** persistante (aura + buff de stats) et peuvent passer en **overload** sur kill streak.

## Loot

- **5 raretés** : Commun → Magique → Rare → Épique → Légendaire (+100% stats)
- **6 slots d'équipement** : casque / torse / jambes / bottes / ceinture / gants — chacun visible sur le héros, teinté par sa rareté
- **Affixes** procéduraux (0-4 par item) en archétypes (Offensif / Défensif / Mobilité / Vampirique / Frénétique), avec compromis positifs+négatifs
- **Fusion 3-en-1** : 3 items identiques → rareté +1, +20% stats. Touche F dans l'inventaire
- **20 uniques build-defining** : drops scriptés sur les milestones de kills, 3 par run (seed-locked)
- **7 talismans par run**, dispatchés invisiblement sur les kills selon la seed

## Heroes & armes

- **10 héros** débloqués progressivement (1 par boss tué) : Guerrier, Voleur, Mage, Berserker, Paladin, Druide, Assassin, Ranger, Templier, Nécromant
- **6 armes** combinables 2 par 2 → ~16 sous-classes nommées (Sorcelame, Croisé, Archimage, Garde, Boucher, …)
- Seule l'**arme active** attaque (1 / 2 / TAB pour swap, c'est stratégique)

## Méta-progression

- **Éclats d'âme** dépensés au Temple : +PV / +armure / +vitesse / +%dégâts permanents
- **Forge** : améliorations d'arme permanentes (lvl 0..5, +5 dmg / niveau)
- **Codex** : combos, talismans, équipements, armes découvertes — touche K
- **Seed** affichée + partageable

## Options

Touches rebindables, mute / volume SFX (mixer software 24-voix avec pitch jitter), filtrage de sortie, salle debug bac-à-sable, **barres de vie flottantes** (toggle).

## Stack technique

- **C99** strict, ~5000 lignes
- **SDL2** : fenêtre + input + audio callback (vrai mixer, pas de queue)
- **OpenGL 3.3 core** : 3 shaders embarqués (terrain Lambert+fog, cubes tintés, UI ortho batché)
- **Mesh voxel** régénéré par `gen_id` quand le donjon change
- **FBO 640×360** → blit sur backbuffer 1280×720 pour le pixel-art voxel cohérent
- **Aucun asset externe** : sprites, fonts 5×7 bitmap, sons et voxels sont tous générés par code

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/main.c` | Boucle, state machine, hub walkable |
| `src/game.h` | Types, constantes, API publique |
| `src/world.c` `src/dungeon.c` | Donjon procédural, salles, biomes |
| `src/player.c` | Mouvement, damage, knockback, pickup |
| `src/combat.c` | Armes, fire_*, élements, combos |
| `src/enemies.c` | AI par type, 5 boss multi-phases |
| `src/projectiles.c` `src/surfaces.c` | Projectiles, flaques (eau/feu/glace/…) |
| `src/inventory.c` | Items, affixes, fusion |
| `src/uniques.c` | 20 uniques build-defining |
| `src/shop.c` | ~40 recettes data-driven, double-tranchant |
| `src/audio.c` | Mixer software, SFX procéduraux |
| `src/render_world.c` | Pipeline 3D + overlay HP/dmgnum |
| `src/render_menus.c` `src/render_hud.c` | UI 2D |
| `src/gfx.c` `src/gfx.h` | Couche GL3.3 (shaders, batcher, math) |
| `src/options.c` `src/meta.c` | Persistance |

## Build

Linux / macOS :
```
make
./element_dungeon
```

Windows : voir `Readme.txt` (MSYS2 MinGW + `make`).

Build flags : `-O2 -Wall -Wextra -std=c99`. Aucun warning.
