# SCPS — conventions du dépôt

## Build & vérification

- `make` → `core_demo` (35 contrôles auto-vérifiés, sortie ≠ 0 si échec).
- `make <x>_demo` : chaque module a son banc auto-vérifiant — **tout doit rester vert** (exception connue : `ai_demo` porte 1 échec d'équilibrage préexistant « le Bâtisseur bâtit le PLUS de K », dette documentée).
- `make chronicle && ./chronicle <graine> <sims> <années> [empires] [cités] [continents]` : le balayage headless — la télémétrie est la preuve d'équilibre.
- `make asan && ./chronicle_asan …` : ASan+UBSan doivent rester muets.
- `make scps` : le visualiseur (SDL2) — **0 warning** (`-Wall -Wextra`), toujours.

## Disciplines non négociables

- **La membrane** : `viewer.c` n'inclut jamais `scps_core.h` et ne lit aucun flottant SCPS — des MOTS (readout) et des nombres tangibles seulement.
- **On lit des coordonnées, on n'assigne jamais de modificateur** : un effet passe par les entrées du moteur (K, P, H…), jamais par un bonus plat.
- **Binaire unique, zéro asset** : aucune donnée chargée du disque (stb_perlin vendorisé, polices système, langues compilées).

## Langue (brief table de chaînes)

- **Aucune chaîne littérale face-joueur hors des tables.** Tout panneau, journal, preview, bande, tooltip à venir naît directement en `STR_*` :
  - la liste maîtresse vit dans `scps/strings_ids.h` (X-macro, texte FR de référence inline) ;
  - l'anglais est la liste jumelle `scps/strings_en.h` (même ordre — la complétude est vérifiée à la **compilation**, retirer une ligne casse le build) ;
  - appels : `tr(STR_X)`, plages `tr_band(STR_X_0, idx, n)`, paramètres **positionnels** `tr_fmt(buf, n, STR_X, a0, a1)` — `{0}..{9}`, l'ordre des mots n'est pas universel ;
  - pluriels : deux clés (`STR_X_UN` / `STR_X_PLUSIEURS`) là où c'est nécessaire, et seulement là.
- **Clôture** : `chronicle.c`, `econ_scan.c`, `batch.c`, `dump.c`, tout `printf` de télémétrie/journal console et les commentaires restent en **français, définitivement** (l'outillage de l'ingénieur, pas le jeu).
- `make lang-check` : le **cliquet** — échoue si le nombre de littéraux face-joueur dépasse la base (`scps/lang_baseline.txt`). Le reflux est attendu à mesure que la migration avance (abaisser la base à chaque extraction).
- État de migration : lexique readout (bandes, labels, hovers) + shell (menu, pause, slots, tutoriel) **migrés** ; le reste de `viewer.c` (panneaux, chips, zone_add) part de la base 64 et descend.

## Sauvegarde

- Format versionné (`SAVE_VERSION`), sections taguées, ChaCha20 (clé = obfuscation assumée, pas un secret) + empreinte FNV du clair.
- Toute valeur désérialisée qui borne une boucle ou indexe un tableau **se revalide** au chargement (`save_sane`) — refus net.
- Changer la taille d'une struct sérialisée ⇒ bump `SAVE_VERSION` (« ère antérieure »).
