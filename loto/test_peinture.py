#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test ciblé de l'hypothèse « masse de peinture / gravure ».

L'hypothèse physique fait une prédiction FALSIFIABLE : si l'encre (ou la
gravure) des chiffres modifie la masse et que cette masse modifie la
probabilité de sortie, alors la fréquence de sortie de chaque boule doit
être corrélée à la quantité d'encre qu'elle porte. Ce test dirigé (1 degré
de liberté) est bien plus puissant que le chi² omnibus : il détecterait un
effet commun ~5 fois plus petit.

Proxy de masse d'encre : nombre de segments (affichage 7 segments) des
chiffres de la boule — le « 1 » porte 2 segments, le « 38 » en porte 12.
Le facteur d'échelle réel (mg par segment) est inconnu mais sans importance :
la corrélation est invariante par échelle.

Tests :
  P1  corrélation fréquence <-> encre (permutation, bilatérale)
  P2  boules à 1 chiffre (1-9, moins encrées) vs 2 chiffres (z bilatéral)
  P3  corrélation fréquence <-> valeur du numéro (autre proxy : plus grand
      numéro = souvent plus d'encre)
  P4  même chose sur l'ORDRE DE SORTIE : si les lourdes sortent plus tôt ou
      plus tard, la position moyenne de sortie doit dépendre de l'encre

Usage : python3 test_peinture.py [--perms N]
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

K, M = 49, 5
SEG = {"0": 6, "1": 2, "2": 5, "3": 5, "4": 4, "5": 5, "6": 6, "7": 3,
       "8": 7, "9": 6}


def ink(n):
    return sum(SEG[c] for c in str(n))


def load():
    rows = []
    with open(DATA, encoding="utf-8") as f:
        for r in csv.DictReader(f, delimiter=";"):
            balls = [int(r[f"b{i}"]) for i in range(1, 6)]
            order = [int(r[f"d{i}"]) for i in range(1, 6)] if r.get("d1") else balls
            rows.append((datetime.strptime(r["date"], "%Y-%m-%d").date(),
                         balls, order))
    rows.sort(key=lambda x: x[0])
    return rows


def pearson(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    return sxy / math.sqrt(sxx * syy)


def perm_pvalue(xs, ys, iters, rng):
    """p bilatérale par permutation des étiquettes xs."""
    r_obs = abs(pearson(xs, ys))
    xs = list(xs)
    hits = 0
    for _ in range(iters):
        rng.shuffle(xs)
        if abs(pearson(xs, ys)) >= r_obs:
            hits += 1
    return (hits + 1) / (iters + 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perms", type=int, default=20000)
    args = ap.parse_args()
    rng = random.Random(4242)

    rows = load()
    n = len(rows)
    counts = Counter()
    pos_sum = Counter()   # somme des positions de sortie (1..5) par boule
    for _, balls, order in rows:
        counts.update(balls)
        for pos, b in enumerate(order, start=1):
            pos_sum[b] += pos

    balls = list(range(1, K + 1))
    freqs = [counts.get(b, 0) for b in balls]
    inks = [ink(b) for b in balls]
    N = n * M

    print("=" * 74)
    print("  TEST DE L'HYPOTHÈSE « MASSE DE PEINTURE » — %d tirages" % n)
    print("=" * 74)
    print(f"Proxy d'encre : segments 7-seg — min boule 1 ({ink(1)}), "
          f"max boule 28/38 ({ink(38)})")
    print(f"Si la peinture biaise le tirage, fréquence et encre doivent être "
          f"corrélées.\n")

    # P1 — corrélation fréquence / encre
    r1 = pearson(inks, freqs)
    p1 = perm_pvalue(inks, freqs, args.perms, rng)
    print(f"P1  corr(fréquence, encre)          r={r1:+.3f}   p_perm={p1:.4f}")

    # P2 — groupe 1 chiffre vs 2 chiffres (test dirigé, 1 ddl)
    g1 = sum(counts.get(b, 0) for b in range(1, 10))
    p_g = 9 / K
    exp_g = N * p_g
    # variance hypergéométrique par tirage, sommée sur n tirages
    var_g = n * M * p_g * (1 - p_g) * (K - M) / (K - 1)
    z2 = (g1 - exp_g) / math.sqrt(var_g)
    p2 = math.erfc(abs(z2) / math.sqrt(2))
    print(f"P2  boules 1-9 (moins encrées)      {g1} sorties "
          f"(attendu {exp_g:.0f})   z={z2:+.2f}   p={p2:.3f}")

    # P3 — corrélation fréquence / valeur
    r3 = pearson(balls, freqs)
    p3 = perm_pvalue(balls, freqs, args.perms, rng)
    print(f"P3  corr(fréquence, valeur)         r={r3:+.3f}   p_perm={p3:.4f}")

    # P4 — position moyenne de sortie vs encre
    has_order = any(o != sorted(o) for _, _, o in rows)
    if has_order:
        mean_pos = [pos_sum[b] / counts[b] for b in balls]
        r4 = pearson(inks, mean_pos)
        p4 = perm_pvalue(inks, mean_pos, args.perms, rng)
        print(f"P4  corr(position sortie, encre)    r={r4:+.3f}   p_perm={p4:.4f}")
    else:
        print("P4  ordre de sortie indisponible — ignoré")

    # sensibilité du test dirigé P2
    z80 = 2.80  # z(0.975) + z(0.80)
    delta = z80 * math.sqrt(var_g) / exp_g
    print(f"""
Sensibilité : le test P2 détecterait (80% de puissance) un biais commun de
{delta:.1%} sur le groupe des boules peu encrées. Le chi² omnibus, lui, ne
descend pas sous ~16% par boule : ce test dirigé est ~4x plus sensible.

Lecture : si toutes les p-values ci-dessus sont > 0.05 et les corrélations
proches de 0, l'hypothèse « la peinture crée un biais observable » est
réfutée au niveau de précision atteignable avec {n} tirages réels de la
machine physique réelle — qui intègrent déjà TOUTE la physique (peinture,
gravure, usure, électricité statique, humidité...).""")


if __name__ == "__main__":
    main()
