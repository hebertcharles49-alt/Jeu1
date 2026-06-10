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

> À venir (leurs étapes) : `miniaudio.h` (la prise audio, §5), `nuklear.h`
> (l'overlay de dev sous `-DSCPS_DEV`, §6) — vendorés au moment où ils sont câblés.

## Termes (résumé)

- **miniz** — MIT. Copyright Rich Geldreich et al. — voir l'en-tête de `miniz.h`.
- **stb_image_write** — double licence MIT / domaine public (Unlicense), au
  choix de l'utilisateur — voir le pied de `stb_image_write.h`.

Tous compatibles avec un dépôt sans restriction de redistribution.
