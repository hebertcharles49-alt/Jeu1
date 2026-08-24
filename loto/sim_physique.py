#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Simulation physique d'un boulier de loterie — effet d'une asymétrie de masse.

Modèle : tambour circulaire 2D en rotation, 49 boules soumises à la gravité,
collisions boule-boule et boule-paroi inélastiques, entraînement tangentiel
par la paroi (paddles), agitation turbulente. Après une phase de brassage,
on extrait une à une les 5 boules les plus proches de la trappe (bas du
tambour). C'est un modèle JOUET : il ne reproduit pas une machine FDJ, mais
il répond à la question dimensionnante — de combien un écart de masse
relatif Δm/m décale-t-il la probabilité de sortie dans un mélange chaotique ?

Protocole « dose-réponse » : l'écart de masse est AMPLIFIÉ (jusqu'à x1000 le
réel) pour rendre l'effet mesurable avec un nombre de tirages simulables,
puis on ajuste la pente biais = f(Δm/m) et on extrapole à la valeur réelle
(~0,03% : ~25 mg d'encre/gravure sur une boule de ~80 g).

La masse de chaque boule est m0 + (encre_normalisée · Δm), l'encre étant le
nombre de segments 7-seg des chiffres — comme dans test_peinture.py.

Usage : python3 sim_physique.py [--draws N] [--configs "0,0.1,0.3"]
"""

import argparse
import math
import os

try:
    import numpy as np
except ImportError:
    raise SystemExit("numpy requis pour la simulation physique : pip install numpy")

K, M = 49, 5
SEG = {"0": 6, "1": 2, "2": 5, "3": 5, "4": 4, "5": 5, "6": 6, "7": 3,
       "8": 7, "9": 6}


def ink(n):
    return sum(SEG[c] for c in str(n))


# ---------------------------------------------------------------------------
# paramètres physiques du tambour
# ---------------------------------------------------------------------------
R_DRUM = 0.35          # rayon du tambour (m)
R_BALL = 0.024         # rayon des boules (m)
M0 = 0.080             # masse nominale (kg)
G = 9.81
OMEGA = 2.0            # rotation du tambour (rad/s)
DT = 2.0e-3
E_WALL = 0.75          # restitution paroi
E_BALL = 0.90          # restitution boule-boule
MU_WALL = 0.55         # entraînement tangentiel par la paroi
SIGMA_AIR = 0.9        # agitation turbulente (m/s^2, écart-type par axe)
T_MIX = 2.0            # brassage initial (s)
T_BETWEEN = 0.4        # brassage entre deux extractions (s)
PORT_HALF = 0.10       # demi-largeur de la trappe (m)


def simulate_draw(masses, rng):
    """Un tirage complet : renvoie les 5 indices de boules extraites."""
    n = K
    # positions initiales aléatoires sans recouvrement grossier
    pos = np.zeros((n, 2))
    placed = 0
    while placed < n:
        p = (rng.random(2) * 2 - 1) * (R_DRUM - R_BALL)
        if p @ p < (R_DRUM - R_BALL) ** 2:
            if placed == 0 or np.min(np.linalg.norm(pos[:placed] - p, axis=1)) > 1.8 * R_BALL:
                pos[placed] = p
                placed += 1
    vel = rng.standard_normal((n, 2)) * 0.5
    active = np.ones(n, dtype=bool)
    drawn = []

    def step(nsteps):
        nonlocal pos, vel
        for _ in range(nsteps):
            idx = np.where(active)[0]
            p = pos[idx]
            v = vel[idx]
            m = masses[idx]
            # gravité + turbulence
            v[:, 1] -= G * DT
            v += rng.standard_normal(v.shape) * SIGMA_AIR * DT
            p += v * DT
            # collision paroi (cercle) + entraînement rotatif
            r = np.linalg.norm(p, axis=1)
            out = r > (R_DRUM - R_BALL)
            if out.any():
                nrm = p[out] / r[out, None]
                p[out] = nrm * (R_DRUM - R_BALL)
                vn = np.sum(v[out] * nrm, axis=1)
                hit = vn > 0
                v[out] -= (1 + E_WALL) * (vn * hit)[:, None] * nrm
                # vitesse tangentielle de la paroi (rotation du tambour)
                tang = np.stack([-nrm[:, 1], nrm[:, 0]], axis=1)
                v_wall = OMEGA * R_DRUM
                vt = np.sum(v[out] * tang, axis=1)
                v[out] += MU_WALL * (v_wall - vt)[:, None] * tang * 0.5
            # collisions boule-boule (paires en recouvrement)
            d = p[:, None, :] - p[None, :, :]
            dist = np.linalg.norm(d, axis=2)
            iu = np.triu_indices(len(idx), k=1)
            overlap = (dist[iu] < 2 * R_BALL) & (dist[iu] > 1e-9)
            if overlap.any():
                ii = iu[0][overlap]
                jj = iu[1][overlap]
                nrm = d[ii, jj] / dist[ii, jj][:, None]
                # séparation positionnelle
                push = (2 * R_BALL - dist[ii, jj])[:, None] * nrm / 2
                np.add.at(p, ii, push)
                np.add.at(p, jj, -push)
                # impulsion élastique (masses différentes !)
                rel = np.sum((v[ii] - v[jj]) * nrm, axis=1)
                closing = rel < 0
                mi, mj = m[ii], m[jj]
                imp = (1 + E_BALL) * rel * closing / (1 / mi + 1 / mj)
                v[ii] -= (imp / mi)[:, None] * nrm
                v[jj] += (imp / mj)[:, None] * nrm
            pos[idx] = p
            vel[idx] = v

    step(int(T_MIX / DT))
    for _ in range(M):
        step(int(T_BETWEEN / DT))
        idx = np.where(active)[0]
        p = pos[idx]
        # boule extraite : la plus basse dans la zone de la trappe ;
        # à défaut la plus basse tout court
        in_port = np.abs(p[:, 0]) < PORT_HALF
        cand = idx[in_port] if in_port.any() else idx
        chosen = cand[np.argmin(pos[cand, 1])]
        drawn.append(int(chosen))
        active[chosen] = False
    return drawn


def run_config(rel_dm, draws, seed):
    """rel_dm : Δm/m max (la boule la plus encrée vs la moins encrée)."""
    rng = np.random.default_rng(seed)
    inks = np.array([ink(b) for b in range(1, K + 1)], dtype=float)
    ink_norm = (inks - inks.min()) / (inks.max() - inks.min())   # 0..1
    masses = M0 * (1.0 + rel_dm * ink_norm)
    counts = np.zeros(K)
    for _ in range(draws):
        for b in simulate_draw(masses, rng):
            counts[b] += 1
    return counts, masses


def pearson(x, y):
    x = np.asarray(x, float)
    y = np.asarray(y, float)
    x = x - x.mean()
    y = y - y.mean()
    return float((x @ y) / math.sqrt((x @ x) * (y @ y)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--draws", type=int, default=250,
                    help="tirages simulés par configuration (défaut 250)")
    ap.add_argument("--configs", default="0,0.10,0.30",
                    help="liste de Δm/m amplifiés à tester")
    args = ap.parse_args()
    configs = [float(x) for x in args.configs.split(",")]

    print("=" * 74)
    print("  SIMULATION PHYSIQUE DU BOULIER — dose-réponse masse -> biais")
    print("=" * 74)
    print(f"Tambour R={R_DRUM} m, {K} boules r={R_BALL} m, m0={M0*1000:.0f} g, "
          f"rotation {OMEGA} rad/s")
    print(f"{args.draws} tirages simulés par configuration ; masse(boule) = "
          f"m0·(1 + Δm/m·encre_normalisée)\n")

    slopes = []
    for rel_dm in configs:
        counts, masses = run_config(rel_dm, args.draws, seed=987)
        total = counts.sum()
        freq_rel = counts / (total / K)          # 1.0 = fréquence uniforme
        r = pearson(masses, freq_rel) if rel_dm > 0 else float("nan")
        # pente : variation relative de fréquence entre boule la plus lourde
        # et la plus légère (régression linéaire sur la masse relative)
        x = (masses - masses.mean()) / M0
        y = freq_rel - freq_rel.mean()
        slope = float((x @ y) / (x @ x)) if rel_dm > 0 else 0.0
        if rel_dm > 0:
            slopes.append((rel_dm, slope))
        if rel_dm > 0:
            heavy = freq_rel[np.argsort(masses)[-10:]].mean()
            light = freq_rel[np.argsort(masses)[:10]].mean()
            print(f"  Δm/m = {rel_dm:5.0%} : corr(masse,fréq) = {r:+.3f} ; "
                  f"10 lourdes {heavy:.3f} vs 10 légères {light:.3f} "
                  f"(1.000 = uniforme)")
        else:
            spread = float(freq_rel.std())
            print(f"  Δm/m =    0% (contrôle) : dispersion des fréquences "
                  f"σ={spread:.3f} — bruit d'échantillonnage pur")

    if slopes:
        # sensibilité : biais relatif par unité de Δm/m (moyenne des pentes)
        k = sum(s / d for d, s in [(d, s) for d, s in slopes]) / len(slopes)
        print(f"\n  Pente ajustée : biais relatif ≈ {k:+.2f} x (Δm/m)")
        real_dm = 3.0e-4    # ~25 mg d'encre/gravure sur 80 g
        real_bias = k * real_dm
        print(f"  Extrapolation à l'écart RÉEL Δm/m ≈ {real_dm:.2%} "
              f"(≈25 mg sur 80 g) :")
        print(f"      biais de fréquence prédit ≈ {real_bias:+.3%} par boule")
        p = M / K
        if abs(real_bias) > 1e-12:
            n_detect = (2.80 ** 2) * (1 - p) / (p * real_bias ** 2)
            years = n_detect / 156
            print(f"      tirages nécessaires pour le DÉTECTER (80% puissance) :"
                  f" {n_detect:,.0f}  (~{years:,.0f} ans de Loto)")
        print(f"""
  Ordre de grandeur analytique (modèle de Boltzmann granulaire) :
  biais ~ Δm/m x (m·g·h / T_eff) ~ {real_dm:.1e} x O(1) — même conclusion.

  Rappel historique : la fraude du 'Triple Six Fix' (Pennsylvanie, 1980)
  a nécessité d'INJECTER de la peinture (plusieurs grammes, Δm/m ~ 5-10%)
  pour créer un biais exploitable — et elle a été détectée immédiatement.
  C'est 100 a 300 fois l'écart d'encre réel entre deux boules.""")


if __name__ == "__main__":
    main()
