# Installer Windows (setup.exe)

Produit un installer classique `ElementDungeon-Setup-1.0.exe` que les
joueurs lancent en double-clic pour installer le jeu dans
`Program Files`, avec raccourci Démarrer + désinstaller propre.

## Pré-requis (one-time)

1. **MSYS2 MinGW 64-bit** avec `gcc` + `SDL2` installés (cf `Readme.txt` du dépôt).
2. **Inno Setup 6** : <https://jrsoftware.org/isdl.php> — installeur officiel, gratuit.

## Build local

Depuis un cmd Windows (ou MSYS2 MinGW 64-bit) à la racine du dépôt :

```cmd
installer\build_installer.bat
```

Le script :
1. compile le jeu (`make`)
2. copie `SDL2.dll` à la racine
3. lance `ISCC.exe` sur `installer\element_dungeon.iss`

Résultat : `installer\out\ElementDungeon-Setup-1.0.exe`.

## Build via GitHub Actions (release automatique)

Pushe un tag `v*.*` :

```bash
git tag v1.0
git push origin v1.0
```

Le workflow `.github/workflows/release.yml` :
- monte MSYS2 + SDL2
- compile le jeu
- compile l'installer Inno Setup
- crée une GitHub Release avec `setup.exe` en asset

Pas besoin d'une machine Windows locale.

## Que contient le setup.exe ?

- `element_dungeon.exe`
- `SDL2.dll`
- `Readme.txt`, `README.md`
- `mods/*.cfg`
- Raccourci menu Démarrer + désinstaller

Les fichiers de save (`crucible_save.dat`, `crucible_settings.dat`)
sont créés à côté de l'exe au premier lancement et **survivent** à
un upgrade ; ils ne sont pas supprimés à la désinstallation par
défaut (cf bloc `[UninstallDelete]` commenté dans le `.iss`).
