/*
 * scps_readout.c — la membrane (voir scps_readout.h)
 *
 * SEUL fichier autorisé à traduire un flottant SCPS en mot. Les seuils sont
 * indicatifs (à calibrer) ; la VOIX est diégétique, in-world — jamais
 * « Faible/Moyen/Élevé », jamais un nombre.
 *
 * Pour cet incrément, l'entrée se fait par flottants nus (testable sans la
 * sim). Les enveloppes lisant WorldProsperity/WorldLegitimacy — qui, elles,
 * incluront scps_core.h pour appeler scps_order — viendront avec le câblage
 * de la légitimité (Partie 1/1.5).
 */
#include "scps_readout.h"
#include "scps_lang.h"   /* la table de chaînes : les MOTS vivent dans les tables compilées */
#include "scps_factions.h"   /* la balance des factions-éthos (§9) */
#include <stddef.h>   /* NULL */
#include <string.h>   /* memset */
#include <math.h>     /* roundf */

static inline float rclampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline int   iclamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ===================================================================== */
/* SEUILLAGE                                                              */
/* ===================================================================== */
BandStab band_stab(float SI, float fragilite) {
    BandStab b;
    if      (SI < 5.0f) b = ST_SUBMERGE;
    else if (SI < 6.5f) b = ST_VACILLANT;
    else if (SI < 8.0f) b = ST_TENU;
    else if (SI < 9.2f) b = ST_ASSURE;
    else                b = ST_INEBRANLABLE;
    /* Un ordre tenu par la contrainte (fragilité ≥ 5) ne se lit jamais mieux
     * que « Tenue » : il a l'air solide, il ne l'est pas. */
    if (fragilite >= 5.0f && b > ST_TENU) b = ST_TENU;
    return b;
}
BandAssise band_assise(float fragilite) {
    if (fragilite < 2.0f) return AS_CONSENTIE;
    if (fragilite < 4.0f) return AS_PARTAGEE;
    if (fragilite < 6.5f) return AS_CONTRAINTE;
    return AS_TYRANNIQUE;
}
BandLegit band_legit(float L) {
    if (L < 2.0f) return LG_USURPEE;
    if (L < 4.0f) return LG_CONTESTEE;
    if (L < 6.0f) return LG_TOLEREE;
    if (L < 8.0f) return LG_RECONNUE;
    return LG_SACREE;
}
BandConcorde band_concorde(float fracture, bool secession_mode) {
    if (secession_mode)   return CO_SECESSION;
    if (fracture < 1.5f)  return CO_UNIE;
    if (fracture < 3.0f)  return CO_MURMURANTE;
    return CO_FRACTUREE;
}
BandProsp band_prosp(float p) {
    if (p < 2.0f) return PR_MISERE;
    if (p < 4.0f) return PR_DISETTE;
    if (p < 6.0f) return PR_SUFFISANCE;
    if (p < 8.0f) return PR_AISANCE;
    return PR_OPULENCE;
}
BandSavoir band_savoir(float lum) {
    if (lum < 1.5f) return SA_OBSCURITE;
    if (lum < 4.0f) return SA_LUEUR;
    if (lum < 7.0f) return SA_FOYER;
    return SA_PHARE;
}
/* FORGE (§7) — profondeur de production matérielle, sur un niveau nu [0..10]. */
BandForge band_forge(float l) {
    if (l < 2.5f) return FORGE_RUDIMENTAIRE;
    if (l < 5.0f) return FORGE_ARTISANALE;
    if (l < 7.5f) return FORGE_MANUFACTURIERE;
    return FORGE_INDUSTRIELLE;
}
/* SYNCRÉTIQUE (§12) — bandes des cercles de contact, classées sur des NUS (la cloison
 * tient : aucun type moteur ici). Les libellés sont DIÉGÉTIQUES et parlent de cultures
 * et de savoir-faire, jamais de races ni de coordonnées. */
BandProfondeur band_profondeur(int d) {
    switch (d) {
        case 0:  return PROF_OBSCURE;
        case 1:  return PROF_SURFACE_B;
        case 2:  return PROF_METIER_B;
        case 3:  return PROF_PROFOND_B;
        default: return PROF_SECRET_B;
    }
}
BandAcces band_acces(float p) {
    if (p >= 1.0f)  return AC_ACQUIS;
    if (p >= 0.75f) return AC_IMMINENT;
    if (p >= 0.35f) return AC_PROCHE;
    return AC_LOINTAIN;
}
const char *label_forge(BandForge b){ return tr_band(STR_FORGE_0, (int)b, 4); }
const char *label_profondeur(BandProfondeur b){ return tr_band(STR_PROF_0, (int)b, 5); }
const char *label_acces(BandAcces b){ return tr_band(STR_ACCES_0, (int)b, 4); }
/* MARCHÉ — l'état d'un bien, classé sur demande vs disponible (nus). MORT = ni
 * offre ni demande (la chaîne ne vit pas) ; le reste suit la couverture. */
BandMarche band_marche(float demand, float avail) {
    if (demand < 0.05f && avail < 0.05f) return MARCHE_MORT;
    if (avail >= demand*3.0f)            return MARCHE_ENGORGE;
    if (avail <  demand*0.35f)           return MARCHE_PENURIE;
    if (avail <  demand*0.80f)           return MARCHE_TENDU;
    return MARCHE_SAIN;
}
BandFidelite band_fidelite(float g) {
    if (g < 0.25f) return FID_FIDELE;
    if (g < 0.50f) return FID_TIEDE;
    if (g < 0.70f) return FID_FRONDEUR;
    return FID_LIGUEUR;
}
BandMoral band_moral(float f) {
    if (f >= 0.70f) return MO_FERME;
    if (f >= 0.45f) return MO_EPROUVE;
    if (f >  0.25f) return MO_VACILLANT;
    return MO_ROMPU;
}
const char *label_moral(BandMoral b){ return tr_band(STR_MORAL_0, (int)b, 4); }
const char *label_fidelite(BandFidelite b){ return tr_band(STR_FIDELITE_0, (int)b, 4); }
const char *label_marche(BandMarche b){ return tr_band(STR_MARCHE_0, (int)b, 5); }

/* ---- LENTILLES de carte (§6) — bandes → teintes DISCRÈTES, par région -------
 * Tout le classement vit ici : le viewer reçoit des couleurs, jamais un float. */
const char *map_lens_name(MapLens l){ return tr_band(STR_LENS_0, (int)l, 4); }
void map_lens_tints(const WorldEconomy *econ, const WorldLegitimacy *wl,
                    MapLens lens, uint32_t out[SCPS_MAX_REG]) {
    static const uint32_t T_PROSP[5]  = { 0xFF5a3d2e,0xFF8a6a3a,0xFFb0975a,0xFFd4b96a,0xFFf0d878 }; /* misère→opulence */
    static const uint32_t T_HUMEUR[5] = { 0xFFb03030,0xFFc07040,0xFFb0a060,0xFF7aa060,0xFF4a9a6a }; /* révoltée→dévouée */
    static const uint32_t T_MARCHE[5] = { 0xFF383838,0xFFc04038,0xFFc08040,0xFF6a9a70,0xFF8090a8 }; /* mort·pénurie·tendu·sain·engorgé */
    static const Resource BASKET[5]   = { RES_GRAIN, RES_TOOLS, RES_IRON, RES_CLOTH, RES_WINE };    /* la tension qui compte */
    for (int r=0; r<SCPS_MAX_REG; r++) out[r]=0xFF202020u;
    if (!econ) return;
    for (int r=0; r<econ->n_regions && r<SCPS_MAX_REG; r++) {
        const RegionEconomy *re=&econ->region[r];
        if (!re->active || !re->colonized) continue;
        switch (lens) {
            case LENS_PROSP: {
                float p=re->prosperity; int b;        /* PIB/tête — seuils = surface d'équilibrage */
                b = (p<0.2f)?0 : (p<0.5f)?1 : (p<1.0f)?2 : (p<2.0f)?3 : 4;
                out[r]=T_PROSP[b];
            } break;
            case LENS_HUMEUR: {
                BandHumeur h = band_humeur(wl ? wl->L[r] : 5.f);
                out[r]=T_HUMEUR[(h>=0&&h<=HU_DEVOUEE)?(int)h:2];
            } break;
            case LENS_MARCHE: {
                int worst=MARCHE_ENGORGE;             /* on ignore les marchés MORTS (pas une détresse) */
                for (int k=0;k<5;k++){
                    BandMarche m=band_marche(re->demand[BASKET[k]], re->supply[BASKET[k]]+re->stock[BASKET[k]]);
                    if (m!=MARCHE_MORT && (int)m<worst) worst=(int)m;
                }
                out[r]=T_MARCHE[worst];
            } break;
            default: out[r]=0xFF404040u; break;
        }
    }
}
/* §11/§12 — le cercle prévisionnel d'un nœud syncrétique, traduit en bandes + chemin
 * diégétique. Lit le cache de profondeur (TechState.arch_depth) et le loquet (sync_unlocked) ;
 * aucun flottant ne franchit la cloison (la chaîne renvoyée parle cultures/savoir-faire). */
SyncReadout sync_node_readout(const TechState *ts, int i) {
    SyncReadout r = { AC_LOINTAIN, PROF_OBSCURE, PROF_OBSCURE, "?", "" };
    const SyncNode *sn = tech_sync_node(i);
    if (!ts || !sn) return r;
    r.nom = sn->name;
    int req = (int)sn->prof_requise;                 /* 1=surface … 4=secret */
    r.requise = band_profondeur(req);
    if (ts->sync_unlocked[i]) {                       /* loqué : acquis pour toujours */
        r.acces = AC_ACQUIS; r.atteinte = r.requise;
        r.chemin = "acquis — diffusé par le contact, et gardé même si la source s'est fondue";
        return r;
    }
    int reached = (sn->arch>=0 && sn->arch<ARCH_COUNT) ? (int)ts->arch_depth[sn->arch] : 0;
    r.atteinte = band_profondeur(reached);
    float prog = (req>0) ? (float)reached/(float)req : 0.f; if (prog>1.f) prog=1.f;
    r.acces = band_acces(prog);
    if      (reached <= 0)   r.chemin = "tradition jamais côtoyée — il faut entrer en contact avec ses porteurs";
    else if (reached < req)  r.chemin = "savoir de surface : le comptoir ne transmet pas l'art profond — gouverne ou voisine cette culture, et légitime le sol";
    else                     r.chemin = "à portée — il manque le socle (recherche le nœud parent du cercle)";
    return r;
}
BandPresage band_presage(float charge) {
    if (charge < 1.0f) return PG_CALME;
    if (charge < 4.0f) return PG_FREMISSEMENT;
    if (charge < 7.0f) return PG_OMBRE;
    return PG_SEUIL;
}
BandHumeur band_humeur(float L) {
    if (L < 2.0f) return HU_REVOLTEE;
    if (L < 4.0f) return HU_FRONDEUSE;
    if (L < 6.0f) return HU_TIEDE;
    if (L < 8.0f) return HU_LOYALE;
    return HU_DEVOUEE;
}
BandAgitation band_agitation(int a) {
    if (a < 25) return AG_CALME;
    if (a < 50) return AG_FREMISSANTE;
    if (a < AGIT_REVOLT_SEUIL) return AG_AGITEE;
    return AG_INSURGEE;
}
BandLignee band_lignee(float clock_dist, float content_dist, bool schism) {
    /* Narcissisme des petites différences : un cousin schismatique fait un
     * pire ennemi qu'un infidèle lointain (cf. doc des pools). */
    if (schism)               return LI_HERETIQUE_PROCHE;
    if (content_dist >= 7.0f) return LI_INASSIMILABLE;   /* axe-mur : jamais sans la force */
    bool clock_near   = (clock_dist   < 3.0f);
    bool content_near = (content_dist < 3.0f);
    if ( clock_near &&  content_near) return LI_MEME_SANG;
    if ( clock_near && !content_near) return LI_COUSINE;        /* horloge proche, contenu dérivé */
    if (!clock_near &&  content_near) return LI_SOEUR_LOINTAINE;/* horloge loin, contenu jumeau */
    return LI_ETRANGERE;
}
BandFoi band_foi(bool same_branch, float religion_dist, bool schism, bool region_fervent) {
    /* La FOI de la province face au culte du trône. Le schisme — cousin du même
     * tronc devenu inconciliable — fait pire qu'une foi étrangère (narcissisme
     * des petites différences). La ferveur fait le DÉVOT : un pluraliste aligné
     * reste tiède (il ne s'embrase pour aucun dogme). */
    if (!same_branch)            return FOI_HERETIQUE;  /* branche sacrée étrangère */
    if (schism)                  return FOI_HERETIQUE;  /* rupture doctrinale ouverte */
    if (religion_dist < 2.0f && region_fervent) return FOI_DEVOTE;
    return FOI_TIEDE;
}

/* ===================================================================== */
/* PROJECTIONS — coordonnée [0..10] → métrique [0..100]                   */
/* ===================================================================== */
int metric_from_coord(float x) { return iclamp((int)roundf(rclampf(x,0.f,10.f)*10.f), 0, 100); }

int metric_stability(float SI, float war_exhaustion) {
    /* Composite légitime : l'usure de guerre RONGE la stabilité apparente. */
    float s = SI - 2.0f * rclampf(war_exhaustion, 0.f, 1.f);
    return iclamp((int)roundf(rclampf(s,0.f,10.f)*10.f), 0, 100);
}
int metric_prosperity(float p)  { return metric_from_coord(p); }
int metric_legitimacy(float L)  { return metric_from_coord(L); }
int metric_cohesion(float frac) { return metric_from_coord(10.f - rclampf(frac,0.f,10.f)); }
int metric_savoir(float lum)    { return metric_from_coord(lum); }

int metric_agitation(float L_local, float coercion, float diversity_tension,
                     float recent_shock, int country_stability, float garrison_H) {
    /* Ce qui SOULÈVE : un consentement bas, la coercition subie, une culture
     * étrangère sous la couronne, un choc récent (conquête, famine). */
    float raise = (10.f - rclampf(L_local,0.f,10.f)) * 4.5f      /* L bas : jusqu'à +45 */
                + rclampf(coercion,0.f,1.f) * 25.f                /* coercition : +25     */
                + rclampf(diversity_tension,0.f,10.f) * 2.0f      /* lignée étrangère : +20 */
                + rclampf(recent_shock,0.f,1.f) * 20.f;           /* choc : +20            */
    /* Ce qui CALME : la stabilité du royaume, la garnison (H bâti). C'est l'effet
     * EXISTANT de H/SI sur l'ordre, lu ici en abattement d'agitation. */
    float calm = (country_stability/100.f) * 20.f                /* Stabilité 100 : −20   */
               + rclampf(garrison_H,0.f,8.f) * 4.0f;             /* citadelle : jusqu'à −32 */
    return iclamp((int)roundf(raise - calm), 0, 100);
}

/* ===================================================================== */
/* EFFETS — une courbe LUE d'une métrique (jamais un modificateur plat)   */
/* ===================================================================== */
float prod_multiplier(int prosperity)   { return 1.f + (prosperity-50)/50.f * 0.15f; }
float agitation_modifier(int stability) { return -(stability/100.f) * 2.0f; }
bool  can_enact_reform(int stability)   { return stability >= STAB_REFORM_MIN; }
float aggression_stability_cost(int stability) {
    /* Un État déjà fragile paie cher l'aventure : surcoût décroissant avec la
     * stabilité (lecture de « la guerre ronge l'ordre tenu »). */
    return rclampf(1.5f - (stability/100.f)*1.2f, 0.3f, 1.5f);
}
float integration_speed(int legitimacy) { return 0.5f + (legitimacy/100.f) * 1.5f; } /* ×0.5..×2 */
float research_pace(int savoir)         { return 0.6f + (savoir/100.f) * 1.4f; }       /* ×0.6..×2 */
bool  revolt_threshold_reached(int agitation) { return agitation >= AGIT_REVOLT_SEUIL; }

/* Petit constructeur de métrique (valeur + mot + déf déjà résolus). */
static MetricReadout mk_metric(int value, const char *word, const char *hover) {
    MetricReadout m; m.value = value; m.word = word; m.hover = hover; return m;
}


/* ===================================================================== */
/* ASSEMBLAGE                                                             */
/* ===================================================================== */
CountryReadout country_readout_from_floats(
    float SI, float fragilite, float fracture, float pression,
    float L, float prosperity_0_10, float lumiere_0_10, float charge_0_10) {

    bool submerge       = (SI < 5.0f);
    bool secession_mode = (submerge && fracture > pression);
    bool revolt_mode    = (submerge && pression >= fracture);
    bool coerc_fragile  = (!submerge && fragilite >= 5.0f);

    CountryReadout r;
    r.stabilite  = band_stab(SI, fragilite);
    r.assise     = band_assise(fragilite);
    r.legitimite = band_legit(L);
    r.concorde   = band_concorde(fracture, secession_mode);
    r.prosperite = band_prosp(prosperity_0_10);
    r.savoir     = band_savoir(lumiere_0_10);
    r.presage    = band_presage(charge_0_10);

    /* Métriques (nombre 0-100 + mot + déf) — projetées des mêmes flottants. */
    r.m_stabilite  = mk_metric(metric_stability(SI, 0.f),         label_stab(r.stabilite),   hover_stab());
    r.m_prosperite = mk_metric(metric_prosperity(prosperity_0_10),label_prosp(r.prosperite), hover_prosp());
    r.m_legitimite = mk_metric(metric_legitimacy(L),              label_legit(r.legitimite), hover_legit());
    r.m_cohesion   = mk_metric(metric_cohesion(fracture),         label_concorde(r.concorde),hover_concorde());
    r.m_savoir     = mk_metric(metric_savoir(lumiere_0_10),       label_savoir(r.savoir),    hover_savoir());
    r.influence    = 0;   /* posée par le statecraft (réserve de réputation) */

    /* Augure : ligne d'ambiance, jamais une jauge — seulement en péril. */
    if      (secession_mode) r.augure = "Les marges parlent de se gouverner seules.";
    else if (revolt_mode)    r.augure = "La rue gronde contre le trône.";
    else if (coerc_fragile)  r.augure = "L'ordre tient — mais par la peur seule.";
    else                     r.augure = (const char *)0;
    return r;
}

AllegeanceReadout allegeance_from_floats(
    float L_local, float clock_dist, float content_dist, bool schism) {
    AllegeanceReadout a;
    a.humeur = band_humeur(L_local);
    a.lignee = band_lignee(clock_dist, content_dist, schism);
    return a;
}

/* ===================================================================== */
/* LEXIQUE — labels (le MOT)                                              */
/* ===================================================================== */
#define LBL(fn, type, base, count) \
    const char *fn(type b){ return tr_band(base##_0, (int)b, count); }
LBL(label_stab,     BandStab,     STR_BANDE_STAB, 5)
LBL(label_assise,     BandAssise,     STR_BANDE_ASSISE, 4)
LBL(label_legit,     BandLegit,     STR_BANDE_LEGIT, 5)
LBL(label_concorde,     BandConcorde,     STR_BANDE_CONCORDE, 4)
LBL(label_prosp,     BandProsp,     STR_BANDE_PROSP, 5)
LBL(label_savoir,     BandSavoir,     STR_BANDE_SAVOIR, 4)
LBL(label_presage,     BandPresage,     STR_BANDE_PRESAGE, 4)
LBL(label_stature,     BandStature,     STR_BANDE_STATURE, 5)
LBL(label_flux,     BandFlux,     STR_BANDE_FLUX, 5)
LBL(label_aisance,     BandAisance,     STR_BANDE_AISANCE, 4)
LBL(label_carrefour,     BandCarrefour,     STR_BANDE_CARREFOUR, 4)
LBL(label_humeur,     BandHumeur,     STR_BANDE_HUMEUR, 5)
LBL(label_lignee,     BandLignee,     STR_BANDE_LIGNEE, 6)
LBL(label_agitation,     BandAgitation,     STR_BANDE_AGITATION, 4)
LBL(label_foi,     BandFoi,     STR_BANDE_FOI, 3)
LBL(label_sedition,     BandSedition,     STR_BANDE_SEDITION, 4)
#undef LBL
BandSedition band_sedition(float t){
    if (t < 0.10f) return SED_CALME;
    if (t < 0.20f) return SED_MURMURE;
    if (t < 0.32f) return SED_TENDUE;
    return SED_SEDITIEUSE;
}

/* ===================================================================== */
/* LEXIQUE — hovers (la DÉFINITION, jamais la valeur)                     */
/* ===================================================================== */
const char *hover_stab(void){ return tr(STR_HOVER_STAB); }
const char *hover_assise(void){ return tr(STR_HOVER_ASSISE); }
const char *hover_legit(void){ return tr(STR_HOVER_LEGIT); }
const char *hover_concorde(void){ return tr(STR_HOVER_CONCORDE); }
const char *hover_prosp(void){ return tr(STR_HOVER_PROSP); }
const char *hover_savoir(void){ return tr(STR_HOVER_SAVOIR); }
const char *hover_presage(void){ return tr(STR_HOVER_PRESAGE); }
const char *hover_stature(void){ return tr(STR_HOVER_STATURE); }
const char *hover_flux(void){ return tr(STR_HOVER_FLUX); }
const char *hover_aisance(void){ return tr(STR_HOVER_AISANCE); }
const char *hover_carrefour(void){ return tr(STR_HOVER_CARREFOUR); }
const char *hover_humeur(void){ return tr(STR_HOVER_HUMEUR); }
const char *hover_lignee(void){ return tr(STR_HOVER_LIGNEE); }
const char *hover_agitation(void){ return tr(STR_HOVER_AGITATION); }
const char *hover_foi(void){ return tr(STR_HOVER_FOI); }
const char *hover_sedition(void){ return tr(STR_HOVER_SEDITION); }

/* ===================================================================== */
/* ENVELOPPES SIM — lisent les sorties STOCKÉES, jamais scps_core         */
/* ===================================================================== */
/* Miroir des valeurs de ScpsMode (scps_core.h) — non inclus ici (cloison). */
enum { RD_CONSENTI = 0, RD_COERC_FRAGILE, RD_SUBMERGE_REVOL, RD_SUBMERGE_SECESS };

static float pc_content_dist(const PopCulture *a, const PopCulture *b) {
    float dv = a->valeurs-b->valeurs;       if (dv<0) dv=-dv;
    float ds = a->subsistance-b->subsistance; if (ds<0) ds=-ds;
    float dp = a->parente-b->parente;       if (dp<0) dp=-dp;
    float dr = a->religion-b->religion;     if (dr<0) dr=-dr;
    float m = dv; if (ds>m) m=ds; if (dp>m) m=dp; if (dr>m) m=dr;
    return m;
}
static const PopCulture *pc_ruling(const World *w, const WorldEconomy *econ, int cid) {
    if (cid < 0 || cid >= w->n_countries) return NULL;
    int cp = w->country[cid].capital_prov;
    if (cp < 0 || cp >= w->n_provinces) return NULL;
    int cr = w->province[cp].region;
    if (cr < 0 || cr >= econ->n_regions) return NULL;
    return &econ->region[cr].culture;
}
static const char *vocation_word(Resource res, bool coastal, Biome b) {
    switch (res) {
        case RES_GRAIN: case RES_COTTON:           return "Grenier";
        case RES_LIVESTOCK: case RES_WOOL:         return "Pâtures";
        case RES_FISH:                             return "Pêcheries";
        case RES_COPPER: case RES_IRON: case RES_COAL:
        case RES_GOLD: case RES_PRECIOUS_METAL:
        case RES_SULFUR: case RES_SALTPETER:       return "Mine";
        case RES_WOOD:                             return "Atelier";
        default: break;
    }
    if (coastal) return "Comptoir";
    if (b == BIO_FOREST || b == BIO_WOODS || b == BIO_JUNGLE) return "Sanctuaire";
    return "Marche";
}

CountryReadout country_readout(const WorldProsperity *wp, const TechState *ts,
                               const World *w, int cid) {
    CountryReadout r; memset(&r, 0, sizeof r);
    if (cid < 0 || cid >= wp->n_countries) { r.augure = NULL; return r; }
    const CountryProsperity *cp = &wp->country[cid];

    r.stabilite  = band_stab(cp->SI, cp->fragilite);
    r.assise     = band_assise(cp->fragilite);
    r.legitimite = band_legit(cp->L);
    r.concorde   = band_concorde(cp->fracture, cp->mode == RD_SUBMERGE_SECESS);
    r.prosperite = band_prosp(rclampf(cp->P_realise, 0.f, 10.f));
    r.savoir     = band_savoir(cp->Lumiere);
    float charge = (ts && cid < w->n_countries) ? rclampf(ts[cid].charge, 0.f, 10.f) : 0.f;
    r.presage    = band_presage(charge);

    /* Métriques de jeu (0-100) — la même coordonnée, surfacée en nombre. */
    r.m_stabilite  = mk_metric(metric_stability(cp->SI, 0.f),               label_stab(r.stabilite),   hover_stab());
    r.m_prosperite = mk_metric(metric_prosperity(rclampf(cp->P_realise,0.f,10.f)), label_prosp(r.prosperite), hover_prosp());
    r.m_legitimite = mk_metric(metric_legitimacy(cp->L),                    label_legit(r.legitimite), hover_legit());
    r.m_cohesion   = mk_metric(metric_cohesion(cp->fracture),               label_concorde(r.concorde),hover_concorde());
    r.m_savoir     = mk_metric(metric_savoir(cp->Lumiere),                  label_savoir(r.savoir),    hover_savoir());
    r.influence    = 0;   /* posée par le statecraft */
    r.corruption   = faction_corruption_0_100(cid);   /* §C3 : le rot, en clair (0-100) */

    switch (cp->mode) {
        case RD_SUBMERGE_SECESS: r.augure = "Les marges parlent de se gouverner seules."; break;
        case RD_SUBMERGE_REVOL:  r.augure = "La rue gronde contre le trône.";            break;
        case RD_COERC_FRAGILE:   r.augure = "L'ordre tient — mais par la peur seule.";   break;
        default:                 r.augure = NULL;
    }
    return r;
}

/* ===================================================================== */
/* §9 — LA BALANCE DES FACTIONS-ÉTHOS (politique interne, mots + 0-100)    */
/* ===================================================================== */
FactionsReadout faction_readout(const World *w, const WorldEconomy *econ, int cid) {
    FactionsReadout fr; memset(&fr, 0, sizeof fr);
    float wt[FAC_COUNT];
    EthosFaction dom = faction_effective_distribution(w, econ, cid, wt);  /* base + leviers (§4) */
    fr.dominant = faction_name(dom);
    for (int f = 0; f < FAC_COUNT; f++) {
        float opp  = faction_opposition((EthosFaction)f, dom);   /* 0..1 idéologique */
        float grf  = faction_grievance(cid, (EthosFaction)f);    /* 0..1 politique (leviers) */
        fr.faction[f].name = faction_name((EthosFaction)f);
        fr.faction[f].part = iclamp((int)roundf(wt[f] * 100.f), 0, 100);
        /* SATISFACTION 0-100 : la direction CONTENTE qui pense comme elle ; l'opposition
         * idéologique et la rancune politique l'aliènent (vert → ambre → rouge). */
        fr.faction[f].satisfaction = iclamp((int)roundf((1.f - 0.65f*opp - 0.55f*grf) * 100.f), 0, 100);
        /* alignée = peu opposée à la direction ET peu aigrie par la politique. */
        fr.faction[f].aligned = (opp < 0.5f) && (grf < 0.30f);
    }
    EthosFaction alienated;
    float tension = faction_coup_tension_c(w, econ, cid, &alienated);
    fr.sedition = mk_metric(iclamp((int)roundf(tension * 200.f), 0, 100),  /* tension ~0..0.5 → 0..100 */
                            label_sedition(band_sedition(tension)), hover_sedition());
    return fr;
}

/* Climat : un MOT dérivé de la latitude et du biome (le biome porte l'aridité/
 * l'humidité ; la latitude porte le chaud/froid). Pas un nom qui imite un climat. */
static const char *climat_word(float lat, Biome b) {
    switch (b) {
        case BIO_DESERT: case BIO_COASTAL_DESERT: case BIO_DRYLANDS: return "Aride";
        case BIO_JUNGLE: case BIO_MANGROVE:                          return "Tropical";
        case BIO_SAVANNA:                                            return "Chaud";
        case BIO_GLACIER: case BIO_PEAK:                             return "Glacial";
        case BIO_MARSH: case BIO_BOG:                                return "Humide";
        default: break;
    }
    if (lat < 0.28f) return "Tropical";
    if (lat > 0.82f) return "Glacial";
    if (lat > 0.62f) return "Froid";
    return "Tempéré";
}
/* Relief : un MOT dérivé de l'altitude moyenne (0..1). */
static const char *relief_word(float height_avg, Biome b) {
    if (b==BIO_MOUNTAINS || b==BIO_PEAK || b==BIO_VOLCANO) return "Montagnes";
    if (b==BIO_HILLS)                                      return "Collines";
    if (b==BIO_HIGHLANDS)                                  return "Hauts plateaux";
    if (height_avg > 0.72f) return "Montagnes";
    if (height_avg > 0.55f) return "Hauts plateaux";
    if (height_avg > 0.42f) return "Collines";
    return "Plaines";
}

/* PRODUCTION — la QUANTITÉ produite par bien et par jour (unités/jour), via l'offre
 * réelle du dernier tick. PAS de prix : c'est l'income en ressource (ou en or si le
 * bien EST de l'or) ; la vente est une autre histoire. Le tick vaut un MOIS → /30. */
IncomeReadout province_income(const WorldEconomy *econ, int region) {
    IncomeReadout r; memset(&r, 0, sizeof r);
    if (!econ || region < 0 || region >= econ->n_regions) return r;
    const RegionEconomy *re = &econ->region[region];
    /* quantité/jour par bien produit (offre du dernier tick, sans le prix). */
    float qty[RES_COUNT];
    for (int i=0;i<RES_COUNT;i++) qty[i] = re->supply[i] / 30.f;
    /* sélection des 6 biens les plus produits. */
    bool taken[RES_COUNT]; memset(taken,0,sizeof taken);
    for (int k=0;k<6;k++){
        int best=-1; float bv=0.05f;                 /* seuil : ~1.5/mois, ignore les miettes */
        for (int i=1;i<RES_COUNT;i++){ if (taken[i]) continue; if (qty[i]>bv){ bv=qty[i]; best=i; } }
        if (best<0) break;
        taken[best]=true;
        r.line[r.n].source       = resource_name((Resource)best);
        r.line[r.n].per_day      = qty[best];         /* unités/jour (1 décimale à l'affichage) */
        r.line[r.n].manufactured = (best >= RES_PROD_FIRST);
        r.n++;
    }
    return r;
}

ProvinceReadout province_readout(const World *w, const WorldEconomy *econ,
                                 const WorldProsperity *wp, const WorldLegitimacy *wl,
                                 int pid) {
    ProvinceReadout pr; memset(&pr, 0, sizeof pr);
    if (pid < 0 || pid >= w->n_provinces) { pr.nom = "—"; pr.terrain = "—"; return pr; }
    const Province *p = &w->province[pid];
    int reg = p->region;

    pr.nom       = (reg >= 0 && w->region[reg].name[0]) ? w->region[reg].name : "—";
    pr.terrain   = biome_name(p->biome_dominant);
    pr.climat    = climat_word(p->lat, p->biome_dominant);
    pr.relief    = relief_word(p->height_avg, p->biome_dominant);
    pr.ressource = (p->resource > RES_NONE) ? resource_name(p->resource) : "—";

    const RegionEconomy *re = (reg >= 0 && reg < econ->n_regions) ? &econ->region[reg] : NULL;
    pr.race = re ? species_name(re->culture.race) : "—";
    float pop = 0.f;
    if (re) pop = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                + re->strata[CLASS_ELITE].pop;
    pr.ames = (long)pop;

    if      (pop <   50.f) pr.stature = STA_DESERT;
    else if (pop <  500.f) pr.stature = STA_HAMEAU;
    else if (pop < 2000.f) pr.stature = STA_BOURG;
    else if (pop < 6000.f) pr.stature = STA_CITE;
    else                   pr.stature = STA_METROPOLE;

    float sat = re ? re->satisfaction : 0.5f;
    if      (sat < 0.30f) pr.aisance = AI_MISERE;
    else if (sat < 0.55f) pr.aisance = AI_SUFFISANCE;
    else if (sat < 0.80f) pr.aisance = AI_AISANCE;
    else                  pr.aisance = AI_FASTE;
    /* PROSPÉRITÉ 0-100 (la jauge de l'en-tête) : l'indice local 0..10 projeté. */
    pr.m_aisance = mk_metric(metric_prosperity(re ? re->prosperity : 5.f),
                             label_aisance(pr.aisance), hover_aisance());

    /* Flux : proxy (la migration crée de la diaspora à destination → afflux).
     * La vraie balance migratoire par province viendra avec la prospérité locale. */
    float dia = re ? re->diaspora_pop : 0.f;
    pr.flux     = (dia > 50.f) ? FX_RUEE : (dia > 5.f) ? FX_AFFLUX : FX_STABLE;
    pr.diaspora = (dia > 0.5f);

    pr.vocation  = vocation_word(p->resource, p->coastal, p->biome_dominant);
    /* Carrefour : concentration de PE. L'infrastructure marchande BÂTIE
     * (Marché/Entrepôt) + la prospérité locale font le pôle ; la surchauffe du
     * pays le déchire (le seuil de déréalisation). */
    {
        float hub = re ? (re->build.PE_infra + re->route_pe
                          + rclampf(re->prosperity*2.f, 0.f, 3.f)) : 0.f;
        int   cc  = w->province[pid].country;
        bool  overheat = (cc>=0 && cc<wp->n_countries && wp->country[cc].surchauffe > 2.f);
        if      (hub < 1.0f) pr.carrefour = CF_NONE;
        else if (overheat)   pr.carrefour = CF_SURCHAUFFE;
        else if (hub < 2.5f) pr.carrefour = CF_FLORISSANTE;
        else                 pr.carrefour = CF_BOUILLONNANTE;
    }

    /* Allégeance — les lectures les plus proches du SCPS. */
    float L_local = (wl && reg >= 0 && reg < SCPS_MAX_REG) ? wl->L[reg] : 5.f;
    pr.humeur = band_humeur(L_local);
    pr.m_humeur = mk_metric(metric_legitimacy(L_local), label_humeur(pr.humeur), hover_humeur());

    int cid = re ? re->owner : -1;
    const PopCulture *ruling = (cid >= 0) ? pc_ruling(w, econ, cid) : NULL;
    if (re && ruling) {
        const PopCulture *rc = &re->culture;
        float clock   = rc->langue - ruling->langue; if (clock < 0) clock = -clock;
        float content = pc_content_dist(rc, ruling);
        bool same_branch  = (rc->rel_branch == ruling->rel_branch);
        bool both_zealous = (rc->credo != CREDO_PLURALISTE && ruling->credo != CREDO_PLURALISTE);
        float dr = rc->religion - ruling->religion; if (dr < 0) dr = -dr;
        bool schism = same_branch && both_zealous && dr < 4.f;
        pr.lignee = band_lignee(clock, content, schism);
        pr.foi    = band_foi(same_branch, dr, schism, rc->credo != CREDO_PLURALISTE);
    } else {
        pr.lignee = LI_MEME_SANG;
        pr.foi    = FOI_TIEDE;
    }

    /* Agitation (0-100) : L bas + coercition + tension de diversité (lignée
     * étrangère) + choc récent (conquête, coercition), ABATTUE par la stabilité
     * du pays et la garnison (H bâti) — révolte au-dessus du seuil. C'est l'effet
     * EXISTANT de L/H sur l'ordre, surfacé en un nombre lisible. */
    float div_tension = (re && ruling) ? pc_content_dist(&re->culture, ruling) : 0.f;
    float garrison    = re ? re->build.H_coerc : 0.f;
    float coercion    = re ? re->coercion : 0.f;
    float yh          = (wl && reg >= 0 && reg < SCPS_MAX_REG) ? wl->years_held[reg] : 50.f;
    float recent_shock= (yh < 5.f) ? (1.f - yh/5.f) : 0.f;
    if (coercion > recent_shock) recent_shock = coercion;
    int   country_stab= (cid >= 0 && cid < wp->n_countries)
                        ? metric_stability(wp->country[cid].SI, 0.f) : 50;
    int   agit = metric_agitation(L_local, coercion, div_tension, recent_shock,
                                  country_stab, garrison);
    pr.agitation     = mk_metric(agit, label_agitation(band_agitation(agit)), hover_agitation());
    pr.seuil_revolte = revolt_threshold_reached(agit);

    /* ── BÂTIMENTS — capacité CONSOMMÉE par la population : chaque âme occupe
     * 1 logement et 1 service. On affiche les places ENCORE LIBRES (capacité − pop),
     * pas un score abstrait. Plus deux SLOTS RÉSERVÉS lus de l'état bâti. ──────── */
    {
        float food_cap = re ? re->build.food_cap : 0.f;
        float K_inst   = re ? re->build.K_inst   : 0.f;
        float savoir   = re ? re->build.savoir   : 0.f;
        float faith    = re ? re->build.faith    : 0.f;
        float cap_pop  = re ? re->cap_pop        : 0.f;
        float H        = re ? re->build.H_coerc  : 0.f;
        /* LOGEMENTS : capacité d'accueil (porteuse du site + greniers/aqueducs bâtis) ;
         * chaque âme occupe UN logement → places libres = capacité − population. */
        long house_cap = (long)(cap_pop + food_cap*250.f);
        pr.logements_cap    = house_cap;
        pr.logements_libres = house_cap - (long)pop;
        /* SERVICES : chaque point d'édifice civique (admin/savoir/foi) sert ~700 âmes ;
         * chaque âme consomme UN service → places libres = capacité − population. */
        long serv_cap = (long)((K_inst + savoir + faith) * 700.f);
        pr.services_cap    = serv_cap;
        pr.services_libres = serv_cap - (long)pop;
        /* SLOT DÉFENSE : la fortification bâtie (la coercition BÂTIE H). */
        pr.defense = (H < 0.5f) ? "aucune" : (H < 1.5f) ? "Palissade"
                   : (H < 3.5f) ? "Remparts" : "Citadelle";
        pr.defense_hover = "Slot DÉFENSE : la fortification bâtie — palissade → remparts → citadelle (tient la province, monte la garnison).";
        /* SLOT SPÉCIALISATION : le métier de production du site (mine, pêcheries,
         * comptoir, atelier…) — lu de la ressource/géo, comme la vocation. */
        pr.specialisation = pr.vocation;
        pr.specialisation_hover = "Slot SPÉCIALISATION : ce que la province exploite ou raffine le mieux (mine, pêcheries, comptoir, atelier…).";
    }
    return pr;
}

/* ===================================================================== */
/* ARBRE DE TECH — la membrane de l'arbre concentrique                     */
/* ===================================================================== */
const char *label_tree_state(TreeState s){
    switch(s){ case TREE_DONE: return "acquis"; case TREE_OPEN: return "disponible"; default: return "verrouillé"; }
}
/* L'UTILITÉ CONCRÈTE de chaque bâtiment, en mots de jeu — ce qu'il PERMET ou
 * FOURNIT (production d'une ressource, logements, services, recrutement, défense,
 * recherche…), pas une coordonnée SCPS. Le faustien annonce son revers ÉVIDENT :
 * il rapproche la Brèche. Indexé par TechId (robuste au réordonnancement). */
static const char *const TECH_UTILITY[TECH_COUNT] = {
    /* Savoir · Production — la recherche */
    [TECH_BIBLIOTHEQUE]      = "+recherche (le socle du savoir)",
    [TECH_SCRIPTORIUM]       = "+recherche",
    [TECH_ACADEMIE]          = "+recherche (vitesse accrue)",
    [TECH_UNIVERSITE]        = "+recherche (vitesse maximale)",
    /* Savoir · Armée — la magie de guerre */
    [TECH_SAVOIR_GUERRE]     = "+armée (doctrine de guerre)",
    [TECH_MAGIE_BATAILLE]    = "+armée (mages de combat)",
    [TECH_INVOCATION]        = "armée invoquée, sans population ⚠ rapproche la Brèche",
    [TECH_EVEIL]             = "⚠ armée invoquée massive — déclenche la crise de fin",
    /* Savoir · Renforcement — l'arcane protectrice */
    [TECH_WARDS]             = "+défense (protections runiques)",
    [TECH_SCRYING]           = "+stabilité (vision lointaine)",
    [TECH_COMMUNION]         = "+cohésion (l'harmonie des peuples)",
    [TECH_SAVOIR_INTERDIT]   = "⚠ grand pouvoir interdit — rapproche la Brèche",
    /* Forge · Production — extraction & rendement */
    [TECH_COLLECTE_BOIS]     = "permet la production de bois",
    [TECH_COLLECTE_ARGILE]   = "permet la production d'argile",
    [TECH_FONDERIE]          = "permet la production de métal (fer, acier)",
    [TECH_OUTILLAGE]         = "+production (multiplicateur de rendement)",
    [TECH_MANUFACTURE]       = "+production (biens manufacturés)",
    [TECH_INDUSTRIE]         = "+production de masse",
    /* Forge · Armée — l'armement */
    [TECH_ARMURERIE]         = "permet la production d'armes",
    [TECH_POUDRIERE]         = "permet la production de poudre à canon",
    [TECH_FORGE_RUNES]       = "permet les armes enchantées ⚠ rapproche la Brèche",
    [TECH_OEUVRE_NOIRE]      = "⚠ armes terribles — rapproche la Brèche",
    /* Forge · Renforcement — construction & défense */
    [TECH_ATELIER]           = "permet de bâtir (chantiers de construction)",
    [TECH_QUALITE_MATERIAUX] = "+durabilité des bâtiments (béton → marbre)",
    [TECH_FORTIFICATIONS]    = "+défense (forteresse → citadelle)",
    [TECH_AUTOMATES]         = "+défense & production (golems) ⚠ rapproche la Brèche",
    /* Société · Production — vivres, commerce, impôt */
    [TECH_COLLECTE_NOURRITURE]= "permet la production de nourriture",
    [TECH_IRRIGATION]        = "+nourriture & +logements (greniers)",
    [TECH_COMMERCE]          = "+or (commerce, marché, banque)",
    [TECH_CADASTRE]          = "+or (l'impôt)",
    [TECH_ABONDANCE]         = "+croissance & +or (l'abondance halfeline)",
    /* Société · Armée — la levée */
    [TECH_CASERNE]           = "permet de recruter de l'infanterie",
    [TECH_CONSCRIPTION]      = "+armée (la levée en masse)",
    [TECH_ORGANISATION]      = "+armée (organisation militaire)",
    [TECH_ESCLAVAGE]         = "main-d'œuvre & armées serviles ⚠ fracture interne",
    [TECH_CASTE_MARTIALE]    = "⚠ +armée (caste guerrière) — rapproche la Brèche",
    /* Société · Renforcement — services, foi, intégration */
    [TECH_CHANCELLERIE]      = "+services (administration) & +stabilité",
    [TECH_FOI]               = "+légitimité & services religieux (temple → cathédrale)",
    [TECH_INTEGRATION]       = "+cohésion (l'assimilation des peuples)",
    [TECH_CULTE_IMPERIAL]    = "⚠ +cohésion forcée — rapproche la Brèche",
};
void tech_tree_readout(const TechState *ts, unsigned race_access, float population,
                       TechTreeReadout *out){
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->n = TECH_COUNT;
    out->points = ts ? (int)(ts->research_points + 0.5f) : 0;
    out->n_themes = THM_COUNT; out->n_functions = FN_COUNT;
    for (int t=0;t<THM_COUNT && t<3;t++) out->theme[t]    = tech_theme_name((TechTheme)t);
    for (int f=0;f<FN_COUNT  && f<3;f++) out->function[f] = tech_function_name((TechFunction)f);
    for (int i=0;i<TECH_COUNT;i++){
        const TechNode *n = tech_node((TechId)i);
        TreeNodeReadout *nr = &out->node[i];
        nr->quarter  = tech_quarter(n->theme, n->func);   /* l'ANGLE */
        nr->tier     = n->tier;                            /* le RAYON */
        nr->faustian = n->faustian;
        nr->is_base  = tech_is_base((TechId)i);
        nr->name     = n->name;
        nr->unlocks  = n->unlocks;
        nr->effet    = TECH_UTILITY[i] ? TECH_UTILITY[i] : n->unlocks;   /* l'utilité concrète */
        nr->cost     = (int)(tech_cost((TechId)i, population) + 0.5f);
        bool done = ts && ts->unlocked[i];
        bool open = ts && tech_can_research(ts, (TechId)i, race_access);
        nr->state    = done ? TREE_DONE : (open ? TREE_OPEN : TREE_LOCKED);
        /* orphelin = signature d'une AUTRE race dont l'empire n'a pas l'accès. */
        nr->orphan   = (n->native!=RACE_COUNT) && !(race_access & tech_race_bit(n->native));
    }
}
