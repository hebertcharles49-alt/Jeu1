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
#define LBL(fn, type, ...) \
    const char *fn(type b){ static const char *N[]={__VA_ARGS__}; \
        int n=(int)(sizeof(N)/sizeof(N[0])), ib=(int)b; \
        return (ib>=0&&ib<n)?N[ib]:"?"; }

LBL(label_stab,     BandStab,     "Submergée","Vacillante","Tenue","Assurée","Inébranlable")
LBL(label_assise,   BandAssise,   "Consentie","Partagée","Contrainte","Tyrannique")
LBL(label_legit,    BandLegit,    "Usurpée","Contestée","Tolérée","Reconnue","Sacrée")
LBL(label_concorde, BandConcorde, "Unie","Murmurante","Fracturée","Sécession")
LBL(label_prosp,    BandProsp,    "Misère","Disette","Suffisance","Aisance","Opulence")
LBL(label_savoir,   BandSavoir,   "Obscurité","Lueur","Foyer","Phare")
LBL(label_presage,  BandPresage,  "Calme","Frémissement","Ombre grandissante","Le seuil")
LBL(label_stature,  BandStature,  "Désert","Hameau","Bourg","Cité","Métropole")
LBL(label_flux,     BandFlux,     "Exode","Saignée","Stable","Afflux","Ruée")
LBL(label_aisance,  BandAisance,  "Misère","Suffisance","Aisance","Faste")
LBL(label_carrefour,BandCarrefour,"—","Florissante","Bouillonnante","En surchauffe")
LBL(label_humeur,   BandHumeur,   "Révoltée","Frondeuse","Tiède","Loyale","Dévouée")
LBL(label_lignee,   BandLignee,   "Du même sang","Cousine","Sœur lointaine","Étrangère",
                                  "Hérétique proche","Inassimilable")
LBL(label_agitation,BandAgitation,"Calme","Frémissante","Agitée","Insurgée")
LBL(label_foi,      BandFoi,      "Dévote","Tiède","Hérétique")
#undef LBL

/* ===================================================================== */
/* LEXIQUE — hovers (la DÉFINITION, jamais la valeur)                     */
/* ===================================================================== */
const char *hover_stab(void){ return
    "La solidité de l'ordre : un royaume assuré encaisse les chocs, un royaume vacillant cède au premier vent."; }
const char *hover_assise(void){ return
    "Sur quoi repose l'obéissance : l'adhésion des cœurs, ou le seul poids des armes."; }
const char *hover_legit(void){ return
    "Le droit reconnu au trône de régner ; sacrée, nul ne la conteste — usurpée, chacun guette la chute."; }
const char *hover_concorde(void){ return
    "L'unité des peuples sous une même couronne ; quand les coutures lâchent, les marges rêvent d'indépendance."; }
const char *hover_prosp(void){ return
    "La richesse qui circule et qu'on parvient à lever ; un royaume opulent rayonne, une disette le vide."; }
const char *hover_savoir(void){ return
    "Le savoir né aux carrefours des cultures ; il nourrit les arts et les arcanes."; }
const char *hover_presage(void){ return
    "Ce que la quête de puissance attire ; plus on force l'arcane, plus l'ombre s'épaissit."; }
const char *hover_stature(void){ return
    "L'ampleur de l'établissement humain, du hameau perdu à la cité grouillante."; }
const char *hover_flux(void){ return
    "Le mouvement des âmes : un afflux gonfle la province, un exode la vide."; }
const char *hover_aisance(void){ return
    "La richesse qui circule ici ; les carrefours prospèrent, les culs-de-sac s'étiolent."; }
const char *hover_carrefour(void){ return
    "Quand des cultures se croisent ici, la richesse afflue — jusqu'à ce que le flux déborde et que la ville-monde se déchire."; }
const char *hover_humeur(void){ return
    "Le cœur de la province envers la couronne ; loyale, elle paie sans broncher — frondeuse, elle attend l'étincelle."; }
const char *hover_lignee(void){ return
    "Ce qui la lie à la culture du trône ; le même sang se gouverne aisément, l'inassimilable jamais sans la force."; }
const char *hover_agitation(void){ return
    "La colère qui monte dans la province ; soutenue, elle vire à la révolte — qu'apaisent la stabilité du royaume, la garnison et la légitimité."; }
const char *hover_foi(void){ return
    "La ferveur de la province envers le culte du trône ; dévote, elle nourrit la légitimité sacrée — hérétique, elle couve le schisme."; }

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

    switch (cp->mode) {
        case RD_SUBMERGE_SECESS: r.augure = "Les marges parlent de se gouverner seules."; break;
        case RD_SUBMERGE_REVOL:  r.augure = "La rue gronde contre le trône.";            break;
        case RD_COERC_FRAGILE:   r.augure = "L'ordre tient — mais par la peur seule.";   break;
        default:                 r.augure = NULL;
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
