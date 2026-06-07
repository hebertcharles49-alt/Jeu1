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
