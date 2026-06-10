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
#include "scps_core.h"        /* scps_order : le moteur d'ordre interne §2.4 vérifié */
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

/* ---- Profil culturel d'un pays ---------------------------------------- *
 * Lu sur la POPULATION (RegionEconomy.culture), pas sur la géographie : seules
 * les régions PEUPLÉES (settled) comptent, pondérées par leur population.
 * Agrégé par ce que le pays POSSÈDE (owner), pas par l'assignation géographique
 * figée — ainsi une conquête transfère bien la région (et sa diversité). */
static void compute_profile(const WorldEconomy *econ, const World *w, int cid,
                            CulturalProfile *prof) {
    memset(prof, 0, sizeof(*prof));
    (void)w;

    /* CLÉ DE VOÛTE : on collecte les fiches par GROUPE (clé de voûte démographique)
     * à travers les régions possédées — le D interne se lit ENTRE les groupes.
     * Repli mono-groupe : une région non attachée (n_groups=0) compte pour sa
     * RegionEconomy.culture, et une région attachée à UN groupe-substrat donne la
     * MÊME entrée → les nombres d'hier (non-régression). */
    #define PROF_CAP (SCPS_MAX_REG * 2)
    const PopCulture *cs[PROF_CAP]; double wt[PROF_CAP]; int n = 0;
    for (int rid = 0; rid < econ->n_regions; rid++) {
        const RegionEconomy *re = &econ->region[rid];
        if (re->owner != cid || !re->culture.settled) continue;
        if (re->pop.n_groups > 0) {
            for (int k = 0; k < re->pop.n_groups && n < PROF_CAP; k++) {
                if (re->pop.groups[k].count <= 0) continue;
                cs[n] = &re->pop.groups[k].culture; wt[n] = (double)re->pop.groups[k].count; n++;
            }
        } else if (n < PROF_CAP) {
            double pop = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                       + re->strata[CLASS_ELITE].pop;
            if (pop < 1.0) pop = 1.0;
            cs[n] = &re->culture; wt[n] = pop; n++;
        }
    }
    if (n == 0) return;

    /* Moyenne pondérée par la population (la culture est une propriété des gens). */
    double wsum=0, sv=0, ss=0, sp=0, sr=0;
    for (int i=0;i<n;i++){ double p=wt[i]; wsum+=p;
        sv+=p*cs[i]->valeurs; ss+=p*cs[i]->subsistance; sp+=p*cs[i]->parente; sr+=p*cs[i]->religion; }
    if (wsum>0){ prof->valeurs=(float)(sv/wsum); prof->subsistance=(float)(ss/wsum);
                 prof->parente=(float)(sp/wsum); prof->religion=(float)(sr/wsum); }

    /* Distances par paires de GROUPES (langue exclue) : D̄_int = moyenne, D∞_int = max
     * (le MAILLON FAIBLE). Une province conquise mixte y injecte sa diversité interne. */
    float sum_dist=0.f, max_dist=0.f; int pairs=0;
    for (int i=0;i<n;i++) for (int j=i+1;j<n;j++){
        const PopCulture *a=cs[i], *b=cs[j];
        float dv=fabsf_local(a->valeurs-b->valeurs), ds=fabsf_local(a->subsistance-b->subsistance);
        float dp=fabsf_local(a->parente-b->parente), dr=fabsf_local(a->religion-b->religion);
        float dinf=dv; if(ds>dinf)dinf=ds; if(dp>dinf)dinf=dp; if(dr>dinf)dinf=dr;
        sum_dist+=dinf; if(dinf>max_dist)max_dist=dinf; pairs++;
    }
    prof->D_bar_int = (pairs>0) ? sum_dist/(float)pairs : 0.f;
    prof->D_inf_int = max_dist;
    #undef PROF_CAP
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

/* ---- Gouvernance & pression : entrées de scps_order -------------------- *
 * Tant qu'aucun levier (centralisation/ouverture, doc §7) n'est branché, P et
 * F sont neutres. La pression I émerge des besoins non couverts du pays. */
static float governance_P(const World *w, int cid) { (void)w; (void)cid; return 5.f; }
static float governance_F(const World *w, int cid) { (void)w; (void)cid; return 5.f; }

static float econ_fiscal_pressure(const WorldEconomy *econ, int cid) {
    float pop_sum = 0.f, unmet = 0.f;
    for (int r = 0; r < econ->n_regions; r++) {
        const RegionEconomy *re = &econ->region[r];
        if (re->owner != cid || !re->culture.settled) continue;
        float pop = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                  + re->strata[CLASS_ELITE].pop;
        pop_sum += pop;
        unmet   += pop * (1.f - re->satisfaction);   /* besoins non couverts → charge */
    }
    if (pop_sum <= 0.f) return 3.f;
    /* base modérée + surcroît selon les besoins non couverts (proxy calibrable :
     * un État repu digère peu, une famine impose une lourde charge). */
    return clampf(2.5f + (unmet / pop_sum) * 4.f, 0.f, 10.f);
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
                     const TechState ts[], const WorldLegitimacy *wl) {
    int NC = w->n_countries;
    wp->n_countries = NC;

    /* ---- Passe 1 : profil culturel + C (SI calculée en passe 3) ---------- */
    for (int cid = 0; cid < NC; cid++) {
        CountryProsperity *cp = &wp->country[cid];
        compute_profile(econ, w, cid, &cp->profile);

        float C_base = compute_C_base(w, net, cid);
        /* C update : EMA lente + croissance proportionnelle à P_réalisé (sera
           ajoutée en fin de passe 3 ; ici on fait le decay + base pull) */
        cp->C = cp->C * (1.f - 0.008f) + C_base * 0.008f * 5.f;
        cp->C = clampf(cp->C, 0.f, 10.f);
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
        float Lt= ts_c ? ts_c->L        : 3.f;   /* tech L (ordre consenti) → croissance */
        float P = ts_c ? ts_c->puissance: 3.f;   /* puissance → porte PE (§2.3)          */
        float H = ts_c ? ts_c->H        : 0.f;
        /* Connectivité EFFECTIVE = C du pays + bonus mondial des Âges (Commerce).
         * Offset de lecture (non cumulatif) : le contact devient plus fécond pour
         * tout le monde quand le monde devient commerçant. */
        float C = clampf(cp->C + wp->age_C_bonus, 0.f, 10.f);
        float flux_f = (ts_c ? tech_flux(ts_c) : 0.f) + wp->age_breach_flux;

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
            /* L∞ entre les deux profils-pays (un seul couple → D̄ = D∞),
             * cohérent avec culture_content_distance() et le profil interne. */
            float d_inf_ext = dv;
            if (ds   > d_inf_ext) d_inf_ext = ds;
            if (dp_a > d_inf_ext) d_inf_ext = dp_a;
            if (dr   > d_inf_ext) d_inf_ext = dr;
            cp->PE_externe += PE_contact(C, K, P, d_inf_ext, d_inf_ext);
        }

        cp->P_potentiel = cp->PE_interne + cp->PE_externe;

        /* ---- ORDRE INTERNE : le moteur vérifié scps_order (§2.4) ----------- *
         * SI / fragilité / fracture / mode ÉMERGENT du §2.4 avec la LÉGITIMITÉ
         * VIVANTE comme entrée — fini le proxy K·0.7+(10−H)·0.3. C'est ici que
         * le dragon devient mortel : un ordre coercitif-fragile craque au choc. */
        float Lg = wl ? legitimacy_country(wl, w, econ, cid) : Lt;
        ScpsState st = {0};
        st.D_bar = cp->profile.D_bar_int;   /* diversité interne (§2.4) ; D∞ sert au PE (§2.3) */
        st.C     = C;
        st.P     = governance_P(w, cid);          /* perméabilité (gouvernance/ouverture) */
        st.K     = K;
        st.H     = H;
        st.F     = governance_F(w, cid);          /* fédéralisme (centralisation)         */
        st.I     = econ_fiscal_pressure(econ, cid);
        st.L     = Lg;                            /* ← l'entrée vivante                   */
        st.flux_faustien = flux_f;

        /* Couche BIOLOGIQUE : les leviers de la RACE du pays (lus sur la région-
         * capitale) déplacent les entrées — Nain bâtisseur K+ mais factieux
         * fracture+, Orque coercition+, Halfelin perméabilité+, etc. */
        float race_prod = 0.f;   /* productivité de la race → échelle P_réalisé */
        {
            int cap_prov = w->country[cid].capital_prov;
            int cap_reg  = (cap_prov>=0 && cap_prov<w->n_provinces)
                         ? w->province[cap_prov].region : -1;
            if (cap_reg>=0 && cap_reg<econ->n_regions) {
                SpeciesBuild   sb  = species_default_build(econ->region[cap_reg].culture.race);
                SpeciesLeviers lev = build_leviers(&sb);
                st.K     = clampf(st.K     + lev.capacite,     0.f, 10.f);
                st.P     = clampf(st.P     + lev.permeabilite, 0.f, 10.f);
                st.H     = clampf(st.H     + lev.coercition,   0.f, 10.f);
                st.D_bar = clampf(st.D_bar + lev.fracture,     0.f, 10.f);  /* fracture interne */
                st.flux_faustien += lev.arcane;   /* arcane → pente faustienne (Elfe Arcanique) */
                race_prod = lev.productivite;     /* Gnome Inventif / Orque Borné → rendement */
                /* garde anti-contagion : un levier dégénéré (NaN/inf) polluerait
                 * toute la chaîne P_réalisé → tech_cost. clampf laisse passer NaN. */
                if (!isfinite(race_prod)) race_prod = 0.f;
                race_prod = clampf(race_prod, -0.9f, 9.f);
            }
        }

        /* Densité institutionnelle BÂTIE par le joueur (couche d'agency) :
         * agrège les édifices du pays sur K/P/H, plafond ±5 (rendements
         * décroissants). Un Tribunal monte K, une Citadelle monte H. */
        {
            float bK=0.f, bP=0.f, bH=0.f, bPE=0.f, bArcane=0.f;
            for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid) {
                bK  += econ->region[r].build.K_inst;
                bP  += econ->region[r].build.P_open;
                bH  += econ->region[r].build.H_coerc;
                bPE += econ->region[r].build.PE_infra;   /* marchés/entrepôts → PE capté */
                bPE += econ->region[r].route_pe;         /* routes commerciales (cloche f(D̄)) */
                bArcane += econ->region[r].arcane_charge;/* essence brûlée → Brèche */
            }
            st.K = clampf(st.K + clampf(bK,0.f,5.f), 0.f, 10.f);
            st.P = clampf(st.P + clampf(bP,0.f,5.f), 0.f, 10.f);
            st.H = clampf(st.H + clampf(bH,0.f,5.f), 0.f, 10.f);
            cp->P_potentiel += clampf(bPE,0.f,8.f);    /* infrastructure + carrefour commercial */
            /* ARCANE (le fil faustien) : brûler le cristal/essence MONTE le flux
             * faustien → surchauffe → déréalisation → rapproche la Brèche. */
            st.flux_faustien = clampf(st.flux_faustien + clampf(bArcane*0.5f,0.f,6.f), 0.f, 10.f);
        }

        /* ---- ÂGES STRUCTURELS : on POUSSE les entrées globales du moteur ---- *
         * Aucun mécanisme de révolution/fascisme codé : le verdict §2.4
         * (révolution/sécession/coercitif-fragile, fragilité) produit les
         * conséquences pays par pays. L'âge n'est qu'une pression sur une
         * coordonnée mondiale ; la catastrophe est émergente.
         *   Lumières     : + I  (les idées surgissent)
         *   Soulèvements : − L  (la légitimité ne porte plus l'ordre — contagion)
         *   Ordre de Fer : + H  (la poigne) ET − D̄ effectif (le mythe NIE la
         *                  diversité au lieu de la métaboliser) → ordre APPARENT
         *                  haut mais fragilité maxée : le géant cassant. */
        st.I     = clampf(st.I     + wp->age_I_bonus,      0.f, 10.f);
        st.H     = clampf(st.H     + wp->age_H_bonus,      0.f, 10.f);
        st.D_bar = clampf(st.D_bar - wp->age_myth_homogen, 0.f, 10.f);
        /* Les Lumières DISSOLVENT la légitimité coercitive : un ordre qui reposait
         * sur la poigne (H haut) en perd d'autant plus — sa fragilité monte. Un
         * ordre consenti (H bas) n'y perd presque rien. */
        st.L     = clampf(Lg - wp->age_L_penalty - wp->age_lumiere_solvent*(st.H/10.f),
                          0.f, 10.f);

        cp->K = st.K;   /* capacité EFFECTIVE (tech+race+bâti) — lue par l'IA (frein D∞/K) */
        ScpsOrder o = scps_order(&st);
        cp->SI        = o.SI;
        cp->fragilite = o.fragilite;
        cp->fracture  = o.fracture;
        cp->dereal    = o.dereal;            /* déréalisation faustienne — lue par les Âges */
        cp->L         = st.L;                /* exposé pour la membrane (légitimité EFFECTIVE) */
        cp->mode      = (int)scps_mode(&o);
        cp->rendement = clampf((o.SI / 10.f) * (1.f - LAMBDA * o.fragilite / 10.f), 0.f, 1.f);
        cp->P_realise = cp->P_potentiel * cp->rendement * (1.f + race_prod);  /* productivité de race */

        /* Sorties */
        cp->Lumiere        = BETA  * cp->P_potentiel * (K / 10.f);
        cp->tresor_tick    = GAMMA * cp->P_realise;
        cp->croissance_tick= DELTA * cp->P_realise * (Lt / 10.f);

        /* Surchauffe (§2.3, puissance × charge faustienne — distincte de la
         * déréalisation interne déjà portée par scps_order). */
        float charge_f = ts_c ? ts_c->charge : 0.f;
        float I_factor  = (10.f - H) / 10.f;
        float surch_raw = ((P / 10.f) * charge_f + flux_f) * I_factor - K;
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
    /* D̄ et D∞ sont multi-octets (macron combinant, ∞) : on pad sur la largeur
     * en OCTETS (58) pour obtenir les 54 COLONNES affichées du gabarit. */
    {
        char dl[96];
        snprintf(dl, sizeof dl, "    D̄_int=%.3f   D∞_int=%.3f",
                 cp->profile.D_bar_int, cp->profile.D_inf_int);
        printf("║%-58s║\n", dl);
    }
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  C=%.3f  SI=%.3f  pôle=%s\n",
           cp->C, cp->SI, pole_state_name(cp->pole));
    printf("║  fragilité=%.3f  fracture=%.3f  mode=%s\n",
           cp->fragilite, cp->fracture, scps_mode_name((ScpsMode)cp->mode));
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
