#ifndef SCPS_READOUT_H
#define SCPS_READOUT_H
/*
 * scps_readout.h — LA MEMBRANE : du flottant SCPS au MOT diégétique.
 *
 * Garantie structurelle (Partie 0 du cahier UI) : ce fichier — et lui seul —
 * traduit les flottants du moteur en bandes qualitatives + chaînes. Le
 * renderer (viewer.c, scps_render.c) n'inclut QUE cet en-tête : il ne voit
 * jamais scps_core.h ni un flottant SCPS. Il reçoit un Readout (enums +
 * chaînes) et appelle label_X / hover_X. Franchir la cloison est IMPOSSIBLE,
 * pas seulement déconseillé.
 *
 * → scps_core.h est inclus dans scps_readout.c SEULEMENT, jamais ici.
 * → Les seuils opèrent sur des flottants NUS (pas de type scps_core exposé).
 *
 * Distinction tenue : les quantités tangibles (population, année, or) peuvent
 * s'afficher en chiffres — ce ne sont pas du SCPS. Les abstractions SCPS
 * (stabilité, légitimité, fracture, fragilité, prospérité) : JAMAIS un chiffre.
 */
#include <stdbool.h>
/* Types de la SIM (pas scps_core) : le renderer peut tenir les pointeurs, mais
 * la règle « ne lis jamais un flottant SCPS » reste vérifiée par grep (#1).
 * scps_prosperity.h n'inclut PAS scps_core.h → la cloison tient. */
#include "scps_world.h"
#include "scps_prosperity.h"   /* WorldProsperity, WorldLegitimacy, WorldEconomy */

/* ===================================================================== */
/* BANDES QUALITATIVES — jamais un nombre                                 */
/* ===================================================================== */
/* Bandeau du royaume */
typedef enum { ST_SUBMERGE, ST_VACILLANT, ST_TENU, ST_ASSURE, ST_INEBRANLABLE } BandStab;
typedef enum { AS_CONSENTIE, AS_PARTAGEE, AS_CONTRAINTE, AS_TYRANNIQUE }          BandAssise;
typedef enum { LG_USURPEE, LG_CONTESTEE, LG_TOLEREE, LG_RECONNUE, LG_SACREE }     BandLegit;
typedef enum { CO_UNIE, CO_MURMURANTE, CO_FRACTUREE, CO_SECESSION }               BandConcorde;
typedef enum { PR_MISERE, PR_DISETTE, PR_SUFFISANCE, PR_AISANCE, PR_OPULENCE }    BandProsp;
typedef enum { SA_OBSCURITE, SA_LUEUR, SA_FOYER, SA_PHARE }                       BandSavoir;
typedef enum { PG_CALME, PG_FREMISSEMENT, PG_OMBRE, PG_SEUIL }                    BandPresage;
/* Panneau de province */
typedef enum { STA_DESERT, STA_HAMEAU, STA_BOURG, STA_CITE, STA_METROPOLE }       BandStature;
typedef enum { FX_EXODE, FX_SAIGNEE, FX_STABLE, FX_AFFLUX, FX_RUEE }              BandFlux;
typedef enum { AI_MISERE, AI_SUFFISANCE, AI_AISANCE, AI_FASTE }                   BandAisance;
typedef enum { CF_NONE, CF_FLORISSANTE, CF_BOUILLONNANTE, CF_SURCHAUFFE }         BandCarrefour;
typedef enum { HU_REVOLTEE, HU_FRONDEUSE, HU_TIEDE, HU_LOYALE, HU_DEVOUEE }       BandHumeur;
typedef enum { LI_MEME_SANG, LI_COUSINE, LI_SOEUR_LOINTAINE, LI_ETRANGERE,
               LI_HERETIQUE_PROCHE, LI_INASSIMILABLE }                            BandLignee;

/* ===================================================================== */
/* READOUTS — ce que le renderer reçoit (bandes + chaînes, AUCUN float)   */
/* ===================================================================== */
typedef struct {
    BandStab     stabilite;
    BandAssise   assise;        /* la SIGNATURE : sur quoi repose l'obéissance */
    BandLegit    legitimite;
    BandConcorde concorde;
    BandProsp    prosperite;
    BandSavoir   savoir;
    BandPresage  presage;       /* masqué si PG_CALME */
    const char  *augure;        /* ligne d'ambiance de péril, ou NULL */
} CountryReadout;

typedef struct {
    BandHumeur humeur;
    BandLignee lignee;
} AllegeanceReadout;

/* Panneau de province complet (ce que le renderer dessine). Chaînes + bandes,
 * jamais un flottant SCPS. `ames` est une quantité tangible : un nombre est OK. */
typedef struct {
    const char   *nom;
    const char   *terrain;     /* mot (biome nommé) */
    const char   *race;        /* mot (espèce de la population) */
    BandStature   stature;
    long          ames;        /* population — nombre tangible */
    BandFlux      flux;
    const char   *vocation;    /* mot (spécialisation) */
    const char   *ressource;   /* mot */
    BandAisance   aisance;
    BandCarrefour carrefour;   /* CF_NONE si pas un pôle */
    BandHumeur    humeur;
    BandLignee    lignee;
    bool          diaspora;
} ProvinceReadout;

/* ===================================================================== */
/* SEUILLAGE — flottants NUS → bandes (la membrane testable)              */
/* ===================================================================== */
/* Stabilité depuis SI, BORNÉE par la fragilité : un ordre tenu par la force
 * ne se lit jamais « Assurée/Inébranlable » (il « a l'air » tenu, pas plus).
 * C'est ce qui fait émerger la signature « Tenue · Contrainte ». */
BandStab     band_stab(float SI, float fragilite);
BandAssise   band_assise(float fragilite);
BandLegit    band_legit(float L);
BandConcorde band_concorde(float fracture, bool secession_mode);
BandProsp    band_prosp(float prosperity_0_10);
BandSavoir   band_savoir(float lumiere_0_10);
BandPresage  band_presage(float charge_0_10);
BandHumeur   band_humeur(float L_local);
/* Lignée : horloge (cousinage) ET contenu (friction), + schisme religieux. */
BandLignee   band_lignee(float clock_dist, float content_dist, bool religious_schism);

/* ===================================================================== */
/* ASSEMBLAGE — depuis flottants nus (testable sans la sim)               */
/* ===================================================================== */
/* La membrane proprement dite : tous les flottants SCPS entrent ICI, rien
 * n'en ressort que des bandes. `pression` sert à distinguer sécession (la
 * fracture domine) de révolution (la pression domine) pour l'augure. */
CountryReadout country_readout_from_floats(
    float SI, float fragilite, float fracture, float pression,
    float L, float prosperity_0_10, float lumiere_0_10, float charge_0_10);

AllegeanceReadout allegeance_from_floats(
    float L_local, float clock_dist, float content_dist, bool religious_schism);

/* ===================================================================== */
/* ENVELOPPES RENDERER — les SEULES fonctions que viewer.c/render.c appellent */
/* ===================================================================== */
/* Lisent les sorties STOCKÉES (prospérité §2.4 + légitimité) → bandes. Aucun
 * appel à scps_core ici : tout a déjà été calculé par prosperity_tick. */
CountryReadout  country_readout (const WorldProsperity *wp, const TechState *ts,
                                 const World *w, int cid);
ProvinceReadout province_readout(const World *w, const WorldEconomy *econ,
                                 const WorldProsperity *wp, const WorldLegitimacy *wl,
                                 int province_id);

/* ===================================================================== */
/* LEXIQUE — un mot (label) + une définition (hover) par bande            */
/* ===================================================================== */
/* label_X(band) → le MOT affiché. hover_X() → la définition d'une phrase
 * (le sens du concept, jamais sa valeur). Le renderer n'appelle que ça. */
const char *label_stab(BandStab b);        const char *hover_stab(void);
const char *label_assise(BandAssise b);    const char *hover_assise(void);
const char *label_legit(BandLegit b);      const char *hover_legit(void);
const char *label_concorde(BandConcorde b);const char *hover_concorde(void);
const char *label_prosp(BandProsp b);      const char *hover_prosp(void);
const char *label_savoir(BandSavoir b);    const char *hover_savoir(void);
const char *label_presage(BandPresage b);  const char *hover_presage(void);
const char *label_stature(BandStature b);  const char *hover_stature(void);
const char *label_flux(BandFlux b);        const char *hover_flux(void);
const char *label_aisance(BandAisance b);  const char *hover_aisance(void);
const char *label_carrefour(BandCarrefour b); const char *hover_carrefour(void);
const char *label_humeur(BandHumeur b);    const char *hover_humeur(void);
const char *label_lignee(BandLignee b);    const char *hover_lignee(void);

#endif /* SCPS_READOUT_H */
