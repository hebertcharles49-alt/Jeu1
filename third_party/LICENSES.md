# third_party — bibliothèques vendorées (single-file)

Doctrine SCPS : **binaire unique, zéro asset, zéro gestionnaire de paquets.**
Chaque dépendance entre ici en fichiers committés (un `.h`, parfois un `.c`),
domaine public ou MIT, compilée par le `Makefile` comme le reste du moteur.
Aucun submodule, aucun lien dynamique nouveau (vérifié à `objdump -p`/`ldd` :
le `chronicle` release n'a que libc/libm ; le viewer, ses dépendances SDL).

| Fichier | Lib | Version | Licence | Emploi dans SCPS |
|---|---|---|---|---|
| `miniz.c` / `miniz.h` | miniz (richgel999) | 3.0.2 | MIT | `mz_crc32` (harnais de déterminisme), `mz_compress`/`mz_uncompress` (blocs de sauvegarde). Compilé `-DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS` (crc32 + deflate seuls). |
| `stb_image_write.h` + `stb_image_write_impl.c` | stb_image_write (Sean Barrett) | 1.16 | domaine public / MIT | PNG : `montage.png` (planche-contact) et captures F12 du viewer. L'`_impl.c` est l'unique unité portant `STB_IMAGE_WRITE_IMPLEMENTATION`. |
| `miniaudio.h` + `miniaudio_impl.c` | miniaudio (David Reid) | 0.11.x | domaine public / MIT-0 | La prise audio (viewer seul) : device de lecture + mixeur de voix procédurales. Compilé `MA_NO_DECODING/ENCODING/GENERATION/RESOURCE_MANAGER/NODE_GRAPH` (lecture device seule) ; dlopen ses backends → aucun lien dynamique NOUVEAU (vérifié objdump : NEEDED inchangé). |

| `nuklear.h` + `nuklear_sdl_renderer.h` | Nuklear (Immediate-Mode-UI) | (master) | domaine public / MIT | L'overlay de dev (`dev_overlay.c`, F3) — compilé UNIQUEMENT sous `-DSCPS_DEV` (cible `make dev`, OBJDIR isolé). JAMAIS dans le release : `nm scps_viewer | grep '^nk_'` = 0. Le backend rend via `SDL_RenderGeometryRaw` (SDL ≥ 2.0.18). |

## Termes (résumé)

- **miniz** — MIT. Copyright Rich Geldreich et al. — voir l'en-tête de `miniz.h`.
- **stb_image_write** — double licence MIT / domaine public (Unlicense), au
  choix de l'utilisateur — voir le pied de `stb_image_write.h`.
- **miniaudio** — double licence : domaine public (Unlicense) OU MIT-0 — voir
  le pied de `miniaudio.h`.
- **Nuklear** — double licence : domaine public (Unlicense) OU MIT — voir le
  pied de `nuklear.h`. Backend SDL_Renderer : même licence (demo Nuklear).

Tous compatibles avec un dépôt sans restriction de redistribution.
