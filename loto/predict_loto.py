#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Analyse statistique et génération de pronostics pour le Loto français (FDJ).

Format du jeu depuis octobre 2008 : 5 boules parmi 1-49 + 1 numéro Chance parmi 1-10.

AVERTISSEMENT MATHÉMATIQUE
--------------------------
Chaque tirage est indépendant et équiprobable : aucune combinaison n'a plus de
chances de sortir qu'une autre, quel que soit l'historique. Ce programme le
démontre d'ailleurs lui-même (test du chi², backtest walk-forward inclus).
Ce qu'un algorithme peut réellement faire :
  1. décrire l'historique (fréquences, écarts, retards, paires) ;
  2. générer des grilles selon des stratégies statistiques classiques
     (chaudes, retards, équilibrée, bayésienne) — sans avantage de probabilité ;
  3. optimiser l'ESPÉRANCE DE GAIN conditionnelle : jouer des combinaisons
     peu jouées par le public (stratégie anti-foule) ne change pas la
     probabilité de gagner, mais réduit le risque de partager le jackpot.
     C'est le seul levier mathématiquement défendable.

Usage :
    python3 predict_loto.py [--draws N] [--grids N] [--seed N] [--backtest]
"""

import argparse
import csv
import math
import os
import random
from collections import Counter
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data", "loto_tirages.csv")

N_BALLS = 49
N_PICK = 5
N_CHANCE = 10
EXP_FREQ = N_PICK / N_BALLS          # probabilité qu'une boule donnée sorte à un tirage
EXP_GAP = N_BALLS / N_PICK           # écart moyen attendu entre deux sorties (9,8 tirages)


# ----------------------------------------------------------------------------
# Chargement des données
# ----------------------------------------------------------------------------

def load_draws(path=DATA, last_n=3000):
    """Retourne la liste chronologique des tirages: (date, [b1..b5 triés], chance)."""
    draws = []
    with open(path, encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter=";")
        for row in reader:
            date = datetime.strptime(row["date"], "%Y-%m-%d").date()
            balls = sorted(int(row[f"b{i}"]) for i in range(1, 6))
            draws.append((date, balls, int(row["chance"])))
    draws.sort(key=lambda d: d[0])
    return draws[-last_n:]


# ----------------------------------------------------------------------------
# Fonctions mathématiques (chi², sans dépendance externe)
# ----------------------------------------------------------------------------

def _gamma_p(a, x):
    """Fonction gamma incomplète régularisée P(a, x) (série / fraction continue)."""
    if x <= 0:
        return 0.0
    if x < a + 1:
        # développement en série
        term = 1.0 / a
        total = term
        n = a
        for _ in range(500):
            n += 1
            term *= x / n
            total += term
            if abs(term) < abs(total) * 1e-14:
                break
        return total * math.exp(-x + a * math.log(x) - math.lgamma(a))
    # fraction continue de Lentz pour Q(a, x)
    tiny = 1e-300
    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / b
    h = d
    for i in range(1, 500):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        d = tiny if abs(d) < tiny else d
        c = b + an / c
        c = tiny if abs(c) < tiny else c
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < 1e-14:
            break
    q = math.exp(-x + a * math.log(x) - math.lgamma(a)) * h
    return 1.0 - q


def chi2_pvalue(chi2, dof):
    """p-value du test du chi² (probabilité d'observer un écart au moins aussi grand)."""
    return max(0.0, min(1.0, 1.0 - _gamma_p(dof / 2.0, chi2 / 2.0)))


# ----------------------------------------------------------------------------
# Statistiques descriptives
# ----------------------------------------------------------------------------

def ball_frequencies(draws):
    c = Counter()
    for _, balls, _ in draws:
        c.update(balls)
    return c


def chance_frequencies(draws):
    return Counter(ch for _, _, ch in draws)


def gaps_since_last(draws):
    """Nombre de tirages écoulés depuis la dernière sortie de chaque boule."""
    last_seen = {}
    for idx, (_, balls, _) in enumerate(draws):
        for b in balls:
            last_seen[b] = idx
    n = len(draws)
    return {b: n - 1 - last_seen.get(b, -1) for b in range(1, N_BALLS + 1)}


def chance_gaps(draws):
    last_seen = {}
    for idx, (_, _, ch) in enumerate(draws):
        last_seen[ch] = idx
    n = len(draws)
    return {c: n - 1 - last_seen.get(c, -1) for c in range(1, N_CHANCE + 1)}


def window_frequencies(draws, window):
    return ball_frequencies(draws[-window:])


def top_pairs(draws, k=10):
    c = Counter()
    for _, balls, _ in draws:
        for i in range(len(balls)):
            for j in range(i + 1, len(balls)):
                c[(balls[i], balls[j])] += 1
    return c.most_common(k)


def uniformity_test(freq, n_draws, categories, picks_per_draw):
    """Chi² d'adéquation à la loi uniforme."""
    expected = n_draws * picks_per_draw / categories
    chi2 = sum((freq.get(i, 0) - expected) ** 2 / expected
               for i in range(1, categories + 1))
    dof = categories - 1
    return chi2, dof, chi2_pvalue(chi2, dof)


# ----------------------------------------------------------------------------
# Scores prédictifs
# ----------------------------------------------------------------------------

def compute_scores(draws):
    """Score composite par boule : fréquence long terme + momentum + retard.

    Chaque composante est exprimée en z-score pour être comparable. Les poids
    sont des choix heuristiques classiques (aucun poids ne crée d'avantage réel).
    """
    n = len(draws)
    freq = ball_frequencies(draws)
    recent = window_frequencies(draws, min(100, n))
    gaps = gaps_since_last(draws)

    # z-score de fréquence long terme (loi binomiale approx. normale)
    p = EXP_FREQ
    sd_long = math.sqrt(n * p * (1 - p))
    sd_recent = math.sqrt(min(100, n) * p * (1 - p))
    exp_long = n * p
    exp_recent = min(100, n) * p

    scores = {}
    for b in range(1, N_BALLS + 1):
        z_freq = (freq.get(b, 0) - exp_long) / sd_long
        z_mom = (recent.get(b, 0) - exp_recent) / sd_recent
        # retard normalisé : loi géométrique de moyenne 9,8 ; écart-type ≈ 9,3
        z_gap = (gaps[b] - EXP_GAP) / math.sqrt((1 - p) / (p * p))
        scores[b] = {
            "freq": freq.get(b, 0),
            "recent": recent.get(b, 0),
            "gap": gaps[b],
            "z_freq": z_freq,
            "z_mom": z_mom,
            "z_gap": z_gap,
            "composite": 0.4 * z_freq + 0.35 * z_mom + 0.25 * z_gap,
        }
    return scores


def chance_scores(draws):
    n = len(draws)
    freq = chance_frequencies(draws)
    gaps = chance_gaps(draws)
    p = 1.0 / N_CHANCE
    sd = math.sqrt(n * p * (1 - p))
    out = {}
    for c in range(1, N_CHANCE + 1):
        z_freq = (freq.get(c, 0) - n * p) / sd
        z_gap = (gaps[c] - N_CHANCE) / math.sqrt((1 - p) / (p * p))
        out[c] = {"freq": freq.get(c, 0), "gap": gaps[c],
                  "composite": 0.6 * z_freq + 0.4 * z_gap}
    return out


# ----------------------------------------------------------------------------
# Stratégies de génération de grilles
# ----------------------------------------------------------------------------

def _pick_top(scores, key, k=N_PICK, reverse=True):
    ranked = sorted(scores, key=lambda b: scores[b][key], reverse=reverse)
    return sorted(ranked[:k])


def _weighted_sample(weights, k, rng):
    """Tirage sans remise proportionnel aux poids (>0)."""
    pool = dict(weights)
    chosen = []
    for _ in range(k):
        total = sum(pool.values())
        r = rng.random() * total
        acc = 0.0
        for b, w in pool.items():
            acc += w
            if acc >= r:
                chosen.append(b)
                del pool[b]
                break
    return sorted(chosen)


def strategy_hot(scores, ch_scores, rng):
    """Boules les plus fréquentes récemment (momentum)."""
    balls = _pick_top(scores, "z_mom")
    chance = max(ch_scores, key=lambda c: ch_scores[c]["composite"])
    return balls, chance


def strategy_overdue(scores, ch_scores, rng):
    """Boules en retard maximal (les « dues » — sophisme du joueur assumé)."""
    balls = _pick_top(scores, "gap")
    chance = max(ch_scores, key=lambda c: ch_scores[c]["gap"])
    return balls, chance


def strategy_composite(scores, ch_scores, rng):
    """Meilleur score composite (fréquence + momentum + retard)."""
    balls = _pick_top(scores, "composite")
    chance = max(ch_scores, key=lambda c: ch_scores[c]["composite"])
    return balls, chance


def strategy_bayesian(scores, ch_scores, rng):
    """Échantillonnage a posteriori Dirichlet-multinomial.

    Prior uniforme Dirichlet(alpha=2), vraisemblance = comptes observés.
    On échantillonne une probabilité par boule puis on tire 5 boules
    proportionnellement — les fréquences guident sans figer.
    """
    alpha0 = 2.0
    gammas = {b: rng.gammavariate(alpha0 + scores[b]["freq"], 1.0)
              for b in scores}
    balls = _weighted_sample(gammas, N_PICK, rng)
    gch = {c: rng.gammavariate(alpha0 + ch_scores[c]["freq"], 1.0)
           for c in ch_scores}
    chance = max(gch, key=gch.get)
    return balls, chance


def strategy_anticrowd(scores, ch_scores, rng):
    """Anti-foule : maximise l'espérance conditionnelle de gain.

    Le public sur-joue les dates (1-31, surtout 1-12), les petites valeurs et
    les suites visuelles. Jouer haut (32-49), éviter les motifs populaires :
    même probabilité de gagner, jackpot moins partagé si gain.
    """
    high = [b for b in scores if b >= 32]
    low = [b for b in scores if 14 <= b <= 31]
    balls = sorted(rng.sample(high, 4) + rng.sample(low, 1))
    # sommes typiques jouées : 100-130 ; viser > 150 renforce l'effet
    chance = rng.choice([c for c in ch_scores if c >= 5])
    return balls, chance


STRATEGIES = [
    ("Chaude (momentum 100 tirages)", strategy_hot),
    ("Retards maximaux", strategy_overdue),
    ("Composite fréq+momentum+retard", strategy_composite),
    ("Bayésienne (Dirichlet)", strategy_bayesian),
    ("Anti-foule (EV conditionnelle)", strategy_anticrowd),
]


# ----------------------------------------------------------------------------
# Backtest honnête
# ----------------------------------------------------------------------------

def backtest(draws, test_len=500, seed=42):
    """Walk-forward : à chaque tirage de test, chaque stratégie propose une
    grille à partir du seul passé, puis on compte les numéros trouvés.
    Référence théorique : E[boules trouvées] = 5·5/49 ≈ 0,510."""
    rng = random.Random(seed)
    start = len(draws) - test_len
    results = {name: {"hits": 0, "chance_hits": 0, "by_count": Counter()}
               for name, _ in STRATEGIES}
    results["Aléatoire (référence)"] = {"hits": 0, "chance_hits": 0,
                                        "by_count": Counter()}

    for t in range(start, len(draws)):
        past = draws[:t]
        actual_balls = set(draws[t][1])
        actual_chance = draws[t][2]
        scores = compute_scores(past)
        ch = chance_scores(past)
        for name, fn in STRATEGIES:
            balls, chance = fn(scores, ch, rng)
            k = len(actual_balls & set(balls))
            results[name]["hits"] += k
            results[name]["by_count"][k] += 1
            results[name]["chance_hits"] += (chance == actual_chance)
        rnd = rng.sample(range(1, N_BALLS + 1), N_PICK)
        k = len(actual_balls & set(rnd))
        results["Aléatoire (référence)"]["hits"] += k
        results["Aléatoire (référence)"]["by_count"][k] += 1
        results["Aléatoire (référence)"]["chance_hits"] += (
            rng.randint(1, N_CHANCE) == actual_chance)

    return results, test_len


# ----------------------------------------------------------------------------
# Rapport
# ----------------------------------------------------------------------------

def bar(value, vmax, width=30):
    filled = int(round(width * value / vmax)) if vmax else 0
    return "█" * filled + "·" * (width - filled)


def main():
    ap = argparse.ArgumentParser(description="Analyse prédictive du Loto français")
    ap.add_argument("--draws", type=int, default=3000,
                    help="nombre de derniers tirages analysés (défaut 3000)")
    ap.add_argument("--grids", type=int, default=1,
                    help="grilles générées par stratégie (défaut 1)")
    ap.add_argument("--seed", type=int, default=None,
                    help="graine aléatoire (reproductibilité)")
    ap.add_argument("--backtest", action="store_true",
                    help="évalue chaque stratégie en walk-forward sur 500 tirages")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    draws = load_draws(last_n=args.draws)
    n = len(draws)

    print("=" * 72)
    print("  LOTO FRANÇAIS — ANALYSE STATISTIQUE & PRONOSTICS")
    print("=" * 72)
    print(f"Tirages analysés : {n} (du {draws[0][0]} au {draws[-1][0]})")
    print(f"Format : 5 boules /49 + numéro Chance /10\n")

    # --- fréquences ---
    freq = ball_frequencies(draws)
    exp = n * EXP_FREQ
    print(f"--- Fréquence des boules (attendu ≈ {exp:.0f} sorties chacune) ---")
    ranked = sorted(range(1, N_BALLS + 1), key=lambda b: -freq.get(b, 0))
    vmax = freq[ranked[0]]
    for b in ranked[:8]:
        print(f"  {b:2d} : {freq[b]:4d}  {bar(freq[b], vmax)}")
    print("   ...")
    for b in ranked[-8:]:
        print(f"  {b:2d} : {freq[b]:4d}  {bar(freq[b], vmax)}")

    chi2, dof, p = uniformity_test(freq, n, N_BALLS, N_PICK)
    print(f"\nTest du chi² d'uniformité (boules)  : chi²={chi2:.1f}, ddl={dof}, p={p:.3f}")
    cfreq = chance_frequencies(draws)
    chi2c, dofc, pc = uniformity_test(cfreq, n, N_CHANCE, 1)
    print(f"Test du chi² d'uniformité (chance)  : chi²={chi2c:.1f}, ddl={dofc}, p={pc:.3f}")
    verdict = "compatible avec un tirage parfaitement aléatoire" if p > 0.01 \
        else "écart significatif détecté (vérifier les données !)"
    print(f"=> {verdict}\n")

    # --- retards ---
    gaps = gaps_since_last(draws)
    print(f"--- Retards actuels (écart moyen attendu {EXP_GAP:.1f} tirages) ---")
    for b in sorted(gaps, key=lambda x: -gaps[x])[:8]:
        print(f"  {b:2d} : absent depuis {gaps[b]:3d} tirages")
    print()

    # --- paires ---
    print("--- Paires les plus fréquentes ---")
    for (a, b), c in top_pairs(draws, 5):
        print(f"  {a:2d}-{b:2d} : {c} fois")
    print()

    # --- pronostics ---
    scores = compute_scores(draws)
    ch = chance_scores(draws)
    print("=" * 72)
    print("  GRILLES PROPOSÉES")
    print("=" * 72)
    for name, fn in STRATEGIES:
        for g in range(args.grids):
            balls, chance = fn(scores, ch, rng)
            nums = " - ".join(f"{b:2d}" for b in balls)
            print(f"  [{name}]")
            print(f"      {nums}   Chance : {chance}\n")

    # --- backtest ---
    if args.backtest:
        print("=" * 72)
        print("  BACKTEST WALK-FORWARD (honnêteté du modèle)")
        print("=" * 72)
        results, tl = backtest(draws)
        print(f"Sur les {tl} derniers tirages, moyenne de boules trouvées par grille")
        print(f"(espérance théorique du hasard pur : {5*5/49:.3f}) :\n")
        for name, r in results.items():
            mean = r["hits"] / tl
            pch = r["chance_hits"] / tl
            print(f"  {name:38s} {mean:.3f} boules/grille, chance {pch:.1%}")
        print("\n=> Aucune stratégie ne bat significativement le hasard : c'est")
        print("   la démonstration empirique que le Loto n'est pas prédictible.")

    print()
    print("-" * 72)
    print("Rappel : chaque combinaison a 1 chance sur 19 068 840 au rang 1,")
    print("quel que soit l'historique. Jouez de manière responsable.")
    print("-" * 72)


if __name__ == "__main__":
    main()
