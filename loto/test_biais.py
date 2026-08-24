#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Chasse au biais dans les tirages du Loto français — batterie de tests rigoureuse.

Hypothèse nulle H0 : chaque tirage est un 5-parmi-49 équiprobable sans remise
(+ Chance uniforme sur 1-10), indépendant des tirages précédents.

Subtilité mathématique : au sein d'un tirage les boules sont corrélées
négativement (sans remise). Le chi² de Pearson naïf sur les comptages de
boules n'est donc PAS distribué en chi²(48) : il est déflaté d'un facteur
c = (K-m)/(K-1) = 44/48 ≈ 0,917, ce qui rend le test naïf trop optimiste
en faveur de l'uniformité. Deux parades sont utilisées ici :
  - correction analytique X²/c ~ chi²(K-1) ;
  - Monte-Carlo exact : on simule le processus réel (5 parmi 49 sans remise)
    et on compare la statistique observée à sa vraie distribution nulle.

Tests effectués
  T1  uniformité des fréquences de boules (chi² corrigé + MC exact)
  T2  uniformité du numéro Chance (chi² standard, propre)
  T3  stabilité temporelle des boules (homogénéité sur 5 ères, MC exact)
  T4  stabilité temporelle de la Chance (contingence standard)
  T5  effet du jour de tirage sur les boules (contingence, MC exact)
  T6  biais de POSITION DE SORTIE (ordre physique de la machine), 5 tests exacts
  T7  répétitions entre tirages consécutifs (vs hypergéométrique, MC)
  T8  parité des boules par tirage (vs hypergéométrique exacte)
  T9  paires de numéros consécutifs par tirage (MC)
  T10 statistiques extrêmes : boule la plus/moins sortie (MC du max/min)
  T11 facteur de Bayes : modèle uniforme vs modèle biaisé (Dirichlet)
  T12 puissance : quel biais serait seulement détectable avec n tirages ?

Usage : python3 test_biais.py [--mc N] [--draws N]
"""

import argparse
import csv
import math
import os
from collections import Counter
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data", "loto_tirages.csv")

K, M, KC = 49, 5, 10
C_CORR = (K - M) / (K - 1)          # facteur de déflation du chi² (sans remise)

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False
    import random


# ---------------------------------------------------------------------------
# utilitaires proba (stdlib)
# ---------------------------------------------------------------------------

def _gamma_p(a, x):
    if x <= 0:
        return 0.0
    if x < a + 1:
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
    return 1.0 - math.exp(-x + a * math.log(x) - math.lgamma(a)) * h


def chi2_sf(x, dof):
    return max(0.0, min(1.0, 1.0 - _gamma_p(dof / 2.0, x / 2.0)))


def norm_ppf(q):
    lo, hi = -10.0, 10.0
    for _ in range(200):
        mid = (lo + hi) / 2
        if 0.5 * (1 + math.erf(mid / math.sqrt(2))) < q:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def hypergeom_pmf(k, n_pop, n_success, n_draw):
    return (math.comb(n_success, k) * math.comb(n_pop - n_success, n_draw - k)
            / math.comb(n_pop, n_draw))


def holm(pvals):
    """Correction de Holm-Bonferroni ; retourne les p ajustées (même ordre)."""
    idx = sorted(range(len(pvals)), key=lambda i: pvals[i])
    m = len(pvals)
    adj = [0.0] * m
    running = 0.0
    for rank, i in enumerate(idx):
        running = max(running, min(1.0, (m - rank) * pvals[i]))
        adj[i] = running
    return adj


# ---------------------------------------------------------------------------
# données
# ---------------------------------------------------------------------------

def load(last_n):
    rows = []
    with open(DATA, encoding="utf-8") as f:
        for r in csv.DictReader(f, delimiter=";"):
            date = datetime.strptime(r["date"], "%Y-%m-%d").date()
            balls = [int(r[f"b{i}"]) for i in range(1, 6)]
            order = [int(r[f"d{i}"]) for i in range(1, 6)] if r.get("d1") else balls
            rows.append((date, balls, int(r["chance"]), order, r["jour"].upper()))
    rows.sort(key=lambda x: x[0])
    return rows[-last_n:]


# ---------------------------------------------------------------------------
# statistiques (communes réel / simulation)
# ---------------------------------------------------------------------------

def stats_from_matrix(mat, eras, day_groups):
    """mat: liste de 5-listes (les boules par tirage). Renvoie le vecteur de
    statistiques utilisé à la fois sur les données réelles et en Monte-Carlo."""
    n = len(mat)
    counts = Counter()
    for balls in mat:
        counts.update(balls)
    exp = n * M / K
    x2 = sum((counts.get(b, 0) - exp) ** 2 / exp for b in range(1, K + 1))
    cmax = max(counts.get(b, 0) for b in range(1, K + 1))
    cmin = min(counts.get(b, 0) for b in range(1, K + 1))

    # homogénéité par ère (contingence boule × ère)
    era_counts = [Counter() for _ in range(len(eras))]
    for (start, end), ec in zip(eras, era_counts):
        for i in range(start, end):
            ec.update(mat[i])
    x2_era = 0.0
    for b in range(1, K + 1):
        tot_b = sum(ec.get(b, 0) for ec in era_counts)
        for (start, end), ec in zip(eras, era_counts):
            e = tot_b * (end - start) / n
            if e > 0:
                x2_era += (ec.get(b, 0) - e) ** 2 / e

    # contingence boule × jour de tirage
    day_counts = [Counter() for _ in day_groups]
    for gi, idxs in enumerate(day_groups):
        for i in idxs:
            day_counts[gi].update(mat[i])
    x2_day = 0.0
    for b in range(1, K + 1):
        tot_b = sum(dc.get(b, 0) for dc in day_counts)
        for idxs, dc in zip(day_groups, day_counts):
            e = tot_b * len(idxs) * M / (n * M)
            if e > 0:
                x2_day += (dc.get(b, 0) - e) ** 2 / e

    # répétitions consécutives (0,1,2+)
    rep = Counter()
    prev = None
    for balls in mat:
        s = set(balls)
        if prev is not None:
            rep[min(len(s & prev), 2)] += 1
        prev = s
    p_rep = [hypergeom_pmf(k, K, M, M) for k in range(6)]
    exp_rep = [(n - 1) * p_rep[0], (n - 1) * p_rep[1], (n - 1) * sum(p_rep[2:])]
    x2_rep = sum((rep.get(k, 0) - exp_rep[k]) ** 2 / exp_rep[k] for k in range(3))

    # parité (nb de boules paires : 24 paires dans 1..49)
    par = Counter(sum(1 for b in balls if b % 2 == 0) for balls in mat)
    exp_par = [n * hypergeom_pmf(k, K, 24, M) for k in range(6)]
    x2_par = sum((par.get(k, 0) - exp_par[k]) ** 2 / exp_par[k] for k in range(6))

    # numéros consécutifs (k, k+1) par tirage — total sur l'historique
    adj = sum(sum(1 for a, b in zip(balls_s, balls_s[1:]) if b == a + 1)
              for balls_s in (sorted(x) for x in mat))

    return {"x2": x2, "max": cmax, "min": cmin, "x2_era": x2_era,
            "x2_day": x2_day, "x2_rep": x2_rep, "x2_par": x2_par, "adj": adj}


def simulate_null(n, eras, day_groups, iters, seed=12345):
    """Distribution nulle exacte des statistiques par simulation du vrai
    processus (5 parmi 49 sans remise, indépendant entre tirages)."""
    sims = []
    if HAVE_NUMPY:
        rng = np.random.default_rng(seed)
        for _ in range(iters):
            u = rng.random((n, K))
            idx = np.argpartition(u, M, axis=1)[:, :M] + 1
            sims.append(stats_from_matrix(idx.tolist(), eras, day_groups))
    else:
        rng = random.Random(seed)
        pop = list(range(1, K + 1))
        for _ in range(iters):
            mat = [rng.sample(pop, M) for _ in range(n)]
            sims.append(stats_from_matrix(mat, eras, day_groups))
    return sims


def mc_pvalue(observed, sims, key, two_sided=False, low=False):
    vals = [s[key] for s in sims]
    ge = sum(1 for v in vals if v >= observed)
    le = sum(1 for v in vals if v <= observed)
    n = len(vals)
    if two_sided:
        return min(1.0, 2 * min(ge + 1, le + 1) / (n + 1))
    return (le + 1) / (n + 1) if low else (ge + 1) / (n + 1)


# ---------------------------------------------------------------------------
# programme principal
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mc", type=int, default=3000,
                    help="itérations Monte-Carlo (défaut 3000)")
    ap.add_argument("--draws", type=int, default=3000)
    args = ap.parse_args()

    rows = load(args.draws)
    n = len(rows)
    mat = [r[1] for r in rows]
    chances = [r[2] for r in rows]
    orders = [r[3] for r in rows]

    # découpages fixes
    n_eras = 5
    bounds = [round(i * n / n_eras) for i in range(n_eras + 1)]
    eras = [(bounds[i], bounds[i + 1]) for i in range(n_eras)]
    era_dates = [(rows[a][0], rows[b - 1][0]) for a, b in eras]
    days = {}
    for i, r in enumerate(rows):
        days.setdefault(r[4], []).append(i)
    day_names = [d for d in sorted(days) if len(days[d]) >= 100]
    day_groups = [days[d] for d in day_names]

    print("=" * 74)
    print("  CHASSE AU BIAIS — LOTO FRANÇAIS")
    print("=" * 74)
    print(f"Tirages : {n} ({rows[0][0]} -> {rows[-1][0]})")
    print(f"Monte-Carlo : {args.mc} historiques simulés du processus exact "
          f"({'numpy' if HAVE_NUMPY else 'stdlib'})")
    print(f"Jours de tirage retenus : "
          + ", ".join(f"{d} ({len(days[d])})" for d in day_names))
    print()

    obs = stats_from_matrix(mat, eras, day_groups)
    sims = simulate_null(n, eras, day_groups, args.mc)

    tests = []   # (nom, statistique, p-value, méthode)

    # T1 — uniformité des boules
    p_naif = chi2_sf(obs["x2"], K - 1)
    p_corr = chi2_sf(obs["x2"] / C_CORR, K - 1)
    p_mc = mc_pvalue(obs["x2"], sims, "x2")
    print(f"T1  Uniformité boules        X²={obs['x2']:.1f}  "
          f"p_naïf={p_naif:.3f}  p_corrigé={p_corr:.3f}  p_MC={p_mc:.3f}")
    tests.append(("T1 uniformité boules", obs["x2"], p_mc, "MC"))

    # T2 — uniformité chance (multinomiale propre)
    cf = Counter(chances)
    e = n / KC
    x2c = sum((cf.get(c, 0) - e) ** 2 / e for c in range(1, KC + 1))
    p2 = chi2_sf(x2c, KC - 1)
    print(f"T2  Uniformité Chance        X²={x2c:.1f}  p={p2:.3f}")
    tests.append(("T2 uniformité Chance", x2c, p2, "chi²(9)"))

    # T3 — stabilité temporelle boules
    p3 = mc_pvalue(obs["x2_era"], sims, "x2_era")
    print(f"T3  Stabilité temporelle     X²={obs['x2_era']:.1f}  p_MC={p3:.3f}")
    print("      ères : " + " | ".join(f"{d0}..{d1}" for d0, d1 in era_dates))
    tests.append(("T3 stabilité boules (5 ères)", obs["x2_era"], p3, "MC"))

    # T4 — stabilité chance (contingence 10 × 5, standard)
    x2c_era = 0.0
    for c in range(1, KC + 1):
        tot_c = cf.get(c, 0)
        for a, b in eras:
            e = tot_c * (b - a) / n
            o = sum(1 for i in range(a, b) if chances[i] == c)
            if e > 0:
                x2c_era += (o - e) ** 2 / e
    p4 = chi2_sf(x2c_era, (KC - 1) * (n_eras - 1))
    print(f"T4  Stabilité Chance         X²={x2c_era:.1f}  p={p4:.3f}")
    tests.append(("T4 stabilité Chance", x2c_era, p4, f"chi²({(KC-1)*(n_eras-1)})"))

    # T5 — effet jour de tirage
    p5 = mc_pvalue(obs["x2_day"], sims, "x2_day")
    print(f"T5  Effet jour de tirage     X²={obs['x2_day']:.1f}  p_MC={p5:.3f}")
    tests.append(("T5 effet jour de tirage", obs["x2_day"], p5, "MC"))

    # T6 — biais de position de sortie (marginale de chaque position = uniforme)
    has_order = any(o != sorted(o) for o in orders)
    if has_order:
        print("T6  Position de sortie (ordre machine) :")
        for pos in range(5):
            pc = Counter(o[pos] for o in orders)
            e = n / K
            x2p = sum((pc.get(b, 0) - e) ** 2 / e for b in range(1, K + 1))
            pp = chi2_sf(x2p, K - 1)
            print(f"      position {pos+1}            X²={x2p:.1f}  p={pp:.3f}")
            tests.append((f"T6 position {pos+1}", x2p, pp, "chi²(48)"))
    else:
        print("T6  Position de sortie : ordre de sortie non disponible — ignoré")

    # T7 — répétitions entre tirages consécutifs
    p7 = mc_pvalue(obs["x2_rep"], sims, "x2_rep")
    print(f"T7  Répétitions consécutives X²={obs['x2_rep']:.1f}  p_MC={p7:.3f}")
    tests.append(("T7 répétitions consécutives", obs["x2_rep"], p7, "MC"))

    # T8 — parité
    p8 = mc_pvalue(obs["x2_par"], sims, "x2_par")
    print(f"T8  Parité des boules        X²={obs['x2_par']:.1f}  p_MC={p8:.3f}")
    tests.append(("T8 parité", obs["x2_par"], p8, "MC"))

    # T9 — numéros consécutifs
    p9 = mc_pvalue(obs["adj"], sims, "adj", two_sided=True)
    mean_adj = sum(s["adj"] for s in sims) / len(sims)
    print(f"T9  Paires consécutives      obs={obs['adj']}  attendu≈{mean_adj:.0f}  "
          f"p_MC={p9:.3f}")
    tests.append(("T9 numéros consécutifs", obs["adj"], p9, "MC bilatéral"))

    # T10 — extrêmes
    p_max = mc_pvalue(obs["max"], sims, "max")
    p_min = mc_pvalue(obs["min"], sims, "min", low=True)
    exp_cnt = n * M / K
    print(f"T10 Boule la plus sortie     {obs['max']} sorties (attendu {exp_cnt:.0f}) "
          f" p_MC={p_max:.3f}")
    print(f"    Boule la moins sortie    {obs['min']} sorties  p_MC={p_min:.3f}")
    tests.append(("T10 max fréquence", obs["max"], p_max, "MC"))
    tests.append(("T10 min fréquence", obs["min"], p_min, "MC"))

    # ------------------------------------------------------------------
    # Correction de Holm sur toute la famille
    # ------------------------------------------------------------------
    print()
    print("-" * 74)
    print("  CORRECTION DES COMPARAISONS MULTIPLES (Holm)")
    print("-" * 74)
    adj_p = holm([t[2] for t in tests])
    worst = sorted(zip(tests, adj_p), key=lambda x: x[1])
    for (name, stat, p, meth), pa in worst:
        flag = "  <-- ATTENTION" if pa < 0.05 else ""
        print(f"  {name:32s} p={p:.3f}  p_Holm={pa:.3f}  [{meth}]{flag}")
    n_sig = sum(1 for pa in adj_p if pa < 0.05)
    print(f"\n  Tests significatifs après correction : {n_sig} / {len(tests)}")

    # ------------------------------------------------------------------
    # T11 — facteur de Bayes
    # ------------------------------------------------------------------
    print()
    print("-" * 74)
    print("  T11  FACTEUR DE BAYES : uniforme (H0) vs biaisé Dirichlet (H1)")
    print("-" * 74)
    counts = Counter()
    for balls in mat:
        counts.update(balls)
    N = n * M
    log_m0 = -N * math.log(K)
    print("  (approximation multinomiale au niveau boule ; α petit = biais fort"
          " autorisé)")
    best_bf = -float("inf")
    for alpha in (1.0, 5.0, 20.0, 100.0, 500.0):
        log_m1 = (math.lgamma(K * alpha) - math.lgamma(N + K * alpha)
                  + sum(math.lgamma(counts.get(b, 0) + alpha) - math.lgamma(alpha)
                        for b in range(1, K + 1)))
        log_bf10 = log_m1 - log_m0
        best_bf = max(best_bf, log_bf10)
        print(f"  alpha={alpha:6.0f} : log10 BF(biais/uniforme) = "
              f"{log_bf10 / math.log(10):8.2f}")
    print(f"\n  => même dans le meilleur cas pour H1, les données sont "
          f"10^{-best_bf/math.log(10):.1f} fois")
    print("     plus probables sous le modèle UNIFORME que sous un modèle biaisé.")

    # ------------------------------------------------------------------
    # T12 — puissance et exploitabilité
    # ------------------------------------------------------------------
    print()
    print("-" * 74)
    print("  T12  PUISSANCE : quel biais pourrait-on seulement détecter ?")
    print("-" * 74)
    p = M / K
    sd = math.sqrt(n * p * (1 - p))
    z_a = norm_ppf(1 - 0.025)            # test unique
    z_ak = norm_ppf(1 - 0.025 / K)       # corrigé pour 49 boules
    z_b = norm_ppf(0.80)
    deltas = []
    for label, zc in (("boule pré-spécifiée", z_a), ("balayage des 49 boules", z_ak)):
        delta = (zc + z_b) * sd / (n * p)
        deltas.append(delta)
        print(f"  Détectable à 80% de puissance ({label}) : "
              f"biais relatif >= {delta:.1%}")
    print(f"""
  Autrement dit : avec {n} tirages, un biais de machine inférieur à
  ~{deltas[0]:.0%}-{deltas[1]:.0%} sur une boule serait INVISIBLE — et les machines FDJ (boules pesées,
  certifiées, changées régulièrement) sont contrôlées bien en-deçà.

  Et même en supposant un biais ÉNORME de +15% sur 5 boules identifiées :
  probabilité rang 1 : 1/19 068 840 x 1.15^5 ~= 1/9 500 000.
  Avec ~50% des mises redistribuées, l'espérance resterait lourdement
  négative. Un biais détectable ne serait toujours pas exploitable.""")

    # ------------------------------------------------------------------
    print("=" * 74)
    if n_sig == 0:
        print("  VERDICT : aucun biais détecté par aucun des tests, après")
        print("  correction des comparaisons multiples et Monte-Carlo exact.")
    else:
        print("  VERDICT : signaux à examiner ci-dessus (p_Holm < 0.05).")
        print("  Vérifier d'abord l'intégrité des données avant de conclure.")
    print("=" * 74)


if __name__ == "__main__":
    main()
