# Element Dungeon

Roguelite élémentaire en C99 + SDL2 + OpenGL 3.3. 10 éléments combinables sur 6 armes, 5 biomes à 2 étages + Archimage final, ARPG (loot, raretés, fusion), méta-progression.

> **Windows joueur** : `ElementDungeon-Setup-*.exe` sur la [page Releases](https://github.com/hebertcharles49-alt/jeu1/releases), double-clic.
>
> **Développeur** (Windows / Linux / macOS) : voir [`Readme.txt`](Readme.txt).

## Build

```
make
./element_dungeon
```

Flags : `-O2 -Wall -Wextra -std=c99`.

## Installer Windows

`installer/build_installer.bat` (local, Inno Setup 6 requis) ou push d'un tag `v*.*` → CI produit `ElementDungeon-Setup-*.exe`. Détails : [`installer/README.md`](installer/README.md).
