/*
 * scps_core.c — moteur SCPS headless (voir scps_core.h)
 *
 * Transcription DIRECTE du bloc de référence de l'annexe. Les constantes
 * (0.4, 0.30, 0.08, 0.6, 0.55, 0.5625, 0.8, 0.35…) sont calibrées dans le
 * modèle d'origine : on ne les « nettoie » pas. Aucune logique ajoutée par
 * rapport à l'annexe — uniquement ce qu'elle écrit.
 */
#include "scps_core.h"
#include <math.h>

static inline float absf(float v) { return v < 0.f ? -v : v; }

/* ===================================================================== */
/* §2.2 — DISTANCE                                                       */
/* ===================================================================== */
float scps_sigmoid(float x) { return 1.f / (1.f + expf(-x)); }

float scps_dist_bar(const ScpsFiche *a, const ScpsFiche *b) {
    /* D̄ = √((1/5)·Σ δ_k²) sur les 5 axes (annexe). */
    float s = 0.f;
    for (int k = 0; k < SCPS_NAXES; k++) {
        float d = a->axis[k] - b->axis[k];
        s += d * d;
    }
    return sqrtf(s / (float)SCPS_NAXES);
}

float scps_dist_inf(const ScpsFiche *a, const ScpsFiche *b) {
    /* D∞ = max δ_k sur le CONTENU (langue exclue — cf. en-tête / §4.5). */
    float m = 0.f;
    for (int k = 0; k < SCPS_NAXES; k++) {
        if (k == SCPS_LANGUE) continue;
        float d = absf(a->axis[k] - b->axis[k]);
        if (d > m) m = d;
    }
    return m;
}

float scps_clock(const ScpsFiche *a, const ScpsFiche *b) {
    return absf(a->axis[SCPS_LANGUE] - b->axis[SCPS_LANGUE]);
}

/* ===================================================================== */
/* §2.3 — PROSPÉRITÉ EXTERNE                                             */
/* ===================================================================== */
float scps_bell(float D_bar) { return D_bar * (10.f - D_bar) / 25.f; }

float scps_metabolisation(float P, float D_inf, float K) {
    return scps_sigmoid(0.8f * (P - D_inf) + 0.35f * (K - 5.f));
}

float scps_PE(float C, float P, float K, float D_bar, float D_inf) {
    return 10.f * (C / 10.f) * scps_bell(D_bar) * scps_metabolisation(P, D_inf, K);
}

/* ===================================================================== */
/* §2.4 — ORDRE INTERNE                                                  */
/* ===================================================================== */
ScpsOrder scps_order(const ScpsState *s) {
    ScpsOrder o;
    o.K_prime       = s->K + (10.f - s->K) * 0.4f * (s->F / 10.f);
    o.coercition    = (s->H / 10.f) * o.K_prime * (10.f - s->L) / 10.f;
    o.R             = o.K_prime * (0.30f + 0.08f * s->L) + 0.6f * o.coercition;
    o.I_prime       = s->I * (s->C / 10.f) * (10.f - s->H) / 10.f;
    o.amplificateur = 1.f + 0.8f * (10.f - s->L) / 10.f;
    o.pression      = o.I_prime * o.amplificateur;
    float dr        = (s->P / 10.f) * s->C + s->flux_faustien - s->K;
    o.dereal        = dr > 0.f ? dr : 0.f;
    o.fracture      = 0.55f * (s->D_bar / 10.f) * (10.f - s->L);
    o.S             = o.pression + 0.5625f * o.dereal + o.fracture;
    o.SI            = 10.f * scps_sigmoid(0.8f * (o.R - o.S));
    /* R ≥ ~0.9 dès que K,F>0 ; garde-fou anti-division par zéro pour le cas
     * limite K=F=0 (état sans aucune institution). */
    float Rden      = o.R > 1e-6f ? o.R : 1e-6f;
    o.fragilite     = 10.f * (0.6f * o.coercition) / Rden;
    return o;
}

ScpsMode scps_mode(const ScpsOrder *o) {
    if (o->SI < 5.f)
        return (o->pression >= o->fracture) ? SCPS_SUBMERGE_REVOLUTION
                                            : SCPS_SUBMERGE_SECESSION;
    if (o->fragilite >= 5.f) return SCPS_COERCITIF_FRAGILE;
    return SCPS_CONSENTI;
}

const char *scps_mode_name(ScpsMode m) {
    switch (m) {
        case SCPS_CONSENTI:            return "consenti";
        case SCPS_COERCITIF_FRAGILE:   return "coercitif-fragile";
        case SCPS_SUBMERGE_REVOLUTION: return "submergé → révolution";
        case SCPS_SUBMERGE_SECESSION:  return "submergé → sécession";
        default:                       return "?";
    }
}

float scps_diversity(const ScpsFiche *fiches, int n) {
    if (n < 2) return 0.f;
    float sum = 0.f; int pairs = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            sum += scps_dist_bar(&fiches[i], &fiches[j]);
            pairs++;
        }
    return pairs ? sum / (float)pairs : 0.f;
}
