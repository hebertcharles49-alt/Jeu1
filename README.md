# Crucible — Doomlike Hybride (C + SDL2)

Mélange de Doom (donjon vue du dessus), Vampire Survivor (auto-attaque, bullet hell, économie d'XP) et Binding of Isaac (combinaisons d'objets). Pixel art procédural, 100% en C.

## Concept

- **Vue 2D top-down**, donjon généré aléatoirement étage par étage.
- **5 armes** distinctes (chacune un slot) :
  1. **Épée** — mêlée single target / fencer
  2. **Bouclier** — défense / reflect AOE
  3. **Arc** — distance single target
  4. **Bâton** — magie AOE
  5. **Hache** — mêlée AOE / cleave
- **7 éléments** qui se greffent sur l'arme active (max 3) :
  Feu · Eau · Terre · Foudre · Air · Vide · Fée
- **Combinaisons d'éléments** uniques. Quelques exemples :
  - Feu + Eau = **Vapeur** (AOE)
  - Feu + Foudre = **Plasma** (chaîne)
  - Eau + Foudre = **Choc** (paralyse)
  - Feu + Eau + Foudre = **Tempête**
  - Vide + Fée + Foudre = **Déchirure** (perce + chaîne + vol de vie)
  - Feu + Terre + Air = **Volcan**
  - … cf. `combat.c` `compute_combo()` pour le tableau complet (~30 combos nommés + fallback hybride).
- **Roguelike** : chaque mort renvoie au sanctuaire, on dépense des **éclats d'âme** pour débloquer armes et éléments. La progression in-run (level-up) propose 3 choix aléatoires (élément à greffer / +PV / +vitesse / +dégâts).
- **Auto-attaque** : chaque arme tire seule quand son cooldown est prêt. La souris dirige les armes ciblées (arc, bâton). L'arme **active** (TAB / 1-5) reçoit les nouveaux éléments ramassés.

## Contrôles

| Touche             | Action                                    |
|--------------------|-------------------------------------------|
| WASD / flèches     | Déplacement                               |
| Souris             | Visée (arc / bâton)                        |
| Espace             | Dash (i-frames)                           |
| 1 à 5 / TAB / Q    | Arme active (où se greffe le prochain élément) |
| 1 / 2 / 3          | Choix au level-up                         |
| ÉCHAP              | Abandonner la course / quitter            |

## Compiler — Windows

### Option A : MSYS2 + MinGW (le plus simple)

1. Installe **MSYS2** : <https://www.msys2.org>
2. Ouvre `MSYS2 MinGW 64-bit` puis :
   ```
   pacman -Syu
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkg-config make
   ```
3. Dans le dossier du projet :
   ```
   make
   ./crucible
   ```
   ou bien :
   ```
   ./build.bat
   ./crucible.exe
   ```

### Option B : SDL2-devel pour MinGW (sans MSYS2)

1. Installe MinGW-w64 (par ex. `https://www.mingw-w64.org/`).
2. Récupère `SDL2-devel-2.30.x-mingw.zip` sur <https://github.com/libsdl-org/SDL/releases>.
3. Décompresse et règle la variable d'environnement :
   ```
   set SDL2_DIR=C:\chemin\vers\SDL2-2.30.x\x86_64-w64-mingw32
   ```
4. Lance :
   ```
   build.bat
   ```
   `SDL2.dll` est copié à côté de l'exécutable.

### Option C : MSVC / Visual Studio

1. Installe **Build Tools for Visual Studio**.
2. Récupère `SDL2-devel-2.30.x-VC.zip` sur <https://github.com/libsdl-org/SDL/releases>.
3. Ouvre **x64 Native Tools Command Prompt for VS** :
   ```
   set SDL2_DIR=C:\chemin\vers\SDL2-2.30.x
   build_msvc.bat
   ```

## Compiler — Linux / WSL (utile pour développer)

```
sudo apt-get install build-essential libsdl2-dev
make
./crucible
```

## Structure

```
src/
  game.h     types, constantes, API
  main.c     boucle principale, états (titre/hub/run/levelup/dead)
  world.c    génération donjon, mouvement joueur, ennemis, pickups, logique salle
  combat.c   armes, éléments, combos, projectiles, fées
  render.c   sprites procéduraux, font 5x7 bitmap, HUD, menus
  meta.c     sauvegarde permanente (crucible_save.dat)
```

Pas d'asset externe : tout le pixel art (joueur, ennemis, projectiles, tuiles, font) est dessiné par code.

## Conseils gameplay

- Au début seule l'**Épée** est débloquée. Survis quelques étages, accumule des éclats, dépense au sanctuaire pour ouvrir les autres armes et les premiers éléments (le Feu et l'Eau sont les plus économiques en premier).
- Ramasse des élements et change l'arme active **avant** la collecte pour cibler la greffe.
- Le **Bouclier** reflète les projectiles ennemis : utile contre les brutes bullet-hell.
- Les combos triples (3 éléments) sont les plus puissants : vise par exemple **Vide+Fée+Foudre** sur l'arc pour un build perce-chaîne-vol-de-vie.

## Pistes d'extension (la porte est laissée ouverte volontairement)

- Combos de plus de 3 éléments en mode "saturation" temporaire.
- Synergies entre éléments présents sur **différentes** armes (overcharge inter-armes).
- Ennemis avec affinités élémentaires (résistances/faiblesses).
- Boss d'étage à pattern bullet-hell étendu.
- Streaming audio (pour l'instant aucun son, le code initialise déjà `SDL_INIT_AUDIO`).

Bon vibe-coding.
