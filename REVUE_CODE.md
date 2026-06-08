# Revue de code — moteur SCPS

> **Périmètre** : ~21 000 lignes de C99 (`scps/`), 58 fichiers source.
> **Branche** : `claude/pensive-shannon-VZ0sO` · **Date** : 2026-06-08
> **Chaîne d'outils** : gcc 13.3.0, valgrind 3.x · SDL absent (viewer non lié, normal).
> **Nature** : vérification uniquement, aucune correction appliquée.

---

## 1. Verdict global

**Codebase de très bonne qualité.** L'architecture est claire et le principe directeur
(« on lit des coordonnées, on n'assigne jamais de modificateur ») est tenu. Toutes les
vérifications objectives sont au vert ; les points relevés relèvent du durcissement, pas
du bug vivant.

| Contrôle | Résultat |
|---|---|
| Build `-Wall -Wextra -std=c99` (cibles headless) | **0 warning** |
| Self-test `core_demo` | **35 / 35 réussis** |
| `gcc -fanalyzer` (tous les modules sim) | **1** signalement latent (cf. §3.1) |
| `-Wshadow` strict | 4 (cosmétiques, bénins) |
| valgrind `core_demo` | **propre** |
| valgrind `chronicle` (chaîne sim complète) | **propre** — 0 accès invalide, 0 fuite définitive |
| `strcpy` / `strcat` / `sprintf` / `gets` | **aucun** (uniquement `snprintf`) |
| `rand()` | **aucun** — moteur déterministe (PRNG semé) |

---

## 2. Vérification : appels fantômes & fonctions manquantes

Objectif : aucun appel vers une fonction inexistante, aucun prototype déclaré sans corps.

### 2.1 Méthode

- **Vérité « éditeur de liens »** : compilation des 57 `.c` non-SDL en `.o`, puis
  croisement via `nm` des symboles **non définis** (`U`) contre les symboles **globaux**
  définis. Un `static` d'un autre fichier ne résout pas un `U` externe → un tel appel
  serait détecté comme fantôme.
- **`viewer.c`** (non compilable ici, SDL absent) : analyse de ses identifiants appelés
  **après suppression des commentaires et des littéraux de chaîne**, croisée avec les
  objets `SCPS_OBJS`, SDL/TTF/GL, la libc et ses propres helpers `static`.
- **Fonctions manquantes** : extraction des prototypes des `.h` (slurp `perl -0777`,
  prototypes multi-lignes inclus) puis soustraction des symboles globaux réellement
  définis.

### 2.2 Résultats

| Vérification | Mesure | Verdict |
|---|---|---|
| Symboles globaux définis (`nm`) | 359 | — |
| Symboles non définis (`U`) | 325 | seul `toupper` (libc) non couvert |
| Prototypes `.h` analysés | 281 | tous implémentés |
| **Appels fantômes** | — | **🟢 0** |
| **Fonctions manquantes** | — | **🟢 0** |
| Appels cross-fichier vers un `static` | — | **🟢 0** |

### 2.3 Candidats surfacés — tous faux positifs

| Candidat | Verdict | Définition réelle |
|---|---|---|
| `ADJ` | macro | `#define ADJ(field)` local à `viewer.c:967` |
| `scps_cellc` | défini | `static inline` dans `scps_types.h:281` (non global → invisible en `nm`) |
| `fflush` | libc | `<stdio.h>` |
| `stb__perlin_fade` | défini | `static` dans `stb_perlin.h:83` (lib mono-header vendorisée) |
| `stb__perlin_fastfloor` | défini | `static` dans `stb_perlin.h:98` |
| `stb__perlin_grad` | défini | `static` dans `stb_perlin.h:87` |

### 2.4 Garanties supplémentaires

- **Signatures cohérentes** : chaque `.c` inclut son propre `.h` et compile sans warning
  sous `-Wall -Wextra` → un écart prototype/définition aurait produit une erreur
  « conflicting types ». Aucun.
- **Aucune déclaration morte** : pas de prototype `.h` déclaré, jamais appelé *et* jamais
  défini.

**Conclusion : le graphe d'appels est complet et cohérent.**

---

## 3. Points à corriger (par sévérité)

> Aucun n'est bloquant ni un bug vivant. Listés du plus au moins important.

### 3.1 ⚠️ Latent — déréférencement NULL non gardé dans `province_composition`

`scps_demography.c:71` — `group_culture_effective()` fait `PopCulture c = g->origin;`
**sans tester `g`**. Or `province_dominant()` (`:90`) renvoie `NULL` pour une province
vide, et `province_composition` (`:315-316`) enchaîne directement :

```c
const PopGroup *dom = province_dominant(pp);            // NULL si n_groups==0
PopCulture domc = group_culture_effective(dom, drift);  // → deref NULL
```

**Pas atteignable aujourd'hui** : les deux appelants réels gardent en amont
(`viewer.c:586` teste `pop.n_groups>0`, la démo passe une ville peuplée). Mais la fonction
est exportée avec un contrat « non-NULL » implicite et non documenté, alors que le reste
du module est rigoureusement défensif (le tick fait `continue` sur `n_groups<=0`, `:394`
teste `if(dom)`). Durcissement d'une ligne : `if (pp->n_groups<=0) return 0;` en tête de
`province_composition`, ou rendre `group_culture_effective` NULL-safe.

### 3.2 🔸 Mineur — `chronicle.c` : un `malloc` exclu du contrôle OOM

`chronicle.c:249` alloue `s.host = malloc(sizeof(WarHost))`, mais la garde OOM `:250-251`
teste tout **sauf `s.host`**. Si cette allocation échoue, `s.host` part dans la sim non
vérifié. Ajouter `|| !s.host` à la condition.

### 3.3 🔸 Mineur — `malloc` non vérifiés dans plusieurs démos

`agency_demo.c:67-73`, `ai_demo.c:100-104`, `diplo_demo.c:38-41`, etc. assignent le retour
de `malloc` sans le tester (contrairement à `chronicle.c`, qui le fait). Harnais de test,
faible priorité, mais l'incohérence avec `chronicle` mérite d'être alignée.

### 3.4 ▪️ Cosmétique — 4 `-Wshadow` dans `scps_world.c`

`:408-409` et `:428-429` : `rx`/`ry` redéclarés dans les corps de boucle masquent des
homonymes externes. **Bénin** (recalculés et consommés à chaque itération). Un renommage
lèverait les warnings.

---

## 4. Points forts

- **Cœur vérifié et isolé** : `scps_core.c` est une transcription directe des équations,
  auto-testée (35 contrôles à 0.01 près), avec garde-fou anti-division par zéro explicite
  (`:75`). La cloison « le viewer n'inclut pas `scps_core.h` » est vérifiée par grep dans
  le Makefile.
- **Codage défensif systématique** : bornage des indices avant indexation
  (`cid`, `region`, `prov` testés contre `n_*`/`SCPS_MAX_*`), replis propres sur valeurs
  par défaut.
- **`scps_factions.c`** (travail récent) exemplaire : table d'opposition `O[6][6]`
  **parfaitement symétrique** (vérifiée), `group_ethos_lean` gère le cas `!c`,
  normalisations protégées contre `s==0`.
- **Déterminisme** : aucun `rand()`, PRNG maison semé → simulations reproductibles.
- **Hygiène de build** : génération auto des dépendances (`-MMD -MP`), détection SDL
  silencieuse, headless sans dépendance externe (`stb_perlin` vendorisé).

---

## 5. Synthèse

| Axe | État |
|---|---|
| Sécurité mémoire (valgrind + `-fanalyzer`) | 🟢 propre (1 lacune défensive latente, non atteignable) |
| Intégrité du graphe d'appels | 🟢 0 appel fantôme, 0 fonction manquante |
| Hygiène de compilation | 🟢 0 warning `-Wall -Wextra` |
| Tests | 🟢 35/35 |
| Robustesse (durcissements §3.2–3.4) | 🟡 polish optionnel |

Rien de bloquant. Le seul point touchant le code de production est §3.1, et seulement en
cas d'évolution des appelants.
