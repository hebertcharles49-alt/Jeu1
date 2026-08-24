# Loto français — analyse statistique & pronostics

Outil d'analyse des tirages du Loto FDJ (format « nouveau Loto » depuis
octobre 2008 : 5 boules parmi 49 + 1 numéro Chance parmi 10).

## Contenu

| Fichier | Rôle |
|---|---|
| `data/loto_tirages.csv` | Historique consolidé : **2711 tirages, du 06/10/2008 au 31/01/2026**, sans trou (boules triées `b1-b5` **et** ordre de sortie machine `d1-d5`) |
| `predict_loto.py` | Analyse + génération de grilles (aucune dépendance, Python ≥ 3.8) |
| `test_biais.py` | Batterie de 15 tests de détection de biais (Monte-Carlo exact, Holm, facteur de Bayes, analyse de puissance) |
| `update_data.py` | Met à jour le CSV depuis le fichier officiel FDJ (accès internet requis) |

## Usage

```bash
python3 predict_loto.py                 # analyse + 1 grille par stratégie
python3 predict_loto.py --seed 42       # reproductible
python3 predict_loto.py --grids 3       # 3 grilles par stratégie
python3 predict_loto.py --backtest      # évaluation walk-forward sur 500 tirages
python3 predict_loto.py --draws 1000    # se limiter aux 1000 derniers tirages
```

## Ce que fait l'algorithme

1. **Statistiques descriptives** : fréquences par boule et par numéro Chance,
   fréquences récentes (fenêtre glissante), retards (tirages depuis la
   dernière sortie), paires les plus fréquentes.
2. **Tests d'uniformité (chi²)** : vérifie que la distribution observée est
   compatible avec un tirage équiprobable (implémentation maison de la
   fonction gamma incomplète, p-values exactes).
3. **Cinq stratégies de grilles** :
   - *Chaude* : boules au meilleur momentum sur les 100 derniers tirages ;
   - *Retards maximaux* : boules absentes depuis le plus longtemps ;
   - *Composite* : mélange pondéré de z-scores (fréquence long terme 40 %,
     momentum 35 %, retard 25 %) ;
   - *Bayésienne* : échantillonnage a posteriori Dirichlet-multinomial ;
   - *Anti-foule* : numéros peu joués par le public (> 31, pas de dates, pas
     de suites) — même probabilité de gain, mais jackpot moins partagé en cas
     de victoire : seul levier mathématiquement réel.
4. **Backtest honnête** : chaque stratégie est rejouée en walk-forward sur les
   500 derniers tirages, contre une référence purement aléatoire.

## Résultat du backtest (à retenir)

| Stratégie | Boules trouvées / grille |
|---|---|
| Espérance théorique du hasard | 0,510 |
| Chaude | 0,488 |
| Retards maximaux | 0,522 |
| Composite | 0,510 |
| Bayésienne | 0,512 |
| Anti-foule | 0,486 |
| Aléatoire (référence) | 0,510 |

Aucune stratégie ne bat le hasard — et le chi² sur 2711 tirages donne
p ≈ 0,95 : les boules sortent de façon parfaitement uniforme. **Le Loto n'est
pas prédictible** ; chaque grille a exactement 1 chance sur 19 068 840 au
rang 1. L'outil sert à explorer les statistiques et à générer des grilles
« informées » (au sens des stratégies ci-dessus), pas à promettre un gain.

## Y a-t-il un biais ? (`test_biais.py`)

Batterie de 15 tests sur les 2711 tirages, avec trois précautions rarement
prises : (1) le chi² naïf sur un tirage **sans remise** est déflaté d'un
facteur (K−m)/(K−1) = 44/48 — corrigé ici ; (2) la distribution nulle de
chaque statistique est obtenue par **Monte-Carlo du processus exact**
(3000 historiques simulés) ; (3) **correction de Holm** des comparaisons
multiples.

| Test | p-value |
|---|---|
| T1 Uniformité des 49 boules (MC) | 0,898 |
| T2 Uniformité du n° Chance | 0,553 |
| T3 Stabilité temporelle (5 ères, MC) | 0,547 |
| T4 Stabilité temporelle Chance | 0,363 |
| T5 Effet du jour de tirage (MC) | 0,593 |
| T6 Biais de position de sortie machine (×5) | 0,52 – 0,93 |
| T7 Répétitions entre tirages consécutifs (MC) | 0,096 |
| T8 Parité (MC) | 0,428 |
| T9 Numéros consécutifs (MC) | 0,413 |
| T10 Boule la plus / moins sortie (MC) | 0,414 / 0,925 |

**0 / 15 significatif après Holm.** Le facteur de Bayes est écrasant : les
données sont au moins 10² fois (et jusqu'à 10⁵³ fois selon le prior) plus
probables sous le modèle uniforme que sous un modèle biaisé. L'analyse de
puissance montre qu'un biais relatif < 16 % par boule serait de toute façon
indétectable avec 2711 tirages — et qu'un biais même de +15 % sur 5 boules
laisserait l'espérance de gain lourdement négative (1/9,5 M au rang 1 pour
~50 % des mises redistribuées).

## Sources des données

Historique reconstitué à partir des fichiers officiels FDJ redistribués par
des dépôts publics :
[Ealenn/fdj-forecast](https://github.com/Ealenn/fdj-forecast) (2008→2019),
[SDINAHET/LOTO_API_v5](https://github.com/SDINAHET/LOTO_API_v5) (2019→01/2026).
Fichiers croisés et vérifiés (dates continues, 5 boules distinctes 1-49,
chance 1-10, recoupement des périodes de chevauchement sans conflit).
Pour ajouter les tirages postérieurs au 31/01/2026 : `python3 update_data.py`.
