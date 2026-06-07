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

static inline float rclampf(float v, float lo, float hi) {
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
    (void)wp;
    if (pid < 0 || pid >= w->n_provinces) { pr.nom = "—"; pr.terrain = "—"; return pr; }
    const Province *p = &w->province[pid];
    int reg = p->region;

    pr.nom       = (reg >= 0 && w->region[reg].name[0]) ? w->region[reg].name : "—";
    pr.terrain   = biome_name(p->biome_dominant);
    pr.ressource = (p->resource > RES_NONE) ? resource_name(p->resource) : "—";

    const RegionEconomy *re = (reg >= 0 && reg < econ->n_regions) ? &econ->region[reg] : NULL;
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
    pr.carrefour = CF_NONE;   /* PE concentrée par province : Partie 4 (à venir) */

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
    } else {
        pr.lignee = LI_MEME_SANG;
    }
    return pr;
}
