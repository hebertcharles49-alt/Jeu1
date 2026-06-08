/*
 * scps_legitimacy.c — la légitimité émergente (voir scps_legitimacy.h)
 *
 * Constantes à calibrer. Aucune valeur de L n'est posée en dur : L relâche
 * lentement vers une cible lue sur l'alignement, l'aisance, la contrainte et
 * l'ancienneté de tutelle.
 */
#include "scps_legitimacy.h"
#include <stddef.h>   /* NULL */

#define K_ALIGN          1.0f
#define W_ALIGN          0.55f
#define W_AISANCE        0.45f
#define K_COERC          3.0f     /* coercition locale [0..1] → impact */
#define K_H              0.4f     /* part de H (contrainte du pays)    */
#define YEARS_INTEGRATE  40.f     /* durée pleine d'intégration        */
#define RELAX_RATE       0.04f    /* inertie : L tend lentement vers L* */
#define L_CONQUEST_FLOOR 1.5f
#define K_BUILD_H        0.6f     /* la coercition BÂTIE (garnisons) ronge L */
#define K_FAITH          0.7f     /* la foi BÂTIE (temples) SOUTIENT L (contre l'ombre) */

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float absf(float v) { return v < 0.f ? -v : v; }

/* Distance de CONTENU (L∞ sur valeurs/subsistance/parenté/religion) entre deux
 * profils de population — la friction, langue exclue (horloge). */
/* La FOI est ACTIVE (§3 légitimité sacrée) : régner sur une AUTRE BRANCHE de foi
 * éloigne (alignement ↓ → L ↓), au-delà de l'axe religion. */
#define FAITH_BRANCH_PEN 3.5f
static float content_dist(const PopCulture *a, const PopCulture *b) {
    float dv = absf(a->valeurs     - b->valeurs);
    float ds = absf(a->subsistance - b->subsistance);
    float dp = absf(a->parente     - b->parente);
    float dr = absf(a->religion    - b->religion);
    if (a->rel_branch!=b->rel_branch && dr<FAITH_BRANCH_PEN) dr=FAITH_BRANCH_PEN;
    float m = dv; if (ds>m) m=ds; if (dp>m) m=dp; if (dr>m) m=dr;
    return m;
}

/* Culture régnante d'un pays = celle de sa région-capitale. */
static const PopCulture *ruling_culture(const World *w, const WorldEconomy *econ, int cid) {
    if (cid < 0 || cid >= w->n_countries) return NULL;
    int cap_prov = w->country[cid].capital_prov;
    if (cap_prov < 0 || cap_prov >= w->n_provinces) return NULL;
    int cap_reg = w->province[cap_prov].region;
    if (cap_reg < 0 || cap_reg >= econ->n_regions) return NULL;
    return &econ->region[cap_reg].culture;
}

static float region_pop(const WorldEconomy *econ, int r) {
    const RegionEconomy *re = &econ->region[r];
    return re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
         + re->strata[CLASS_ELITE].pop;
}

void legitimacy_init(WorldLegitimacy *wl, const World *w, const WorldEconomy *econ) {
    (void)w;
    for (int r = 0; r < SCPS_MAX_REG; r++) { wl->L[r] = 0.f; wl->years_held[r] = 0.f; }
    for (int r = 0; r < econ->n_regions; r++) {
        if (econ->region[r].culture.settled) {
            wl->L[r]          = 5.f;             /* neutre au départ */
            wl->years_held[r] = YEARS_INTEGRATE; /* les foyers initiaux sont intégrés */
        }
    }
    wl->seeded = true;
}

void legitimacy_on_conquest(WorldLegitimacy *wl, int region_id) {
    if (region_id < 0 || region_id >= SCPS_MAX_REG) return;
    if (wl->L[region_id] > L_CONQUEST_FLOOR) wl->L[region_id] = L_CONQUEST_FLOOR;
    wl->years_held[region_id] = 0.f;            /* l'intégration recommence */
}

void legitimacy_tick(WorldLegitimacy *wl, const World *w,
                     const WorldEconomy *econ, const TechState ts[]) {
    if (!wl->seeded) legitimacy_init(wl, w, econ);
    for (int r = 0; r < econ->n_regions; r++) {
        const RegionEconomy *re = &econ->region[r];
        if (!re->culture.settled) { wl->L[r] = 0.f; wl->years_held[r] = 0.f; continue; }

        wl->years_held[r] += 1.f;   /* un tick = un an de tutelle */
        float integ = clampf(wl->years_held[r] / YEARS_INTEGRATE, 0.f, 1.f);

        int cid = re->owner;
        const PopCulture *R = ruling_culture(w, econ, cid);
        float align = R ? (10.f - K_ALIGN * content_dist(&re->culture, R)) : 5.f;

        /* aisance : la satisfaction des besoins fait foi du « pain » (0..1 → 0..10),
         * lue sur l'éco du tick précédent (anti-circularité L↔prospérité). */
        float aisance = clampf(re->satisfaction * 10.f, 0.f, 10.f);

        float country_H = (ts && cid >= 0 && cid < w->n_countries) ? ts[cid].H : 0.f;
        /* coercition reçue + densité coercitive BÂTIE (garnisons/citadelles)
         * → tenir par la force baisse le consentement local. */
        float ombre = K_COERC * re->coercion + K_H * country_H
                    + K_BUILD_H * re->build.H_coerc;

        /* la foi bâtie (temples) relève le consentement : elle CONTRE l'ombre
         * coercitive (un trône qui sacralise tient sans tout réprimer). */
        float lumiere = K_FAITH * re->build.faith;
        float Lstar = (W_ALIGN * align + W_AISANCE * aisance) * integ - ombre + lumiere;
        Lstar = clampf(Lstar, 0.f, 10.f);

        wl->L[r] += (Lstar - wl->L[r]) * RELAX_RATE;   /* inertie : pas d'achat instantané */
        wl->L[r]  = clampf(wl->L[r], 0.f, 10.f);
    }
}

float legitimacy_country(const WorldLegitimacy *wl, const World *w,
                         const WorldEconomy *econ, int cid) {
    (void)w;
    float num = 0.f, den = 0.f;
    for (int r = 0; r < econ->n_regions; r++) {
        if (!econ->region[r].culture.settled || econ->region[r].owner != cid) continue;
        float pop = region_pop(econ, r);
        num += pop * wl->L[r];
        den += pop;
    }
    return den > 0.f ? num / den : 5.f;
}
