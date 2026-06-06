/*
 * scps_prosperity.c — Générateur de Prospérité (PE/SI)
 *
 * Formules clés (doc §PE/SI) :
 *   f(D̄)        = D̄·(10−D̄)/25            // cloche, pic=1.0 en D̄=5
 *   métabolisation = σ(0.8·(P−D∞) + 0.35·(K−5))   // sigmoid (porte)
 *   PE(n)       = 10·(C/10)·f(D̄)·métabolisation
 *   SI          = K·0.70 + (10−H)·0.30
 *   rendement   = (SI/10)·(1 − λ·fragilité/10)   λ=0.45
 *   P_réalisé   = P_potentiel·rendement
 *   Lumière     = β·P_pot·(K/10)  β=0.40
 *   tresor_tick = γ·P_réalisé      γ=1.5
 *   croissance  = δ·P_réalisé·(L/10)  δ=0.12
 *   surchauffe  = max(0, (P/10)·C + flux_faustien − K)
 */
#include "scps_prosperity.h"
#include "scps_culture.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* ---- Paramètres -------------------------------------------------------- */
#define LAMBDA  0.45f
#define BETA    0.40f
#define GAMMA   1.5f
#define DELTA   0.12f

/* ---- Utilitaires ------------------------------------------------------- */
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline float fabsf_local(float v) { return v < 0.f ? -v : v; }

/* Fonction cloche : f(D̄) = D̄·(10−D̄)/25, pic=1.0 en D̄=5 */
static float bell_f(float d) {
    if (d < 0.f) d = 0.f;
    if (d > 10.f) d = 10.f;
    return d * (10.f - d) / 25.f;
}

/* Sigmoïde : σ(x) = 1/(1+exp(-x)) */
static float sigmoid(float x) {
    return 1.f / (1.f + expf(-x));
}

/* ---- Collecte des provinces d'un pays ---------------------------------- */
static int country_provinces(const World *w, int cid,
                              int out[/*60*/], int max_out) {
    int n = 0;
    const Country *co = &w->country[cid];
    for (int ri = 0; ri < co->n_regions && n < max_out; ri++) {
        int rid = co->region_ids[ri];
        if (rid < 0 || rid >= w->n_regions) continue;
        const Region *reg = &w->region[rid];
        for (int pi = 0; pi < reg->n_provinces && n < max_out; pi++) {
            int pid = reg->province_ids[pi];
            if (pid >= 0 && pid < w->n_provinces) {
                out[n++] = pid;
            }
        }
    }
    return n;
}

/* ---- Profil culturel d'un pays ---------------------------------------- */
static void compute_profile(const World *w, int cid, CulturalProfile *prof) {
    int pids[60];
    int np = country_provinces(w, cid, pids, 60);
    memset(prof, 0, sizeof(*prof));

    if (np == 0) return;

    /* Moyenne pondérée par area */
    double wsum = 0.0, sv = 0.0, ss = 0.0, sp = 0.0, sr = 0.0;
    for (int i = 0; i < np; i++) {
        const Province *prov = &w->province[pids[i]];
        double a = (double)prov->area;
        wsum += a;
        sv   += a * prov->valeurs;
        ss   += a * prov->subsistance;
        sp   += a * prov->parente;
        sr   += a * prov->religion;
    }
    if (wsum > 0.0) {
        prof->valeurs      = (float)(sv / wsum);
        prof->subsistance  = (float)(ss / wsum);
        prof->parente      = (float)(sp / wsum);
        prof->religion     = (float)(sr / wsum);
    }

    /* Distances par paires de provinces — D̄_int et D∞_int */
    float sum_dist = 0.f, max_dist = 0.f;
    int pairs = 0;
    for (int i = 0; i < np; i++) {
        const Province *a = &w->province[pids[i]];
        for (int j = i+1; j < np; j++) {
            const Province *b = &w->province[pids[j]];
            float dv = fabsf_local(a->valeurs     - b->valeurs);
            float ds = fabsf_local(a->subsistance - b->subsistance);
            float dp = fabsf_local(a->parente     - b->parente);
            float dr = fabsf_local(a->religion    - b->religion);
            float mean4 = (dv + ds + dp + dr) / 4.f;
            float max4  = dv; if (ds>max4) max4=ds; if (dp>max4) max4=dp; if (dr>max4) max4=dr;
            sum_dist += mean4;
            if (max4 > max_dist) max_dist = max4;
            pairs++;
        }
    }
    prof->D_bar_int = (pairs > 0) ? sum_dist / (float)pairs : 0.f;
    prof->D_inf_int = max_dist;
}

/* ---- Connectivité de base à partir des liens commerciaux --------------- */
static float compute_C_base(const World *w, const TradeNetwork *net, int cid) {
    if (!net) return 0.f;
    float base = 0.5f;
    for (int li = 0; li < net->n_links; li++) {
        const TradeLink *lk = &net->link[li];
        int ra = lk->ra, rb = lk->rb;
        /* Vérifie si ra ou rb appartient à ce pays */
        int ra_owner = (ra >= 0 && ra < w->n_regions) ? w->region[ra].country : -1;
        int rb_owner = (rb >= 0 && rb < w->n_regions) ? w->region[rb].country : -1;
        if (ra_owner != cid && rb_owner != cid) continue;
        if (lk->sea_route)   base += 0.9f;
        else if (lk->river_route) base += 0.55f;
        else                 base += 0.30f;
    }
    return clampf(base, 0.f, 10.f);
}

/* ---- Stabilité interne ------------------------------------------------- */
static float compute_SI(const TechState *ts) {
    if (!ts) return 4.0f;
    return clampf(ts->K * 0.70f + (10.f - ts->H) * 0.30f, 0.f, 10.f);
}

/* ---- PE d'un contact externe ------------------------------------------ */
static float PE_contact(float C, float K, float P, float d_bar, float d_inf) {
    float fdb = bell_f(d_bar);
    float metro = sigmoid(0.8f * (P - d_inf) + 0.35f * (K - 5.f));
    return 10.f * (C / 10.f) * fdb * metro;
}

/* ======================================================================= */
void prosperity_init(WorldProsperity *wp, const World *w) {
    memset(wp, 0, sizeof(*wp));
    wp->n_countries = w->n_countries;
    for (int c = 0; c < w->n_countries; c++) {
        wp->country[c].C  = 1.0f;  /* connectivité initiale basse */
        wp->country[c].SI = 4.0f;
    }
}

/* ======================================================================= */
void prosperity_tick(WorldProsperity *wp, const World *w,
                     const WorldEconomy *econ, const TradeNetwork *net,
                     const TechState ts[]) {
    (void)econ;  /* reserved for future per-region prosperity queries */
    int NC = w->n_countries;
    wp->n_countries = NC;

    /* ---- Passe 1 : profil culturel + C + SI ----------------------------- */
    for (int cid = 0; cid < NC; cid++) {
        CountryProsperity *cp = &wp->country[cid];
        compute_profile(w, cid, &cp->profile);

        float C_base = compute_C_base(w, net, cid);
        /* C update : EMA lente + croissance proportionnelle à P_réalisé (sera
           ajoutée en fin de passe 3 ; ici on fait le decay + base pull) */
        cp->C = cp->C * (1.f - 0.008f) + C_base * 0.008f * 5.f;
        cp->C = clampf(cp->C, 0.f, 10.f);

        cp->SI = compute_SI(ts ? &ts[cid] : NULL);
    }

    /* ---- Passe 2 : matrice de voisinage (liens = pays différents) -------- */
    /* 48*48 = 2304 bytes — sûr en pile */
    bool neighbors[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    memset(neighbors, 0, sizeof(neighbors));

    if (net) {
        for (int li = 0; li < net->n_links; li++) {
            int ra = net->link[li].ra, rb = net->link[li].rb;
            int oa = (ra >= 0 && ra < w->n_regions) ? w->region[ra].country : -1;
            int ob = (rb >= 0 && rb < w->n_regions) ? w->region[rb].country : -1;
            if (oa >= 0 && ob >= 0 && oa < NC && ob < NC && oa != ob) {
                neighbors[oa][ob] = true;
                neighbors[ob][oa] = true;
            }
        }
    }

    /* ---- Passe 3 : PE + rendement + sorties ----------------------------- */
    for (int cid = 0; cid < NC; cid++) {
        CountryProsperity *cp = &wp->country[cid];
        const TechState   *ts_c = ts ? &ts[cid] : NULL;

        float K = ts_c ? ts_c->K        : 3.f;
        float L = ts_c ? ts_c->L        : 3.f;
        float P = ts_c ? ts_c->puissance: 3.f;
        float H = ts_c ? ts_c->H        : 0.f;
        (void)H;

        float C = cp->C;

        /* PE interne */
        float d_bar_int = cp->profile.D_bar_int;
        float d_inf_int = cp->profile.D_inf_int;
        float fdb_int   = bell_f(d_bar_int);
        float metro_int = sigmoid(0.8f * (P - d_inf_int) + 0.35f * (K - 5.f));
        cp->PE_interne  = 10.f * (C / 10.f) * fdb_int * metro_int;

        /* PE externe : somme sur les voisins */
        cp->PE_externe = 0.f;
        for (int nid = 0; nid < NC; nid++) {
            if (!neighbors[cid][nid]) continue;
            const CulturalProfile *np2 = &wp->country[nid].profile;
            /* D̄ et D∞ entre les profils moyens des deux pays */
            float dv = fabsf_local(cp->profile.valeurs    - np2->valeurs);
            float ds = fabsf_local(cp->profile.subsistance- np2->subsistance);
            float dp_a = fabsf_local(cp->profile.parente  - np2->parente);
            float dr = fabsf_local(cp->profile.religion   - np2->religion);
            float d_bar_ext = (dv + ds + dp_a + dr) / 4.f;
            float d_inf_ext = dv;
            if (ds > d_inf_ext) d_inf_ext = ds;
            if (dp_a > d_inf_ext) d_inf_ext = dp_a;
            if (dr > d_inf_ext) d_inf_ext = dr;
            cp->PE_externe += PE_contact(C, K, P, d_bar_ext, d_inf_ext);
        }

        cp->P_potentiel = cp->PE_interne + cp->PE_externe;

        /* SI & rendement */
        float SI        = cp->SI;
        float fragil    = ts_c ? tech_fragility(ts_c) : 0.f;
        cp->rendement   = (SI / 10.f) * (1.f - LAMBDA * fragil / 10.f);
        cp->rendement   = clampf(cp->rendement, 0.f, 1.f);
        cp->P_realise   = cp->P_potentiel * cp->rendement;

        /* Sorties */
        cp->Lumiere        = BETA  * cp->P_potentiel * (K / 10.f);
        cp->tresor_tick    = GAMMA * cp->P_realise;
        cp->croissance_tick= DELTA * cp->P_realise * (L / 10.f);

        /* Surchauffe */
        float flux_f = ts_c ? tech_flux(ts_c) : 0.f;
        float charge_f = ts_c ? ts_c->charge : 0.f;
        float surch_raw = (P / 10.f) * charge_f + flux_f - K;
        cp->surchauffe = surch_raw > 0.f ? surch_raw : 0.f;

        /* C growth après P_réalisé calculé */
        cp->C += 0.010f * cp->P_realise;
        cp->C  = clampf(cp->C, 0.f, 10.f);

        /* État pôle */
        if (cp->surchauffe > 2.f)            cp->pole = POLE_EN_SURCHAUFFE;
        else if (cp->P_realise > 6.f)        cp->pole = POLE_BOUILLONNANT;
        else if (cp->P_realise > 2.f)        cp->pole = POLE_FLORISSANT;
        else                                  cp->pole = POLE_CALME;
    }
}

/* ======================================================================= */
const char *pole_state_name(PoleState s) {
    switch (s) {
        case POLE_CALME:         return "Calme";
        case POLE_FLORISSANT:    return "Florissant";
        case POLE_BOUILLONNANT:  return "Bouillonnant";
        case POLE_EN_SURCHAUFFE: return "En surchauffe";
        default:                 return "?";
    }
}

/* ======================================================================= */
void prosperity_print_country(const WorldProsperity *wp, const World *w, int cid) {
    if (cid < 0 || cid >= wp->n_countries) return;
    const CountryProsperity *cp = &wp->country[cid];
    const char *name = (cid < w->n_countries) ? w->country[cid].name : "?";

    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║  Pays %-44s║\n", name);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Profil culturel interne :                           ║\n");
    printf("║    valeurs=%.2f  subsistance=%.2f  parenté=%.2f  religion=%.2f\n",
           cp->profile.valeurs, cp->profile.subsistance,
           cp->profile.parente,  cp->profile.religion);
    printf("║    D̄_int=%.3f   D∞_int=%.3f\n",
           cp->profile.D_bar_int, cp->profile.D_inf_int);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  C=%.3f  SI=%.3f  pôle=%s\n",
           cp->C, cp->SI, pole_state_name(cp->pole));
    printf("║  PE_int=%.3f  PE_ext=%.3f  P_pot=%.3f\n",
           cp->PE_interne, cp->PE_externe, cp->P_potentiel);
    printf("║  rendement=%.3f  P_réalisé=%.3f\n",
           cp->rendement, cp->P_realise);
    printf("║  Lumière=%.3f  trésor/tick=%.3f  croissance/tick=%.4f\n",
           cp->Lumiere, cp->tresor_tick, cp->croissance_tick);
    printf("║  surchauffe=%.3f\n", cp->surchauffe);
    printf("╚══════════════════════════════════════════════════════╝\n");
}

/* ======================================================================= */
void prosperity_print_summary(const WorldProsperity *wp, const World *w) {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              RÉSUMÉ PROSPÉRITÉ MONDIALE                      ║\n");
    printf("╠════╤═══════════════════════╤═══════╤════════╤════════════════╣\n");
    printf("║ ID │ Pays                  │ C     │ P_réal │ Pôle           ║\n");
    printf("╠════╪═══════════════════════╪═══════╪════════╪════════════════╣\n");
    for (int cid = 0; cid < wp->n_countries; cid++) {
        const CountryProsperity *cp = &wp->country[cid];
        const char *nm = (cid < w->n_countries) ? w->country[cid].name : "?";
        const char *role_s = "?";
        if (cid < w->n_countries) switch (w->country[cid].role) {
            case POLITY_PLAYER:     role_s = "JOU"; break;
            case POLITY_ANTAGONIST: role_s = "ANT"; break;
            case POLITY_CITY_STATE: role_s = "CIT"; break;
            default:                role_s = "VIE"; break;
        }
        printf("║%3d │ %-21.21s │%6.2f │%7.3f │ %-14s ║\n",
               cid, nm, cp->C, cp->P_realise, pole_state_name(cp->pole));
        (void)role_s;
    }
    printf("╚════╧═══════════════════════╧═══════╧════════╧════════════════╝\n");
}
