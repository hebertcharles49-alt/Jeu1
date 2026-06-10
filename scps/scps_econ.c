/*
 * scps_econ.c — moteur de simulation économique (voir scps_econ.h)
 *
 * Modèle volontairement causal et lisible : chaque chiffre a une raison
 * géographique ou démographique. Aucune valeur n'est posée « au hasard » à
 * la simulation — l'aléa est dans la génération du monde, pas dans l'éco.
 */
#include "scps_econ.h"
#include "scps_world.h"   /* resource_name(), subsistance_for_biome() */
#include "scps_culture.h" /* culture_content_distance() pour la novelty diaspora */
#include "scps_labor.h"   /* capitale_* : la productivité de la capitale booste la prod réelle */
#include "scps_factions.h"/* §C3 : faction_capture_total → le « rot » qui mine l'efficacité noble */
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ====================================================================== */
/* TABLES DE CONSTANTES                                                    */
/* ====================================================================== */

/* Prix de base par bien (monnaie/unité). Brutes bon marché, manufacturés
 * chers, luxe très cher. Sert d'ancre au prix de marché. */
static const float BASE_PRICE[RES_COUNT] = {
    [RES_NONE]          = 0.f,
    /* brutes agricoles */
    [RES_GRAIN]         = 1.0f,
    [RES_LIVESTOCK]     = 1.6f,
    [RES_WOOL]          = 1.8f,
    [RES_FISH]          = 1.2f,
    [RES_FUR]           = 3.0f,
    [RES_SALT]          = 2.2f,
    [RES_COTTON]        = 1.9f,
    [RES_SUGAR]         = 2.0f,
    [RES_WOOD]          = 1.0f,
    [RES_MED_HERBS]     = 3.5f,
    /* brutes minérales */
    [RES_COPPER]        = 2.6f,
    [RES_IRON]          = 2.4f,
    [RES_COAL]          = 1.8f,
    [RES_SULFUR]        = 3.0f,
    [RES_SALTPETER]     = 3.2f,
    [RES_GOLD]          = 8.0f,
    [RES_PRECIOUS_METAL]= 12.0f,
    [RES_PEARL]         = 12.0f,   /* perle : prix habituel d'une ressource précieuse (≈ métal préc.) */
    [RES_MUREX]         = 11.0f,   /* teinture pourpre — rare comme une précieuse (gate du luxe-tissu) */
    [RES_INDIGO]        = 10.0f,   /* teinture bleue — bas-pays chaud (gate alternatif du luxe-tissu) */
    /* manufacturés */
    [RES_CLOTH]         = 4.5f,
    [RES_TUNIQUE]       = 5.5f,    /* vêtement fini du commun — étoffe + façon (un cran au-dessus du tissu) */
    [RES_NAVAL_SUPPLIES]= 4.0f,
    [RES_WINE]          = 5.0f,
    [RES_BEER]          = 3.0f,    /* la boisson du commun — moins chère que le vin */
    [RES_PRECIOUS_WARE] = 22.0f,
    [RES_PRECIOUS_CLOTH]= 18.0f,
    [RES_PAPER]         = 5.5f,
    [RES_ARCANE_CRYSTAL]= 16.0f,   /* résidu rare des nœuds telluriques */
    [RES_ESSENCE]       = 34.0f,   /* mana raffiné — très haute valeur */
    [RES_CELESTIAL_IRON]= 20.0f,   /* météorique — très rare */
    [RES_ENCHANTED_ARMS]= 46.0f,   /* armes enchantées — la Forge supérieure */
    [RES_METAL]         = 5.0f,    /* fonte/acier — intrant */
    [RES_TOOLS]         = 8.5f,    /* outils — le multiplicateur de productivité */
    [RES_ARMS]          = 9.0f,    /* armes & armures — militaire de base */
    [RES_GUNPOWDER]     = 11.0f,   /* poudre — militaire */
    [RES_REMEDE]        = 7.0f,    /* remèdes — santé/confort */
};

/* Recette d'une manufacture : jusqu'à 2 intrants → 1 produit. */
typedef struct {
    Resource in1;  float q1;
    Resource in2;  float q2;   /* in2 = RES_NONE si une seule entrée */
    Resource out;  float qout;
    float    labor;            /* besoin de main-d'œuvre par niveau */
    Resource alt1; float alt1_q;  /* intrant de REPLI pour in1, à SA PROPRE quantité
                                   * (perle pour l'or : 2× le métal par bijou). On puise
                                   * in1 d'abord, le repli ensuite. RES_NONE = aucun. */
} Recipe;

static const Recipe RECIPE[BLD_TYPE_COUNT] = {
    /* TEXTILE : intrant allégé (2.0→1.5) et sortie relevée (1.0→1.8) → la pénurie
     * d'étoffe (couv 22%) se résorbe ; la laine est mieux dispatchée (scps_world). */
    [BLD_TEXTILE]   = { RES_WOOL,  1.5f, RES_NONE,          0.f, RES_CLOTH,          2.8f, 1.0f },  /* rendement étoffe relevé (1.8→2.8) : l'étoffe nourrit DEUX chaînes (tunique + précieuse 1:4) — il en faut plus */
    [BLD_SAWMILL]   = { RES_WOOD,  2.0f, RES_NONE,          0.f, RES_NAVAL_SUPPLIES, 1.0f, 0.8f },
    [BLD_PAPERMILL] = { RES_WOOD,  1.5f, RES_NONE,          0.f, RES_PAPER,          1.0f, 0.7f },
    /* VIN : sucre allégé (2.0→1.6), sortie relevée (1.0→1.4) ; le sucre tropical est
     * mieux dispatché (scps_world) → la pénurie de vin (couv 25%) se résorbe. */
    [BLD_WINERY]    = { RES_SUGAR, 1.6f, RES_NONE,          0.f, RES_WINE,           1.4f, 0.9f },
    [BLD_BREWERY]   = { RES_GRAIN, 1.2f, RES_NONE,          0.f, RES_BEER,           1.0f, 0.8f },
    /* JOAILLERIE : OR, ou PERLE en repli (2× la quantité par bijou — littoral).
     * Sortie TEMPÉRÉE (1.0→0.5) et intrant plus lourd (1.5→2.0) : l'orfèvrerie
     * surinondait (couv ×170) → on vise un surplus DOUX, pas un raz-de-marée. */
    [BLD_JEWELER]   = { RES_GOLD,  0.8f, RES_NONE,          0.f, RES_PRECIOUS_WARE,  0.5f, 1.2f, RES_PEARL, 1.6f },  /* SURCADENCE : l'or/perle est un FILIGRANE — une once rend assez de bijoux pour servir une cour + dégager un surplus à exporter (ratio 2× perle conservé) */
    /* ÉTOFFE PRÉCIEUSE — désormais GATÉE PAR LA TEINTURE (murex côtier, ou indigo du
     * bas-pays chaud en repli), comme l'orfèvrerie l'est par l'or. Recette 1:4 :
     * 1 teinture + 4 ÉTOFFES → 1 précieuse. Le précieux est ainsi PLAFONNÉ par la
     * teinture (rare), PAS par l'étoffe → quand la teinture manque, l'étoffe REFLUE
     * vers les tuniques (les journaliers servis). in1=teinture, in2=4 étoffes. */
    [BLD_WEAVER_LUX]= { RES_MUREX, 0.1f, RES_CLOTH, 4.0f, RES_PRECIOUS_CLOTH, 1.0f, 1.1f, RES_INDIGO, 0.1f },  /* SURCADENCE : un bain de teinture colore beaucoup → l'étoffe précieuse suit la cour ; la teinture PLACE-gate, l'étoffe 1:4 (surplus après tunique) borne le volume */
    /* TUNIQUE — la chaîne SÉPARÉE des journaliers : étoffe → tunique (1:1). Bien fini
     * propre au commun → plus de prix-exclusion par le luxe sur le même tissu. */
    [BLD_TUNIC]     = { RES_CLOTH, 1.0f, RES_NONE,          0.f, RES_TUNIQUE,       1.0f, 0.8f },
    /* ARCANE : on BRÛLE le cristal pour raffiner l'essence (mana). Sa combustion
     * nourrit la Brèche (couplée plus bas dans econ_tick → arcane_charge). */
    [BLD_MAGE_WORKSHOP]={ RES_ARCANE_CRYSTAL, 1.0f, RES_NONE, 0.f, RES_ESSENCE,    1.0f, 1.3f },
    /* ARCANE militaire : le fer céleste + l'essence → armes enchantées (la Forge
     * supérieure). Consomme donc l'essence de l'atelier de mage (chaîne arcane). */
    [BLD_CELESTIAL_FORGE]={ RES_CELESTIAL_IRON, 1.0f, RES_ESSENCE, 1.0f, RES_ENCHANTED_ARMS, 1.0f, 1.4f },
    /* Épine dorsale de production : fer + charbon → métal → (métal + bois) outils. */
    [BLD_FOUNDRY]   = { RES_IRON,  1.5f, RES_COAL, 1.0f, RES_METAL, 1.0f, 1.0f },
    [BLD_TOOLWORKS] = { RES_METAL, 1.0f, RES_WOOD, 1.0f, RES_TOOLS, 1.0f, 0.9f },
    /* CHARBONNIÈRE : 2 bois → 1 charbon. Le charbon minier est rare et co-localisé
     * avec le fer (gate de la fonderie) ; la charbonnière le PRODUIT du bois (abondant)
     * → la fonderie tourne partout où il y a du fer, et la chaîne métal/outils respire. */
    [BLD_CHARCOAL]  = { RES_WOOD,  2.0f, RES_NONE, 0.f, RES_COAL,  1.0f, 0.8f },
    /* §B2 FOREUSE ARCANIQUE : transmute l'ESSENCE en FER en masse (0.5 essence → 8 fer).
     * L'issue faustienne à la famine de fer ; gatée par la tech (charge) + l'essence (rare). */
    [BLD_FOREUSE]   = { RES_ESSENCE, 0.5f, RES_NONE, 0.f, RES_IRON, 8.0f, 1.4f },
    /* Chaînes militaires de base + santé (compléter le roster de production). */
    [BLD_ARMORY]    = { RES_IRON,      1.2f, RES_NONE, 0.f, RES_ARMS,      1.0f, 1.0f },
    [BLD_POWDERMILL]= { RES_SALTPETER, 1.0f, RES_COAL, 0.8f, RES_GUNPOWDER, 1.0f, 1.0f },
    [BLD_APOTHECARY]= { RES_MED_HERBS, 1.0f, RES_NONE, 0.f, RES_REMEDE,    1.0f, 0.8f },
};

/* Besoins par tête et par strate (unités/100 hab/tick). Le grain (vivres)
 * est universel ; le reste monte en gamme avec la classe. */
/* Table REVISITÉE. La case RES_WINE = palier MORAL (servi bière/vin selon la
 * préférence) ; la case RES_PRECIOUS_WARE = palier STATUT (orfèvrerie/étoffe
 * précieuse). On allège l'étoffe (en pénurie) et le bois de feu ; tout le reste
 * tend de +10 % via DEMAND_TENSION appliqué à `units` (demande tendue permanente). */
static const float NEED[CLASS_COUNT][RES_COUNT] = {
    [CLASS_LABORER] = {
        [RES_GRAIN]=1.00f, [RES_WINE]=0.35f, [RES_FISH]=0.30f, [RES_WOOD]=0.35f, [RES_TUNIQUE]=0.40f, /* §moral : la BIÈRE (palier moral du commun, via préférence) est le levier — brassée du SURPLUS de grain (réserve vivrière protégée) ; calée pour ~60 % de satisfaction journalière */
    },
    [CLASS_BOURGEOIS] = {
        [RES_GRAIN]=1.00f, [RES_CLOTH]=0.34f, [RES_PAPER]=0.25f, [RES_WINE]=0.30f,
        [RES_SALT]=0.20f, [RES_REMEDE]=0.15f,   /* santé urbaine (apothicaire) */
    },
    [CLASS_ELITE] = {
        /* §panier — rééquilibré vers les paliers PRODUCTIBLES (le statut écrasait à 73 %).
         * Conforts relevés (fourrure/papier/vin, que l'éco SAIT fournir), STATUT abaissé
         * (orfèvrerie 0.90→0.55, le maillon rare). Combiné au déblocage progressif. */
        [RES_GRAIN]=1.00f, [RES_FUR]=0.12f, [RES_PAPER]=0.12f, [RES_WINE]=0.28f,
        [RES_PRECIOUS_WARE]=0.13f,   /* palier STATUT : servi en orfèvrerie OU étoffe ; débloqué EN DERNIER ; calé pour ~60 % */
        /* §panier — besoins confort/luxe encore allégés de 0.10 (le grain vital reste 1.0) :
         * l'élite se contente d'un peu moins → satisfaction relevée d'un cran de plus. */
    },
};
/* §besoins progressifs — ORDRE de priorité par classe (subsistance → confort → STATUT).
 * Le nombre de besoins COMPTÉS dans la satisfaction = f(niveau de capitale, ∝ pop) : un
 * petit centre n'aspire qu'aux bases (2 besoins), une grande capitale développée à tout
 * le panier (statut compris). Ainsi le luxe se MÉRITE avec le développement — et l'élite
 * d'un bourg n'est pas punie de ne pas avoir d'orfèvrerie. Le palier STATUT vient DERNIER. */
static const Resource NEED_ORDER[CLASS_COUNT][8] = {
    [CLASS_LABORER]   = { RES_GRAIN, RES_WINE, RES_FISH, RES_WOOD, RES_TUNIQUE, RES_NONE },  /* bière (RES_WINE→préférée) en palier moral PRÉCOCE : le commun veut sa chope */
    [CLASS_BOURGEOIS] = { RES_GRAIN, RES_SALT, RES_CLOTH, RES_REMEDE, RES_WINE, RES_PAPER, RES_NONE },
    [CLASS_ELITE]     = { RES_GRAIN, RES_FUR, RES_PAPER, RES_WINE, RES_PRECIOUS_WARE, RES_NONE },
};
/* rang de priorité d'un besoin (0 = vital) ; 99 = hors panier (jamais débloqué). */
static int need_rank(int c, Resource r){
    if (c<0||c>=CLASS_COUNT) return 99;
    for (int i=0;i<8 && NEED_ORDER[c][i]!=RES_NONE;i++) if (NEED_ORDER[c][i]==r) return i;
    return 99;
}

/* Part de chaque strate dans la population à l'initialisation. */
static const float CLASS_SHARE[CLASS_COUNT] = { 0.80f, 0.15f, 0.05f };

/* ---- Le palier MORAL est une VARIANTE culturelle (catalogue des biens) ----
 * Les cultures de basse subsistance (clans, montagnards nains, sauvages orques)
 * brassent la BIÈRE ; les cultures agraires/urbaines (cités, sylve elfique,
 * mercantile) pressent le VIN. Servir la MAUVAISE boisson ne contente qu'à
 * moitié (un nain boude le vin, un orque méprise le verre fin). */
#define DRINK_OFFCULT 0.5f
static inline Resource preferred_drink(const PopCulture *c){
    return (c->subsistance < 5.f) ? RES_BEER : RES_WINE;
}
/* Le palier STATUT (luxe d'élite) est lui aussi une variante : les cultures
 * martiales/pastorales (clans, nains, orques) prisent l'ORFÈVRERIE (torques,
 * runes, totems = bien OUVRÉ → precious_ware) ; les cultures établies/raffinées
 * (cités, sylve, mercantile) prisent l'ÉTOFFE PRÉCIEUSE (soie, fil-de-lune →
 * precious_cloth). Servir le mauvais luxe ne flatte qu'à moitié — l'élite
 * conquise reste sur sa faim (le terreau du coup d'État). */
#define LUXE_OFFCULT 0.5f
static inline Resource preferred_luxe(const PopCulture *c){
    return (c->subsistance < 5.f) ? RES_PRECIOUS_WARE : RES_PRECIOUS_CLOTH;
}

#define TAX_RATE     0.38f   /* RENTE d'élite — visée : équilibre de satisfaction ~60 % (re-dotée) */
#define WAGE_SHARE   0.42f   /* salaires (laborers) — abaissé pour la rente ; profit bourgeois = 0.20 */
/* le reste (1 - TAX - WAGE) = profit bourgeois (résidu 0.25 — reste sain) */
/* §NF — CONSTRUCTION PAR RÉTROACTION NÉGATIVE : un bien en pénurie appelle son
 * producteur, qu'on bâtit spontanément — mais JAMAIS dans le vide (pop + intrant). */
#define NF_SHORTAGE   1.8f   /* prix ≥ 1.8× base = pénurie qui justifie de bâtir le producteur */
#define NF_POP_FLOOR  80.f   /* pop régionale minimale : sous ce seuil, on ne bâtit pas (le vide) */
#define NF_STOCK_MIN  5.0f   /* stock d'intrant comptant comme « approvisionné » (extraction OU import) */
#define NF_SEED_LEVEL 1.0f   /* niveau de NAISSANCE du bâtiment (puis l'expansion §1 le fait croître) */
/* §gate — DEMANDE EFFECTIVE pour les biens DURABLES (orfèvrerie, outils). Ces biens
 * ne se consomment pas comme le pain : sans frein leur manufacture tourne au PLANCHER
 * du market_effort (0.42×) et GORGE le marché (prix planché 0.2×base — orfèvrerie 4.4,
 * outils 1.7 — intrants rares gaspillés, et le prix figé tue le signal de §NF/IA). */
#define GATE_DEMAND_BUFFER 1.25f   /* orfèvrerie : produire jusqu'à 1.25× la demande RÉELLE d'élite (marge croissance + surplus d'export) */
/* OUTILS — INPUT PASSIF de la main-d'œuvre : l'outil est un CAPITAL que les journaliers
 * USENT en travaillant (∝ leur nombre). Ce flux EST la demande effective (il tire le prix,
 * donc le §NF qui bâtit l'atelier et la perception IA) ET draine le stock (usure par
 * l'usage). L'outil n'entre PAS dans le panier : il ne touche QUE la productivité, JAMAIS
 * la satisfaction. Fini le prix planché à demande nulle qui rendait l'outillage inerte. */
#define TOOLS_PER_LABORER  0.15f   /* stock-outil VISÉ par journalier (palier d'équipement) → tools_pc ≈ 1.5 à plein, prod_mult ≈ +18 % ; la demande est le DÉFICIT vers ce palier (saturant), pas un flux pop-illimité */
/* §collecte — INTENSIFICATION : la récolte d'une tuile suit les BRAS qui l'occupent, pas
 * le seul terrain. raw_cap = RICHESSE de référence ; l'intensité ∝ √(pop/réf) (rendements
 * DÉCROISSANTS, plafonnés). Une région peuplée tire plus de sa terre → la production SUIT
 * la population, sans toucher les manufactures. EXTRACT_POP_REF : pop active donnant une
 * intensité de 1 (réglage du seuil). EXTRACT_INTENS_CAP : plafond physique de la tuile. */
#define EXTRACT_POP_REF    300.f
#define EXTRACT_INTENS_CAP 2.5f
#define TECH_RATE    0.010f  /* conversion richesse élite → tech */
#define PRICE_INERTIA 0.65f  /* lissage du prix (0=instantané,1=figé) */
#define EPS          1e-4f

/* Démographie calibrée : doublement en ~30 ans à food_sat=1, society_sat=0.5
 *   net = BIRTH_RATE*food_sat - DEATH_RATE + SOCIETY_BONUS*society_sat
 *   typique : 0.034 - 0.015 + 0.004 = 0.023 → ln(2)/0.023 ≈ 30 ticks */
#define BIRTH_RATE    0.034f
#define DEATH_RATE    0.015f
#define SOCIETY_BONUS 0.008f

/* Colonisation */
#define COLONY_MIN_POP      500.f   /* pop minimale d'une région pour essaimer  */
#define COLONY_COST_POP     250.f   /* colons détachés (quittent la mère)       */
#define COLONY_SEED_POP     100.f   /* pop installée dans la nouvelle région    */
#define COLONY_FOOD_GATE    0.35f   /* seuil de subsistance pour essaimer        */

/* Migration interne */
#define MIGRATE_RATE        0.02f   /* fraction max de bourgeois/élites migrant/tick */
#define MIGRATE_THRESHOLD   1.30f   /* différentiel de prospérité déclencheur        */
#define DIASPORA_TECH_RATE  0.0008f /* tech/tick par unité de diaspora               */
#define DIASPORA_DECAY      0.98f   /* acculturation : diaspora s'absorbe (~50 ticks) */

/* Relocalisation forcée */
#define RELOC_COERCION_BASE 0.25f   /* pic de coercition de base par évènement       */
#define COERCION_DECAY      0.93f   /* demi-vie ≈ 10 ticks                            */

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}

/* TENSION DE DEMANDE : +10 % de besoins partout (appliqué au facteur `units`) → une
 * demande tendue en permanence, le marché presse toujours sur l'offre. */
#define DEMAND_TENSION 1.10f

/* SURPLUS NATUREL : l'effort de production SUIT le prix (on « lit le marché »).
 *   prix/base = 1 (équilibre) → 1.10  (+10 % : un mince surplus s'installe)
 *   bien au plancher (glut)   → ~0.30 (on lève moins, la main-d'œuvre file ailleurs)
 *   bien cher (pénurie)       → 1.50  (on force la production)
 * Appliqué à l'extraction (out & main-d'œuvre) et à la manufacture (cap = niveau·eff). */
static inline float market_effort(float price, float base){
    if (base<=0.f) return 1.f;
    return clampf(0.25f + 0.85f*(price/base), 0.22f, 1.5f);
}

/* §4 (catalogue des biens) — DEMANDE par VARIANTE CULTURELLE. Les biens d'un
 * peuple ne sont pas d'autres biens : ce sont les variantes d'un même palier.
 * Une minorité d'une autre SPHÈRE réclame SES variantes (un orque méprise le
 * verre fin, un nain boude le vin) ; lui servir celles du dominant la satisfait
 * MAL. L'ASSIMILATION (integration↑, via le refactor démographique) fait DÉRIVER
 * sa demande vers la dominante → la pénalité s'efface sur les générations.
 * Renvoie la fraction de pop « mal servie » [0..1] (0 si province homogène). */
/* Recette d'un bâtiment (intrants → extrant) — exposée pour la perception IA
 * (détecter un raffineur qui tourne à vide faute d'intrant). */
void building_recipe(BuildingType b, Resource *in1, Resource *in2, Resource *out){
    if (b<0 || b>=BLD_TYPE_COUNT){ if(in1)*in1=RES_NONE; if(in2)*in2=RES_NONE; if(out)*out=RES_NONE; return; }
    if (in1) *in1=RECIPE[b].in1;
    if (in2) *in2=RECIPE[b].in2;
    if (out) *out=RECIPE[b].out;
}

float econ_off_culture_fraction(const ProvincePop *pp){
    if (!pp || pp->n_groups<=1) return 0.f;
    int dom=-1; long best=-1;
    for (int i=0;i<pp->n_groups;i++) if (pp->groups[i].count>best){ best=pp->groups[i].count; dom=i; }
    if (dom<0) return 0.f;
    Sphere doms = pp->groups[dom].origin_sphere;
    long total=0; float off=0.f;
    for (int i=0;i<pp->n_groups;i++){
        total += pp->groups[i].count;
        float sd   = sphere_distance(doms, pp->groups[i].origin_sphere)/7.f; /* normalisé 0..1 */
        float mism = sd * (1.f - clampf(pp->groups[i].integration,0.f,1.f)); /* l'assimilation efface */
        off += mism * (float)pp->groups[i].count;
    }
    return (total>0)? off/(float)total : 0.f;
}

const char *social_class_name(SocialClass c) {
    static const char *N[CLASS_COUNT]={"Laborers","Bourgeois","Élites"};
    return (c>=0&&c<CLASS_COUNT)?N[c]:"?";
}
const char *building_name(BuildingType b) {
    static const char *N[BLD_TYPE_COUNT]={
        [BLD_TEXTILE]="Manufacture textile",[BLD_SAWMILL]="Scierie navale",[BLD_PAPERMILL]="Papeterie",
        [BLD_WINERY]="Domaine viticole",[BLD_BREWERY]="Brasserie",[BLD_JEWELER]="Joaillerie",
        [BLD_WEAVER_LUX]="Atelier d'étoffe précieuse",[BLD_MAGE_WORKSHOP]="Atelier de mage",
        [BLD_CELESTIAL_FORGE]="Forge céleste",[BLD_FOUNDRY]="Haut-fourneau",[BLD_TOOLWORKS]="Atelier d'outillage",
        [BLD_ARMORY]="Armurerie",[BLD_POWDERMILL]="Poudrière",[BLD_APOTHECARY]="Apothicaire",
        [BLD_TUNIC]="Atelier de tunique",[BLD_CHARCOAL]="Charbonnière",[BLD_FOREUSE]="Foreuse arcanique",
    };
    return (b>=0&&b<BLD_TYPE_COUNT&&N[b])?N[b]:"?";
}

/* ====================================================================== */
/* INITIALISATION                                                         */
/* ====================================================================== */

/* Ajoute une manufacture de type t si absente, et renvoie son index. */
static int region_ensure_building(RegionEconomy *re, BuildingType t) {
    for (int i=0;i<re->n_bld;i++) if (re->bld[i].type==t) return i;
    if (re->n_bld>=ECON_MAX_BLD) return -1;
    int i=re->n_bld++;
    re->bld[i].type=t; re->bld[i].level=0.f; re->bld[i].workers=0.f;
    return i;
}

/* Injecte une population répartie en strates dans une région (peuplement
 * initial ou arrivée de colons). N'écrase pas les manufactures/prix. */
static void econ_seed_population(RegionEconomy *re, float total_pop) {
    for (int c=0;c<CLASS_COUNT;c++) {
        re->strata[c].pop         = total_pop*CLASS_SHARE[c];
        re->strata[c].wealth      = re->strata[c].pop * (c==CLASS_ELITE?6.f:c==CLASS_BOURGEOIS?2.f:0.5f);
        re->strata[c].satisfaction= 0.5f;
    }
}

void econ_init(WorldEconomy *e, const World *w) {
    memset(e,0,sizeof(*e));
    e->n_regions=w->n_regions;
    e->tick=0;

    /* ---- Passe 1 : capacité et habitabilité de chaque région ------------- *
     * reg_hab = habitabilité moyenne pondérée par la surface (province).
     * reg_cap = capacité brute, MULTIPLIÉE par reg_hab → les zones glaciaires
     * et les déserts hyperarides ont cap_pop ≈ 0, reflétant la réalité.     */
    float reg_cap[SCPS_MAX_REG]={0};
    float reg_hab[SCPS_MAX_REG]={0};
    float cty_cap[SCPS_MAX_COUNTRY]={0};
    for (int rid=0; rid<w->n_regions; rid++) {
        const Region *rg=&w->region[rid];
        float cap=0.f, area=0.f, hab_w=0.f;
        for (int k=0;k<rg->n_provinces;k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            const Province *pv=&w->province[pid];
            float a = (float)pv->area;
            /* Intensité agricole du biome (même source de vérité que l'axe
             * subsistance culturel) → capacité d'accueil brute. */
            float subs = subsistance_for_biome(pv->biome_dominant);
            cap  += a * (0.25f + 0.75f*clampf(subs/10.f,0.f,1.f));
            hab_w += pv->habitability * a;
            area += a;
        }
        if (area<1.f) continue;
        float hab = hab_w / area;   /* habitabilité pondérée par surface */
        reg_hab[rid] = hab;
        reg_cap[rid] = cap * hab;   /* la capacité est nulle pour les zones mortes */
        int cid=rg->country;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY) cty_cap[cid]+=reg_cap[rid];
    }

    /* ---- Passe 2 : cible par pays (4000 majeur ≥4 régions, 2000 satellite) */
    float cty_target[SCPS_MAX_COUNTRY]={0};
    int   cty_nreg  [SCPS_MAX_COUNTRY]={0};
    for (int rid=0; rid<w->n_regions; rid++) {
        int cid=w->region[rid].country;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY) cty_nreg[cid]++;
    }
    for (int cid=0; cid<SCPS_MAX_COUNTRY; cid++)
        if (cty_cap[cid]>0.f)
            cty_target[cid] = (cty_nreg[cid]>=4) ? 4000.f : 2000.f;

    /* ---- Passe 3 : peuplement de chaque région --------------------------- */
    for (int rid=0; rid<w->n_regions; rid++) {
        RegionEconomy *re=&e->region[rid];
        const Region *rg=&w->region[rid];

        float area_sum=0.f;
        for (int k=0;k<rg->n_provinces;k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            area_sum += w->province[pid].area;
        }
        /* Zones mortes / infranchissables : glacier, pic, volcan, désert hyperaride.
         * Deux critères combinés :
         *  a) fraction de la surface à habitabilité nulle (hab_base=0 : GLACIER/PEAK/VOLCANO) ≥ 35%
         *  b) habitabilité moyenne de la région < 12%
         * Le critère (a) détecte les barrières même quand une vallée habitable
         * dilue la moyenne. Le critère (b) attrape les déserts hyperarides sans pic. */
        float dead_area=0.f;
        for (int k=0;k<rg->n_provinces;k++){
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            if (w->province[pid].habitability < 0.01f)
                dead_area += (float)w->province[pid].area;
        }
        bool mostly_dead = (area_sum>0.f && dead_area/area_sum >= 0.35f);
        bool very_low    = (reg_hab[rid] < 0.12f);
        bool is_impass   = mostly_dead || very_low;

        re->habitability = reg_hab[rid];
        if (area_sum<1.f || reg_cap[rid]<=0.f || is_impass) {
            re->active     = false;
            re->impassable = is_impass;
            re->colonized  = false;
            re->owner      = -1;
            continue;
        }
        re->active=true;
        re->impassable=false;
        re->colonized=false;
        re->owner=-1;

        /* Capacité d'accueil : pop cible à terme (sert au peuplement initial
         * de la capitale et de plafond souple à la croissance). */
        int cid=rg->country;
        float total_pop;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY && cty_cap[cid]>0.f)
            total_pop = cty_target[cid] * reg_cap[rid] / cty_cap[cid];
        else
            total_pop = 40.f + reg_cap[rid]*12.f;
        re->cap_pop = total_pop;

        /* Population : laissée à ZÉRO par défaut. Le monde démarre vide ;
         * seules la capitale du joueur et quelques cités-états seront
         * peuplées (voir plus bas). Tout le reste est colonisable. */
        for (int c=0;c<CLASS_COUNT;c++) {
            re->strata[c].pop         = 0.f;
            re->strata[c].wealth      = 0.f;
            re->strata[c].satisfaction= 0.5f;
        }
        re->food_sat=0.5f; re->society_sat=0.5f;

        /* ---- Capacité d'extraction : héritée des ressources brutes des
         *      provinces. Chaque province « pose » sa ressource dominante. */
        bool coastal=false;
        for (int k=0;k<rg->n_provinces;k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            const Province *pv=&w->province[pid];
            if (pv->coastal) coastal=true;
            /* débit proportionnel à la surface de la province (brutes seules) */
            float base = 1.5f + pv->area*0.05f;
            Resource r=pv->resource;
            if (r>RES_NONE && r<RES_PROD_FIRST) re->raw_cap[r] += base;
            Resource r2=pv->resource2;                      /* §6b : 2e brute, mineure ×0.4 */
            if (r2>RES_NONE && r2<RES_PROD_FIRST) re->raw_cap[r2] += base*0.4f;
        }

        /* Subsistance locale : vivres et bois de feu dimensionnés pour couvrir
         * ~90% de la population, laissant la satisfaction refléter les biens
         * supérieurs et non une famine universelle. */
        float subsist = total_pop / 100.f;
        /* Le socle vivrier DOIT dépasser la consommation (≈1.0/100/tête, toutes
         * classes) — sinon le monde meurt de faim. On le porte au-dessus du seuil,
         * pondéré par la FERTILITÉ moyenne de la région (les bonnes terres
         * nourrissent plus). */
        re->raw_cap[RES_GRAIN] += subsist * (1.15f + 0.70f*reg_hab[rid]);
        re->raw_cap[RES_WOOD]  += subsist * 0.44f;   /* §6a : socle bois +10 % (intrant + chauffe) */
        re->coastal = coastal;                       /* lu par la marine (rade) et l'agency (gate du Port) */
        re->estuary = false;                         /* posé au balayage des cellules ci-dessous */
        if (coastal) re->raw_cap[RES_FISH] += subsist * 0.10f;   /* socle côtier minime : le poisson vient surtout des biomes halieutiques (§2) */

        /* ARCANE — le cristal sourd des NŒUDS telluriques : TRÈS rare, lié aux
         * failles profondes/volcaniques (proxy : présence de soufre ou de métal
         * précieux). Seule une fraction des régions concernées porte un nœud. */
        if ((re->raw_cap[RES_SULFUR]>0.f || re->raw_cap[RES_PRECIOUS_METAL]>0.f)
            && ((uint32_t)(rid*2654435761u) % 4u)==0u)
            re->raw_cap[RES_ARCANE_CRYSTAL] += 1.0f;
        /* Fer céleste — météorique : ENCORE plus rare, lié aux sommets/cratères
         * (proxy : minerai de fer en relief), ~1 région concernée sur 9. */
        if (re->raw_cap[RES_IRON]>0.f && ((uint32_t)(rid*40503u+7u) % 9u)==0u)
            re->raw_cap[RES_CELESTIAL_IRON] += 0.8f;

        /* ---- Manufactures : implantées là où l'intrant est extrait dans
         *      la région (cohérence géographique de la chaîne de prod). */
        if (re->raw_cap[RES_WOOL] > 0.f){ region_ensure_building(re,BLD_TEXTILE);
                                          region_ensure_building(re,BLD_TUNIC); }  /* la tunique naît où l'on file */
        if (re->raw_cap[RES_WOOD] > 0.f) {
            region_ensure_building(re,BLD_SAWMILL);
            region_ensure_building(re,BLD_PAPERMILL);
            region_ensure_building(re,BLD_CHARCOAL);   /* charbon DU BOIS : la fonderie n'est plus otage du charbon minier */
        }
        if (re->raw_cap[RES_SUGAR] > 0.f) region_ensure_building(re,BLD_WINERY);
        /* Brasserie : la bière naît du grain — boisson du commun, partout où l'on cultive. */
        if (re->raw_cap[RES_GRAIN] > 0.f) region_ensure_building(re,BLD_BREWERY);
        /* Joaillerie : là où l'on extrait de l'OR ou des PERLES (littoral). */
        if (re->raw_cap[RES_GOLD] > 0.f || re->raw_cap[RES_PEARL] > 0.f)
            region_ensure_building(re,BLD_JEWELER);
        /* L'atelier de luxe a besoin de tissu : présent là où l'on file la laine. */
        /* L'atelier de luxe-tissu s'élève là où l'on EXTRAIT la TEINTURE (murex côtier
         * ou indigo du bas-pays) — place-gated comme la joaillerie l'est par l'or.
         * (La rétroaction négative §NF en bâtira d'autres là où la teinture est importée.) */
        if (re->raw_cap[RES_MUREX] > 0.f || re->raw_cap[RES_INDIGO] > 0.f)
            region_ensure_building(re,BLD_WEAVER_LUX);
        /* Épine dorsale : fonderie + atelier d'outillage là où il y a du FER et de quoi
         * faire le feu — charbon minier OU bois (via la charbonnière). Le fer reste le
         * gate géographique ; le charbon ne l'est plus (charbonnière du bois abondant). */
        if (re->raw_cap[RES_IRON] > 0.f && (re->raw_cap[RES_COAL] > 0.f || re->raw_cap[RES_WOOD] > 0.f)){
            region_ensure_building(re,BLD_FOUNDRY);
            region_ensure_building(re,BLD_TOOLWORKS);
        }
        /* ARCANE : un atelier de mage s'élève au nœud tellurique (cristal). */
        if (re->raw_cap[RES_ARCANE_CRYSTAL] > 0.f) region_ensure_building(re,BLD_MAGE_WORKSHOP);
        /* ARCANE militaire : une forge céleste là où tombe le fer céleste. */
        if (re->raw_cap[RES_CELESTIAL_IRON] > 0.f) region_ensure_building(re,BLD_CELESTIAL_FORGE);
        /* Militaire de base : armurerie au fer, poudrière au salpêtre+charbon. */
        if (re->raw_cap[RES_IRON] > 0.f) region_ensure_building(re,BLD_ARMORY);
        if (re->raw_cap[RES_SALTPETER] > 0.f && re->raw_cap[RES_COAL] > 0.f)
            region_ensure_building(re,BLD_POWDERMILL);
        /* Santé : apothicaire là où poussent les simples (herbes médicinales). */
        if (re->raw_cap[RES_MED_HERBS] > 0.f) region_ensure_building(re,BLD_APOTHECARY);

        /* §NF — CONSTRUCTION PAR RÉTROACTION NÉGATIVE : au-delà de l'implantation
         * géographique ci-dessus (au gisement), un bien PRODUCTIBLE en PÉNURIE
         * (prix ≥ NF_SHORTAGE× base — le signal que « l'input baisse ») APPELLE son
         * producteur dans CETTE région, même sans gisement local. On le bâtit
         * spontanément — mais JAMAIS dans le vide : il faut (a) une POPULATION (des
         * bras qui travaillent, des bouches qui consomment : pop ≥ NF_POP_FLOOR) et
         * (b) de quoi le NOURRIR — l'intrant EXTRAIT sur place OU présent en STOCK
         * (importé), repli (perle…) compris. Le surcroît de bien fait RETOMBER le
         * prix → le signal s'éteint : rétroaction négative, auto-amortie. */
        if (re->colonized){
            float rpop = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                       + re->strata[CLASS_ELITE].pop;
            if (rpop >= NF_POP_FLOOR){
                for (int b=0;b<BLD_TYPE_COUNT;b++){
                    const Recipe *rc=&RECIPE[b];
                    if (rc->out<=RES_NONE || rc->out>=RES_COUNT) continue;
                    if (re->price[rc->out] < BASE_PRICE[rc->out]*NF_SHORTAGE) continue;  /* pas en pénurie */
                    bool feed1 = (rc->in1==RES_NONE)
                              || re->raw_cap[rc->in1]>0.f || re->stock[rc->in1]>=NF_STOCK_MIN
                              || (rc->alt1!=RES_NONE && (re->raw_cap[rc->alt1]>0.f
                                                       || re->stock[rc->alt1]>=NF_STOCK_MIN));
                    bool feed2 = (rc->in2==RES_NONE)
                              || re->raw_cap[rc->in2]>0.f || re->stock[rc->in2]>=NF_STOCK_MIN;
                    if (!feed1 || !feed2) continue;        /* rien pour le nourrir → bâtir dans le vide : refusé */
                    int bi=region_ensure_building(re,(BuildingType)b);
                    if (bi>=0 && re->bld[bi].level < NF_SEED_LEVEL) re->bld[bi].level = NF_SEED_LEVEL;
                }
            }
        }

        /* Niveau initial des manufactures : dimensionné sur la capacité
         * d'accueil (l'infrastructure latente du site). */
        float invest = re->cap_pop*CLASS_SHARE[CLASS_BOURGEOIS];
        for (int i=0;i<re->n_bld;i++)
            re->bld[i].level = 0.5f + invest*0.01f;

        /* ---- Prix & stock de départ. */
        for (int r=0;r<RES_COUNT;r++) {
            re->price[r]=BASE_PRICE[r];
            re->stock[r]=0.f;
        }
    }

    /* ---- ESTUAIRES (commerce asym. §4) : la charnière fleuve ⇄ mer — là où le
     * vrac d'un bassin versant converge. Une cellule de CÔTE au débit notable
     * fait de sa région un entrepôt naturel (la bande Carrefour y montera). */
    for (int i=0;i<SCPS_N;i++){
        const Cell *c=&w->cell[i];
        if (!c->coast || c->river<40) continue;
        int r=c->region;
        if (r>=0 && r<e->n_regions) e->region[r].estuary=true;
    }

    /* ---- Adjacence de régions (terre, 4-connexe) pour la colonisation ---- *
     * On ne trace un lien que si AUCUNE des deux régions n'est infranchissable.
     * Cela rend les glaciers et déserts hyperarides des barrières naturelles :
     * une civilisation ne peut pas coloniser « de l'autre côté » d'une zone morte
     * sans contourner par une région habitable adjacente.                     */
    static const int DX4[4]={1,-1,0,0}, DY4[4]={0,0,1,-1};
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int ra=w->cell[scps_idx(x,y)].region;
        if (ra<0) continue;
        for (int d=0;d<4;d++) {
            int nx=x+DX4[d], ny=y+DY4[d];
            if (nx<0||nx>=SCPS_W||ny<0||ny>=SCPS_H) continue;
            int rb=w->cell[scps_idx(nx,ny)].region;
            if (rb<0||rb==ra) continue;
            /* Ne créer un lien que si les deux régions sont franchissables */
            if (!e->region[ra].impassable && !e->region[rb].impassable) {
                e->adj[ra][rb]=1; e->adj[rb][ra]=1;
            }
        }
    }

    /* ---- Peuplement initial : monde quasi vide ----------------------------- *
     * JOUEUR / ANTAGONISTE : seule la région-capitale est peuplée à cap_pop.
     *   Le reste de leur territoire est vierge → ils colonisent.
     * CITÉ-ÉTAT : TOUTES leurs régions sont peuplées à pop réduite (2000 / n_regs
     *   par région, plafonnée à cap_pop). Elles colonisent ensuite leurs propres
     *   territoires vacants mais n'en sortent jamais.
     * Tout le reste du monde est vierge et colonisable. */
    for (int cid=0; cid<w->n_countries; cid++) {
        const Country *ct=&w->country[cid];
        PolityRole role=ct->role;

        if (role==POLITY_PLAYER || role==POLITY_ANTAGONIST) {
            int cap_prov=ct->capital_prov;
            if (cap_prov<0||cap_prov>=w->n_provinces) continue;
            int cap_reg=w->province[cap_prov].region;
            if (cap_reg<0||cap_reg>=e->n_regions) continue;
            RegionEconomy *re=&e->region[cap_reg];
            if (!re->active) continue;
            econ_seed_population(re, re->cap_pop);
            re->colonized=true;
            re->owner=(int16_t)cid;

        } else if (role==POLITY_CITY_STATE) {
            /* Compter les régions actives du pays */
            int n_act=0;
            for (int ri=0;ri<ct->n_regions;ri++){
                int rid=ct->region_ids[ri];
                if (rid>=0&&rid<e->n_regions&&e->region[rid].active) n_act++;
            }
            if (n_act==0) continue;
            /* 2000 pop répartis uniformément (plafond = cap_pop de chaque région).
             * À 3 régions → ~667/reg ; à 5 → ~400/reg. Les régions vierges seront
             * colonisées depuis les régions sœurs (econ_colonize_tick). */
            float pop_per_reg = 2000.f / (float)n_act;
            for (int ri=0;ri<ct->n_regions;ri++){
                int rid=ct->region_ids[ri];
                if (rid<0||rid>=e->n_regions) continue;
                RegionEconomy *re=&e->region[rid];
                if (!re->active) continue;
                econ_seed_population(re, fminf(re->cap_pop, pop_per_reg));
                re->colonized=true;
                re->owner=(int16_t)cid;
            }
        }
    }
}

/* ====================================================================== */
/* SIMULATION — un tick                                                   */
/* ====================================================================== */

/* §7 — Tolérance fiscale par ÉTHOS × classe : le SEUIL (×satisfaction) au-delà
 * duquel on FUIT l'impôt et l'on gronde. La culture chiffre la stratégie fiscale
 * (un Mercantile n'étrangle pas ses bourgeois ; un Bureaucrate extrait partout ;
 * un Dominateur essore la masse mais pas l'élite). */
float econ_tax_tolerance(Ethos e, SocialClass c){
    static const float T[ETHOS_COUNT][CLASS_COUNT] = {
        /*               Laborer Bourgeois Élite */
        /* DOMINATEUR */ {0.60f,  0.40f,   0.25f},
        /* HONNEUR    */ {0.55f,  0.40f,   0.22f},
        /* ORDRE      */ {0.55f,  0.52f,   0.42f},
        /* BUREAUCRATE*/ {0.60f,  0.60f,   0.58f},
        /* MERCANTILE */ {0.45f,  0.28f,   0.42f},
        /* PACIFISTE  */ {0.30f,  0.30f,   0.30f},
    };
    if (e<0||e>=ETHOS_COUNT||c<0||c>=CLASS_COUNT) return 0.40f;
    return T[e][c];
}
#define STATE_TAX_AMBITION 0.42f   /* le taux que l'État VISE (l'éthos décide ce qui rentre) */
#define K_TAX_AGIT         0.85f   /* poids de la surtaxe sur la satisfaction (la grogne) */
/* §B — DÉ-STÉRILISER LE TRÉSOR + FERMER LE CISEAU OFFRE/DEMANDE.
 *  STATE_SPEND_RATE : part ANNUELLE du trésor que l'État REDÉPENSE (×dt/tick) — il ne
 *  hoarde plus, il circule ; réglé pour un trésor à l'ÉQUILIBRE (≈ TAX/SPEND × richesse),
 *  pas ×16. PAYROLL_FRACTION : part de la dépense versée en GAGES aux classes (le reste
 *  subventionne l'expansion §1). EXPANSION_PRESSION_CAP : l'expansion d'une manufacture
 *  suit le signal-prix de son bien (prix/base), plafonné — l'offre RÉAGIT à la pénurie. */
#define STATE_SPEND_RATE       0.30f
#define PAYROLL_FRACTION       0.60f
#define BASE_EXPANSION         0.20f   /* §1 : vitesse d'expansion d'une manufacture, ∝ pénurie */
#define EXPANSION_PRESSION_CAP 5.0f    /* pénurie max prise en compte (prix/base − 1, plafonné) */

/* §NF v2 — CONSTRUCTION DÉMANDE-MENÉE, DÉLIÉE DU GISEMENT. L'ancien §NF (dans econ_init,
 * prix=0 → inerte) ne tournait JAMAIS : toute manufacture était clouée au gisement local
 * par l'implantation géographique. Ici, CHAQUE TICK : une manufacture s'élève là où son
 * OUTPUT manque (prix ≥ pénurie) et où il y a des BRAS, dès lors que son INTRANT existe
 * QUELQUE PART dans le royaume (le commerce l'achemine) — PLUS sur le gisement local. Le
 * bâtiment suit donc le MARCHÉ (demande + main-d'œuvre), pas l'extraction. region_ensure_
 * building est idempotent → sûr à appeler chaque tick ; le prix qui retombe éteint le signal. */
#define NF_REALM_MIN 0.5f   /* intrant « fournissable » s'il est extrait/produit ≥ ça quelque part dans le pays */
static void econ_build_tick(WorldEconomy *e){
    /* 1. disponibilité d'intrant À L'ÉCHELLE DU PAYS (extraction + offre, n'importe où). */
    static float owner_avail[SCPS_MAX_COUNTRY][RES_COUNT];
    for (int o=0;o<SCPS_MAX_COUNTRY;o++) for (int g=0;g<RES_COUNT;g++) owner_avail[o][g]=0.f;
    for (int r=0;r<e->n_regions;r++){
        RegionEconomy *re=&e->region[r];
        if (!re->active || !re->colonized || re->owner<0 || re->owner>=SCPS_MAX_COUNTRY) continue;
        for (int g=1; g<RES_COUNT; g++) owner_avail[re->owner][g] += re->raw_cap[g] + re->supply[g];
    }
    /* 2. par région PEUPLÉE : bâtir le producteur d'un bien LOCALEMENT en pénurie, si son
     *    intrant existe dans le royaume (ou déjà importé en stock). Plus de gate raw LOCAL. */
    for (int r=0;r<e->n_regions;r++){
        RegionEconomy *re=&e->region[r];
        if (!re->active || !re->colonized || re->owner<0 || re->owner>=SCPS_MAX_COUNTRY) continue;
        float rpop = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop + re->strata[CLASS_ELITE].pop;
        if (rpop < NF_POP_FLOOR) continue;
        const float *avail = owner_avail[re->owner];
        for (int b=0;b<BLD_TYPE_COUNT;b++){
            const Recipe *rc=&RECIPE[b];
            if (rc->out<=RES_NONE || rc->out>=RES_COUNT || BASE_PRICE[rc->out]<=0.f) continue;
            if (b==BLD_FOREUSE && !re->tech_foreuse) continue;                   /* §B2 : foreuse gatée par la tech faustienne */
            if (re->price[rc->out] < BASE_PRICE[rc->out]*NF_SHORTAGE) continue;   /* output pas en pénurie ICI */
            bool feed1 = (rc->in1==RES_NONE)
                      || avail[rc->in1] > NF_REALM_MIN || re->stock[rc->in1] >= NF_STOCK_MIN
                      || (rc->alt1!=RES_NONE && (avail[rc->alt1] > NF_REALM_MIN || re->stock[rc->alt1] >= NF_STOCK_MIN));
            bool feed2 = (rc->in2==RES_NONE)
                      || avail[rc->in2] > NF_REALM_MIN || re->stock[rc->in2] >= NF_STOCK_MIN;
            if (!feed1 || !feed2) continue;        /* le royaume ne sait pas le nourrir → on ne bâtit pas à vide */
            int bi=region_ensure_building(re,(BuildingType)b);
            if (bi>=0 && re->bld[bi].level < NF_SEED_LEVEL) re->bld[bi].level = NF_SEED_LEVEL;
        }
    }
}

/* §B1 — pousse le bonus de PRODUCTION du pays propriétaire vers ses régions. prod_pct
 * (production) et eff_pct (efficacité d'emploi) se cumulent dans un seul multiplicateur
 * sur prod_mult — gain modeste, partout, NON faustien. Appelé avant econ_tick. */
void econ_apply_country_tech(WorldEconomy *e, const TechState *ts, int n_ts){
    if (!e) return;
    for (int r=0;r<e->n_regions;r++){
        RegionEconomy *re=&e->region[r];
        int o=re->owner;
        re->tech_prod = (ts && o>=0 && o<n_ts)
                      ? 1.f + tech_prod_bonus(&ts[o]) + tech_eff_bonus(&ts[o])
                      : 1.f;
        re->tech_foreuse = (ts && o>=0 && o<n_ts) ? ts[o].unlocked[TECH_FOREUSE] : false;  /* §B2 : gate de BLD_FOREUSE */
    }
}

void econ_tick(WorldEconomy *e, float dt) {
    if (dt<=0.f) dt=1.f;
    e->tick++;

    for (int rid=0; rid<e->n_regions; rid++) {
        RegionEconomy *re=&e->region[rid];
        if (!re->active || !re->colonized) continue;

        float supply[RES_COUNT]={0}, demand[RES_COUNT]={0};
        float labor_avail = re->strata[CLASS_LABORER].pop;
        float labor_used  = 0.f;
        float gdp         = 0.f;
        float wage_pool   = 0.f;   /* → laborers */
        float profit_pool = 0.f;   /* → bourgeois */
        float tax_pool    = 0.f;   /* → rente d'élite */
        float over_tax[CLASS_COUNT]={0};   /* surtaxe par classe (grogne, §6) */
        /* OUTILS = le MULTIPLICATEUR de productivité : leur stock (par tête) booste
         * l'extraction ET la manufacture (rendements décroissants, +30% max). Les
         * outils s'USENT (décroissance) → il faut les entretenir (Atelier). */
        float tools_pc  = re->stock[RES_TOOLS] / (labor_avail*0.1f + 1.f);
        float prod_mult = 1.f + 0.30f*(1.f - 1.f/(1.f + tools_pc));   /* lit le stock AVANT usure */
        /* OUTILS — INPUT PASSIF de la main-d'œuvre : les journaliers veulent être ÉQUIPÉS ∝
         * leur nombre, mais SATURANT (on n'outille pas au-delà de l'utile). La demande
         * effective est le COMBLEMENT du déficit d'outillage vers ce palier — elle tire le
         * prix (donc le §NF qui bâtit l'atelier + la perception IA), se RÉOUVRE chaque tick
         * par l'usure, et ne touche QUE prod_mult (ci-dessus), JAMAIS la satisfaction. */
        {
            float tools_target = labor_avail * TOOLS_PER_LABORER;            /* stock-outil VISÉ ∝ bras */
            demand[RES_TOOLS] += fmaxf(0.f, tools_target - re->stock[RES_TOOLS]);  /* déficit à combler (saturant → pas de plafond/runaway) */
        }
        re->stock[RES_TOOLS] *= 0.97f;   /* usure : rouvre un déficit de remplacement chaque tick */
        /* §C3 : le « rot » de l'État (capture par concession) mine l'efficacité NOBLE —
         * une élite gorgée gouverne mal : moins de productivité de capitale, moins de
         * recherche. Lu à l'écran en Corruption. Source : faction_capture_total. */
        float rot = (re->owner>=0)? faction_capture_total(re->owner) : 0.f;
        /* CAPITALE (scps_labor) : sa PRODUCTIVITÉ (+5 %/tier servi) booste la vraie
         * production, au-delà des outils — mais le bonus est rongé par (1−rot). */
        {
            long rpop = (long)(labor_avail + re->strata[CLASS_BOURGEOIS].pop + re->strata[CLASS_ELITE].pop);
            int  ctier = capitale_max_tier(rpop);
            long nob   = capitale_admin_pop(ctier); if (nob>rpop) nob=rpop;
            float cap_bonus = (capitale_prodmult(ctier, nob) - 1.f) * (1.f - rot);
            prod_mult *= (1.f + cap_bonus);
        }
        prod_mult *= (re->tech_prod>0.f ? re->tech_prod : 1.f);   /* §B1 : techs de PRODUCTION du pays (outils/capitale + SAVOIR-FAIRE) */
        /* CÔTE BALAFRÉE (course §4) : la production de la province pillée est
         * entaillée ~1 an ; l'immunité au raid décroît en parallèle. */
        if (re->balafre_days>0.f){ re->balafre_days-=dt*365.f; prod_mult*=0.5f; }
        if (re->raid_cd_days>0.f)  re->raid_cd_days-=dt*365.f;

        /* ---- 1. EXTRACTION = COLLECTE PASSIVE (∝ JOURNALIERS × TERRAIN) -
         * La récolte suit les BRAS qui occupent la tuile, pas le seul terrain : plus de
         * journaliers → plus de collecte, en rendements DÉCROISSANTS (√, la tuile sature).
         * raw_cap = RICHESSE de référence ; pop_intens l'exploite ∝ √(pop/réf). Une région
         * peuplée tire plus de sa terre qu'une région vide → la production SUIT la pop.
         * Borné par la main-d'œuvre dispo (ratio) et l'effort de marché (demande). */
        float pop_active = re->strata[CLASS_LABORER].pop;
        float pop_intens = clampf(sqrtf(fmaxf(pop_active,0.f)/EXTRACT_POP_REF), 0.25f, EXTRACT_INTENS_CAP);
        for (int r=0;r<RES_COUNT;r++) {
            if (re->raw_cap[r]<=0.f) continue;
            float want_labor = re->raw_cap[r]*0.5f*pop_intens;        /* la collecte intensifiée occupe plus de bras */
            float avail = labor_avail-labor_used;
            float ratio = (want_labor>0.f)? clampf(avail/want_labor,0.f,1.f) : 0.f;
            float eff  = market_effort(re->price[r], BASE_PRICE[r]);   /* SURPLUS NATUREL : l'effort suit le prix */
            float out = re->raw_cap[r]*pop_intens*ratio*prod_mult*eff; /* √pop × terrain × outils × effort */
            if (r==RES_WOOD || r==RES_IRON || r==RES_GOLD) out *= 2.0f; /* apport BOIS & FER doublé (épine métal/outils + chauffe) ; OR doublé → nourrir la joaillerie (voie or martiale) */
            labor_used += want_labor*ratio*eff;                        /* le glut LIBÈRE des bras */
            re->stock[r] += out;
            supply[r]    += out;
            float value = out*re->price[r];
            gdp += value;
            wage_pool   += value*WAGE_SHARE;
            profit_pool += value*(1.f-WAGE_SHARE-TAX_RATE);
            tax_pool    += value*TAX_RATE;
        }

        /* ---- 2. MANUFACTURE -------------------------------------------- */
        re->arcane_charge=0.f;   /* essence brûlée CE tick (→ flux faustien) */
        for (int i=0;i<re->n_bld;i++) {
            Building *b=&re->bld[i];
            const Recipe *rc=&RECIPE[b->type];
            /* Production cible = niveau ; bornée par intrants en stock et
             * par la main-d'œuvre restante. */
            /* cap = niveau × effort de marché (SURPLUS NATUREL : on lit le prix sortie). */
            float cap = b->level * market_effort(re->price[rc->out], BASE_PRICE[rc->out]);
            float lim = cap;
            if (rc->in1!=RES_NONE){
                float out_in1 = re->stock[rc->in1]/fmaxf(rc->q1,EPS);   /* sortie possible via in1 */
                if (rc->alt1!=RES_NONE) out_in1 += re->stock[rc->alt1]/fmaxf(rc->alt1_q,EPS);  /* + repli (perle…) */
                lim=fminf(lim, out_in1);
            }
            if (rc->in2!=RES_NONE) lim=fminf(lim, re->stock[rc->in2]/fmaxf(rc->q2,EPS));
            /* RÉSERVE VIVRIÈRE : le grain NOURRIT avant de se brasser. On ne brasse
             * que le SURPLUS au-delà du besoin alimentaire (sinon la bière affame
             * la province — la famine revient). */
            if (rc->in1==RES_GRAIN || rc->in2==RES_GRAIN){
                float pop = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                          + re->strata[CLASS_ELITE].pop;
                float reserve = pop/100.f * 1.20f;      /* besoin de grain (1/100 hab) + marge */
                float spare   = fmaxf(0.f, re->stock[RES_GRAIN] - reserve);
                float gq = (rc->in1==RES_GRAIN)?rc->q1:rc->q2;
                lim = fminf(lim, spare/fmaxf(gq,EPS));
            }
            /* §gate — ORFÈVRERIE bornée à la DEMANDE EFFECTIVE. NB : l'orfèvrerie n'est PAS
             * engorgée mais un LUXE de STATUT à variante culturelle — seules les élites
             * MARTIALES (preferred_luxe) la réclament ; les raffinées prennent l'étoffe
             * précieuse (voie murex). Le gate borne la sortie à la demande réelle pour ne
             * pas brûler l'OR rare (désormais nourri, voie or martiale) en biens que nul ne
             * veut : nul chez les raffinées (demande ~0 → sortie 0), servie chez les martiales.
             * lim est en « lots » → on convertit la sortie voulue par qout·prod_mult. (Les
             * outils, eux, sont régulés par leur demande passive ∝ main-d'œuvre.) */
            if (rc->out==RES_PRECIOUS_WARE){
                float gap = re->demand[rc->out]*GATE_DEMAND_BUFFER - re->stock[rc->out];
                lim = fminf(lim, fmaxf(0.f,gap)/fmaxf(rc->qout*prod_mult,EPS));
            }
            if (lim<=0.f){ b->workers=0.f; continue; }
            float want_labor=rc->labor*cap;
            float avail=labor_avail-labor_used;
            float lratio=(want_labor>0.f)?clampf(avail/want_labor,0.f,1.f):0.f;
            lim=fminf(lim, cap*lratio);
            if (lim<=0.f){ b->workers=0.f; continue; }

            /* Consomme intrants, produit sortie (valeur ajoutée = sortie − intrants).
             * in1 d'abord, puis le repli alt1 à SA quantité (perle = 2× l'or/bijou). */
            float val_in =0.f;
            if (rc->in1!=RES_NONE){
                float out1=fminf(lim, re->stock[rc->in1]/fmaxf(rc->q1,EPS));   /* part faite avec in1 */
                float g1=out1*rc->q1;
                re->stock[rc->in1]-=g1; demand[rc->in1]+=g1; val_in+=g1*re->price[rc->in1];
                float rem=lim-out1;
                if (rem>0.f && rc->alt1!=RES_NONE){
                    float ga=rem*rc->alt1_q;
                    re->stock[rc->alt1]-=ga; demand[rc->alt1]+=ga; val_in+=ga*re->price[rc->alt1];
                }
            }
            if (rc->in2!=RES_NONE){ re->stock[rc->in2]-=lim*rc->q2; demand[rc->in2]+=lim*rc->q2; val_in+=lim*rc->q2*re->price[rc->in2]; }
            float out=lim*rc->qout*prod_mult;   /* outils → productivité */
            out *= (1.f - 0.5f*re->revolt_scar); /* la cicatrice de révolte ronge la production */
            re->stock[rc->out]+=out;
            supply[rc->out]+=out;
            b->workers=rc->labor*lim;
            labor_used+=b->workers;
            /* ARCANE : brûler le cristal pour l'essence nourrit la Brèche. */
            if (b->type==BLD_MAGE_WORKSHOP) re->arcane_charge += out;

            /* Valeur ajoutée = valeur sortie − valeur intrants */
            float val_out=out*re->price[rc->out];
            float va=fmaxf(0.f, val_out-val_in);
            gdp += va;
            wage_pool   += va*WAGE_SHARE;
            profit_pool += va*(1.f-WAGE_SHARE-TAX_RATE);
            tax_pool    += va*TAX_RATE;
        }
        re->gdp=gdp;

        /* ---- 3. REVENUS : salaire / profit / RENTE (l'élite vit de la rente) */
        re->strata[CLASS_LABORER].wealth   += wage_pool;
        re->strata[CLASS_BOURGEOIS].wealth += profit_pool;
        re->strata[CLASS_ELITE].wealth     += tax_pool;   /* rente, PAS l'impôt d'État */

        /* ---- 3b. IMPÔT D'ÉTAT (§6-7) : par classe, taux VISÉ borné par le SEUIL
         * = tolérance(éthos,classe) × (0.4 + 0.6·satisfaction du tick passé).
         * Au-delà : ÉVASION (le net BAISSE) + grogne (la satisfaction chutera).
         * La boucle : un peuple CONTENT sous un éthos TOLÉRANT paie fort ;
         * surtaxer un peuple mécontent ne rapporte pas — contenter d'abord. */
        float coll[CLASS_COUNT]={0}, coll_tot=0.f;     /* §B : ce que CHAQUE classe a versé (pour le rendre) */
        for (int c=0;c<CLASS_COUNT;c++){
            PopStratum *st=&re->strata[c];
            float sat   = clampf(st->satisfaction,0.f,1.f);
            float seuil = econ_tax_tolerance(re->culture.ethos,(SocialClass)c)*(0.40f+0.60f*sat);
            float evasion   = clampf(STATE_TAX_AMBITION - seuil, 0.f, 1.f);
            float collected = STATE_TAX_AMBITION * st->wealth * (1.f-evasion) * dt;
            if (collected>st->wealth) collected=st->wealth;
            st->wealth   -= collected;
            re->treasury += collected;
            coll[c]=collected; coll_tot+=collected;
            over_tax[c]   = (STATE_TAX_AMBITION>seuil)?(STATE_TAX_AMBITION-seuil):0.f;
        }
        re->over_tax = clampf(over_tax[CLASS_LABORER], 0.f, 1.f);   /* grief des laboureurs → révolte */

        /* §B (TRÉSOR MORT) — l'État REDÉPENSE : il ne hoarde plus, il CIRCULE. Une masse
         * salariale réabonde la richesse des classes AU PRORATA de l'impôt qu'elles ont
         * versé (les hautes classes, les plus ponctionnées, récupèrent le plus → pouvoir
         * d'achat restauré → satisfaction, et le drain C1 sur l'élite est réparé). Le
         * solde subventionne l'expansion (§1). Sans cette sortie, le trésor ×16 asséchait
         * les classes à richesse ~0 → 15 % de satisfaction même quand les biens existent. */
        float depense = re->treasury * STATE_SPEND_RATE * dt;
        if (depense > re->treasury) depense = re->treasury;
        re->treasury -= depense;
        float payroll = depense * PAYROLL_FRACTION;
        if (coll_tot > 1e-6f)
            for (int c=0;c<CLASS_COUNT;c++)
                re->strata[c].wealth += payroll * (coll[c]/coll_tot);   /* on rend à chacun ∝ sa contribution */
        /* le solde (depense − payroll) a quitté le trésor en DÉPENSE PUBLIQUE (armée,
         * travaux) : il ne s'agit plus de hoarder. L'expansion (§1) est, elle, portée par
         * le signal-prix — le pouvoir d'achat rendu ici en est le carburant indirect. */

        /* §besoins progressifs — combien de besoins (par ordre de priorité) sont ACTIFS
         * dans cette région : f(niveau de capitale, que la POP débloque). Petit centre →
         * 2 besoins (les bases) ; grande capitale → tout le panier (statut compris). Un
         * besoin non encore débloqué ne crée NI demande NI manque de satisfaction. */
        long rpop_nd = (long)(re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                            + re->strata[CLASS_ELITE].pop);
        int active_needs = 1 + capitale_max_tier(rpop_nd);   /* tier 1 → 2 besoins, +1 par tier */

        /* ---- 4. DEMANDE de consommation par strate ---------------------
         * §2 (CORRECTIF D'INTÉGRATION) : la demande des paliers VARIANTES suit la
         * PRÉFÉRENCE culturelle, EXACTEMENT comme la satisfaction (étape 5). Le
         * besoin inscrit dans la case canonique (RES_WINE = palier moral,
         * RES_PRECIOUS_WARE = palier statut) est ROUTÉ vers la variante préférée
         * (bière/vin, orfèvrerie/étoffe précieuse). Sans cela, bière & étoffe
         * précieuse restent à demande nulle (prix planché, jamais tirées par le
         * marché) tandis que vin & orfèvrerie portent TOUTE la demande (prix
         * plafond, pénurie structurelle) → l'élite ne reçoit pas son statut → coup. */
        for (int c=0;c<CLASS_COUNT;c++) {
            float units=re->strata[c].pop/100.f*DEMAND_TENSION;   /* /100 hab, tendu +10 % */
            for (int r=0;r<RES_COUNT;r++) {
                float need=NEED[c][r];
                if (need<=0.f) continue;
                if (need_rank(c,(Resource)r) >= active_needs) continue;   /* besoin pas encore débloqué */
                Resource tgt=(Resource)r;
                if      (r==RES_WINE)          tgt=preferred_drink(&re->culture);
                else if (r==RES_PRECIOUS_WARE) tgt=preferred_luxe(&re->culture);
                demand[tgt]+=need*units;
            }
        }

        /* ---- 5. MARCHÉ : prix puis allocation au budget ---------------- */
        /* Prix : converge vers base × demande/(stock+offre). */
        for (int r=0;r<RES_COUNT;r++) {
            if (BASE_PRICE[r]<=0.f) continue;
            float avail=re->stock[r]+supply[r];
            float target=BASE_PRICE[r]*clampf(demand[r]/(avail+EPS),0.2f,6.f);
            re->price[r]=re->price[r]*PRICE_INERTIA + target*(1.f-PRICE_INERTIA);
            re->price[r]=clampf(re->price[r],BASE_PRICE[r]*0.15f,BASE_PRICE[r]*8.f);
        }

        /* Satisfaction par strate : fraction des besoins effectivement
         * achetée, pondérée par la solvabilité (budget vs coût). On sert
         * d'abord les vivres, puis le reste. Le stock disponible plafonne. */
        /* Suivi de la couverture RÉELLE par palier (pas la satisfaction globale) :
         * food_got mesure les VIVRES effectivement servis, soc_got le reste. */
        float r_food_need=0.f, r_food_got=0.f, r_soc_need=0.f, r_soc_got=0.f;
        for (int c=0;c<CLASS_COUNT;c++) {
            float units=re->strata[c].pop/100.f*DEMAND_TENSION;   /* /100 hab, tendu +10 % */
            if (units<=0.f){ re->strata[c].satisfaction=0.f; continue; }
            float budget=re->strata[c].wealth;
            float need_w=0.f, met_w=0.f;   /* pondération par valeur du besoin */
            for (int r=0;r<RES_COUNT;r++) {
                float need=NEED[c][r]*units;
                if (need<=0.f) continue;
                if (need_rank(c,(Resource)r) >= active_needs) continue;   /* §progressif : besoin pas encore débloqué → ne pèse pas */
                /* ── Palier MORAL (boisson) : VARIANTE culturelle bière/vin ──
                 * On sert la boisson PRÉFÉRÉE de la culture locale d'abord ; la
                 * mauvaise ne comble qu'à moitié (un nain boude le vin). */
                if (r==RES_WINE){
                    float w_d=BASE_PRICE[RES_WINE]*need;   /* valeur du palier (réf. vin) */
                    need_w+=w_d;
                    Resource pref=preferred_drink(&re->culture);
                    Resource alt =(pref==RES_BEER)?RES_WINE:RES_BEER;
                    float cs_p=clampf(re->stock[pref]/(need+EPS),0.f,1.f);
                    float cost_p=need*cs_p*re->price[pref];
                    float cb_p=(cost_p>0.f)?clampf(budget/cost_p,0.f,1.f):1.f;
                    float got_p=cs_p*cb_p;
                    re->stock[pref]-=need*got_p; budget-=need*got_p*re->price[pref];
                    float rem=1.f-got_p;                   /* comblé par la mauvaise boisson */
                    float cs_a=clampf(re->stock[alt]/(need*rem+EPS),0.f,1.f)*rem;
                    float cost_a=need*cs_a*re->price[alt];
                    float cb_a=(cost_a>0.f)?clampf(budget/cost_a,0.f,1.f):1.f;
                    float got_a=cs_a*cb_a;
                    re->stock[alt]-=need*got_a; budget-=need*got_a*re->price[alt];
                    float got=clampf(got_p + DRINK_OFFCULT*got_a, 0.f, 1.f);
                    met_w+=w_d*got; r_soc_need+=need; r_soc_got+=need*got;
                    continue;
                }
                /* ── Palier STATUT (luxe d'élite) : VARIANTE culturelle ──
                 * orfèvrerie (martial) OU étoffe précieuse (raffiné) ; le mauvais
                 * luxe ne flatte qu'à moitié (l'élite conquise reste sur sa faim). */
                if (r==RES_PRECIOUS_WARE){
                    Resource pref=preferred_luxe(&re->culture);
                    Resource alt =(pref==RES_PRECIOUS_WARE)?RES_PRECIOUS_CLOTH:RES_PRECIOUS_WARE;
                    float w_l=BASE_PRICE[pref]*need; need_w+=w_l;
                    float cs_p=clampf(re->stock[pref]/(need+EPS),0.f,1.f);
                    float cost_p=need*cs_p*re->price[pref];
                    float cb_p=(cost_p>0.f)?clampf(budget/cost_p,0.f,1.f):1.f;
                    float got_p=cs_p*cb_p;
                    re->stock[pref]-=need*got_p; budget-=need*got_p*re->price[pref];
                    float rem=1.f-got_p;
                    float cs_a=clampf(re->stock[alt]/(need*rem+EPS),0.f,1.f)*rem;
                    float cost_a=need*cs_a*re->price[alt];
                    float cb_a=(cost_a>0.f)?clampf(budget/cost_a,0.f,1.f):1.f;
                    float got_a=cs_a*cb_a;
                    re->stock[alt]-=need*got_a; budget-=need*got_a*re->price[alt];
                    float got=clampf(got_p + LUXE_OFFCULT*got_a, 0.f, 1.f);
                    met_w+=w_l*got; r_soc_need+=need; r_soc_got+=need*got;
                    continue;
                }
                float w=BASE_PRICE[r]*need;          /* importance ~ valeur */
                need_w+=w;
                float can_stock=clampf(re->stock[r]/(need+EPS),0.f,1.f);
                float cost=need*can_stock*re->price[r];
                float can_buy=(cost>0.f)?clampf(budget/cost,0.f,1.f):1.f;
                float got=can_stock*can_buy;
                /* consomme stock & budget */
                re->stock[r]-=need*got;
                budget-=need*got*re->price[r];
                met_w+=w*got;
                /* couverture par palier : les vivres VS le reste */
                if (r==RES_GRAIN||r==RES_FISH||r==RES_LIVESTOCK){ r_food_need+=need; r_food_got+=need*got; }
                else                                            { r_soc_need +=need; r_soc_got +=need*got; }
            }
            re->strata[c].wealth=fmaxf(0.f,budget);
            float basket=(need_w>0.f)?met_w/need_w:0.5f;
            /* la surtaxe (§6) gronde : elle ABAISSE la satisfaction → agitation */
            re->strata[c].satisfaction=clampf(basket - over_tax[c]*K_TAX_AGIT, 0.f, 1.f);
        }

        /* ---- 6. MISE À JOUR : démographie, tech, satisfaction générale - */
        /* food_sat = la couverture VIVRIÈRE RÉELLE (et non la satisfaction
         * globale) → plus de nourriture = plus de croissance, fin de la famine. */
        {
            re->food_sat   = (r_food_need>0.f)?clampf(r_food_got/r_food_need,0.f,1.f):0.5f;
            re->society_sat= (r_soc_need >0.f)?clampf(r_soc_got /r_soc_need ,0.f,1.f):0.5f;
            /* §4 — pénalité OFF-CULTURE : une minorité d'une autre sphère est MAL
             * servie par les biens (confort/moral/luxe) de la culture dominante.
             * Frappe la satisfaction SOCIALE — PAS les vivres (food_sat épargné,
             * universel) → la survie/croissance ne sont pas punies par la
             * diversité ; l'assimilation efface la pénalité. */
            re->society_sat *= (1.f - 0.60f*econ_off_culture_fraction(&re->pop));
        }

        /* Croissance calibrée : doublement ~30 ans à food_sat=1, soc=0.5
         *   net = BIRTH_RATE*food_sat - DEATH_RATE + SOCIETY_BONUS*society_sat
         * + pic de famine si food_sat < 0.35
         * Plafond souple : la croissance s'annule à 1.1×cap_pop (apex naturel
         * entre 1000 et 6000 selon la capacité du site). */
        float food_s = re->food_sat;
        float soc_s  = re->society_sat;
        /* Démographie modulée par la RACE (Prolifique/Régénérant → + de naissances ;
         * Lent à croître → moins). Levier de la couche biologique. */
        SpeciesBuild sb_demo = species_default_build(re->culture.race);
        float demo = build_leviers(&sb_demo).demographie;
        float net_growth = BIRTH_RATE*(1.f+demo)*food_s - DEATH_RATE + SOCIETY_BONUS*soc_s;
        if (food_s < 0.35f)
            net_growth -= (0.35f - food_s) * 0.12f;   /* pic de mortalité famine */
        net_growth = clampf(net_growth, -0.10f, 0.06f);
        /* CICATRICE DE RÉVOLTE : une province récemment soulevée se développe mal —
         * −50 % de croissance tant que la plaie n'est pas refermée (fade ~4 ans). */
        re->revolt_scar = fmaxf(0.f, re->revolt_scar - 0.25f*dt);
        re->pillage_cd  = fmaxf(0.f, re->pillage_cd  - dt);   /* le saccage se rouvre après ~5 ans */
        net_growth *= (1.f - 0.5f*re->revolt_scar);
        net_growth *= dt;   /* cumulatif → suit le pas (mensuel : 1/12 d'an) */

        float total_pop_now=0.f;
        for (int c=0;c<CLASS_COUNT;c++) total_pop_now+=re->strata[c].pop;
        /* Les greniers/irrigation bâtis (food_cap) étendent l'apex démographique. */
        float eff_cap = re->cap_pop + re->build.food_cap*250.f;
        float cap_factor = fmaxf(0.f, 1.f - total_pop_now/(eff_cap*1.1f));
        net_growth *= cap_factor;

        float satsum=0.f, popsum=0.f;
        for (int c=0;c<CLASS_COUNT;c++) {
            PopStratum *st=&re->strata[c];
            st->pop *= 1.f + net_growth;
            if (st->pop<1.f) st->pop=1.f;
            satsum+=st->satisfaction*st->pop; popsum+=st->pop;
        }
        re->satisfaction=(popsum>0.f)?satsum/popsum:0.f;
        /* l'insatisfaction off-culture pèse sur la satisfaction GÉNÉRALE (donc
         * prospérité/légitimité/impôt) — mais food_sat reste intact (la survie). */
        re->satisfaction *= (1.f - 0.45f*econ_off_culture_fraction(&re->pop));
        re->prosperity = re->gdp/(popsum+1.f);

        /* Tech : les élites convertissent richesse × satisfaction en savoir. La
         * bibliothèque/le monastère BÂTI (densité de savoir) accélère la cadence. */
        PopStratum *el=&re->strata[CLASS_ELITE];
        float savoir_mult = 1.f + 0.25f*re->build.savoir;   /* +25 % de recherche / point bâti */
        re->tech += el->wealth*TECH_RATE*el->satisfaction*savoir_mult*(1.f-rot)*dt;  /* §C3 : élite capturée recherche moins */

        /* §1 (CISEAU) — l'offre SUIT le signal-prix : chaque manufacture s'étend ∝ la
         * PÉNURIE de son bien (prix AU-DESSUS de la base). À l'équilibre (prix=base) elle
         * stagne ; en pénurie criante (prix→plafond) elle croît vite → l'offre rattrape la
         * demande qui gonfle avec la population, au lieu de ramper à plat (+0.05 plafonné).
         * Rétroaction NÉGATIVE intrinsèque : plus de capacité → prix qui retombe → expansion
         * qui ralentit → pas de snowball (et la surextension→sécession reste l'anti-runaway
         * territorial, intacte). L'État y verse sa subvention (§2) via le pouvoir d'achat. */
        for (int i=0;i<re->n_bld;i++){
            const Recipe *rc=&RECIPE[re->bld[i].type];
            Resource out = rc->out;
            if (out<=RES_NONE || out>=RES_COUNT || BASE_PRICE[out]<=0.f) continue;
            /* §1b — n'étend QUE le maillon RÉELLEMENT saturé : on ne lit pas le prix de
             * l'intrant (un goulet de QUANTITÉ ne fait pas monter son prix — le marché du
             * métal solde au peu qu'on extrait), mais le TAUX D'UTILISATION effectif. Une
             * manufacture qui tourne loin sous sa capacité est bornée en amont (intrant) ou
             * en bras : l'étendre serait vain (cas outillage métal-borné → level runaway). */
            float meff = market_effort(re->price[out], BASE_PRICE[out]);
            float cap  = re->bld[i].level * meff;
            float lim_est = (rc->labor>0.f)? re->bld[i].workers/rc->labor : cap;
            if (cap > EPS && lim_est < 0.70f*cap) continue;   /* sous-utilisé → goulet en amont, pas ici */
            float pression = clampf(re->price[out]/BASE_PRICE[out] - 1.f, 0.f, EXPANSION_PRESSION_CAP);
            re->bld[i].level += BASE_EXPANSION * pression * dt;   /* ∝ pénurie, auto-amortie */
        }

        for (int r=0;r<RES_COUNT;r++){ re->supply[r]=supply[r]; re->demand[r]=demand[r]; }

        /* Diaspora : bonus tech par innovation culturelle accumulée.
         * S'absorbe progressivement (acculturation, demi-vie ~50 ticks). */
        if (re->diaspora_pop > 0.f) {
            re->tech += re->diaspora_pop * DIASPORA_TECH_RATE * re->diaspora_innovation;
            re->diaspora_pop       *= DIASPORA_DECAY;
            re->diaspora_innovation*= DIASPORA_DECAY;
            if (re->diaspora_pop < 0.5f) {
                re->diaspora_pop=0.f; re->diaspora_innovation=0.f;
            }
        }

        /* Coercition : décroît exponentiellement (demi-vie ≈ 10 ticks).
         * La relocalisation forcée et certains événements la relèvent. */
        re->coercion *= COERCION_DECAY;
        if (re->coercion < 0.005f) re->coercion=0.f;

        /* Décroissance du stock excédentaire (denrées périssables / report
         * limité) : 15% s'évapore, évite l'accumulation infinie. */
        for (int r=0;r<RES_COUNT;r++) re->stock[r]*=0.85f;
    }
    econ_build_tick(e);   /* §NF v2 — la construction suit le MARCHÉ (demande + bras), plus le gisement */
}

/* ====================================================================== */
/* COLONISATION                                                            */
/* ====================================================================== */
/* Joueur/Antagoniste : essaiment vers toute région vierge adjacente.
 * Cité-État        : essaime uniquement vers ses propres régions vacantes.
 * Dans les deux cas : au plus une fondation par polité par tick.           */

static void colonize_from(WorldEconomy *e, int src_rid, int dst_rid, int cid);
void econ_colonize_from(WorldEconomy *e, int src_rid, int dst_rid, int cid){
    if (src_rid<0||src_rid>=e->n_regions||dst_rid<0||dst_rid>=e->n_regions) return;
    colonize_from(e,src_rid,dst_rid,cid);
}
static void colonize_from(WorldEconomy *e, int src_rid, int dst_rid, int cid) {
    RegionEconomy *src=&e->region[src_rid];
    RegionEconomy *dst=&e->region[dst_rid];
    float spop=0.f; for(int c=0;c<CLASS_COUNT;c++) spop+=src->strata[c].pop;
    float take=fminf(COLONY_COST_POP, spop*0.25f);
    for (int c=0;c<CLASS_COUNT;c++)
        src->strata[c].pop -= take*(src->strata[c].pop/fmaxf(spop,EPS));
    econ_seed_population(dst, COLONY_SEED_POP);
    dst->colonized=true;
    dst->culture.settled=true;   /* la culture de biome (gen_population) s'active */
    dst->owner=(int16_t)cid;
}

int econ_colonize_tick(WorldEconomy *e, const World *w) {
    int founded=0;

    for (int cid=0; cid<w->n_countries; cid++) {
        const Country *ct=&w->country[cid];
        PolityRole role=ct->role;

        if (role==POLITY_PLAYER || role==POLITY_ANTAGONIST) {
            /* Cherche la meilleure paire (source, cible) du pays. */
            int best_src=-1, best_dst=-1; float best_score=-1.f;
            for (int rs=0; rs<e->n_regions; rs++) {
                RegionEconomy *src=&e->region[rs];
                if (!src->colonized || src->owner!=cid) continue;
                float spop=0.f; for(int c=0;c<CLASS_COUNT;c++) spop+=src->strata[c].pop;
                if (spop<COLONY_MIN_POP || src->food_sat<COLONY_FOOD_GATE) continue;
                for (int rd=0; rd<e->n_regions; rd++) {
                    if (!e->adj[rs][rd]) continue;
                    RegionEconomy *dst=&e->region[rd];
                    if (!dst->active || dst->colonized) continue;
                    float score = dst->cap_pop*0.001f + (spop-COLONY_MIN_POP)*0.0005f
                                + src->food_sat;
                    if (score>best_score){ best_score=score; best_src=rs; best_dst=rd; }
                }
            }
            if (best_src>=0 && best_dst>=0) {
                colonize_from(e, best_src, best_dst, cid);
                founded++;
            }

        } else if (role==POLITY_CITY_STATE) {
            /* Ne peut coloniser que ses propres régions vacantes adjacentes à
             * une de ses régions déjà peuplées. */
            int best_src=-1, best_dst=-1; float best_score=-1.f;
            for (int ri=0; ri<ct->n_regions; ri++) {
                int rs=ct->region_ids[ri];
                if (rs<0||rs>=e->n_regions) continue;
                RegionEconomy *src=&e->region[rs];
                if (!src->colonized || src->owner!=cid) continue;
                float spop=0.f; for(int c=0;c<CLASS_COUNT;c++) spop+=src->strata[c].pop;
                if (spop<COLONY_MIN_POP || src->food_sat<COLONY_FOOD_GATE) continue;
                /* Cibles : uniquement les régions sœurs du même pays */
                for (int rj=0; rj<ct->n_regions; rj++) {
                    int rd=ct->region_ids[rj];
                    if (rd<0||rd>=e->n_regions||!e->adj[rs][rd]) continue;
                    RegionEconomy *dst=&e->region[rd];
                    if (!dst->active || dst->colonized) continue;
                    float score = dst->cap_pop*0.001f + spop*0.0005f;
                    if (score>best_score){ best_score=score; best_src=rs; best_dst=rd; }
                }
            }
            if (best_src>=0 && best_dst>=0) {
                colonize_from(e, best_src, best_dst, cid);
                founded++;
            }
        }
    }
    return founded;
}

/* ====================================================================== */
/* MIGRATION INTERNE                                                       */
/* ====================================================================== */
/* Principe : les bourgeois et élites migrent vers les régions adjacentes
 * plus prospères. La migration crée de la DIASPORA dans la destination, dont
 * l'apport d'innovation (tech) dépend de la DISTANCE CULTURELLE entre la
 * population d'origine et la population hôte (et non de l'écart de richesse).
 *
 * Les laborers ne migrent pas spontanément ; ils sont l'objet de relocalisation
 * forcée (econ_relocate_pop).                                                  */

int econ_migrate_tick(WorldEconomy *e, const World *w) {
    (void)w;  /* adj est dans e ; w réservé pour extensions futures */
    int flows=0;

    for (int rs=0; rs<e->n_regions; rs++) {
        RegionEconomy *src=&e->region[rs];
        if (!src->colonized || src->gdp<=0.f) continue;

        for (int rd=0; rd<e->n_regions; rd++) {
            if (!e->adj[rs][rd]) continue;
            RegionEconomy *dst=&e->region[rd];
            if (!dst->colonized) continue;

            /* Différentiel de prospérité : dst doit être significativement
             * plus riche pour attirer des migrants. */
            float pros_src = src->prosperity + 0.01f;  /* éviter div/0 */
            float pros_dst = dst->prosperity;
            if (pros_dst <= pros_src * MIGRATE_THRESHOLD) continue;

            /* Taux de migration proportionnel au différentiel, plafonné. */
            float ratio = fminf(MIGRATE_RATE,
                                (pros_dst/pros_src - 1.f) * 0.04f);
            float migrated=0.f;

            for (int cl=CLASS_BOURGEOIS; cl<CLASS_COUNT; cl++) {
                float mv = src->strata[cl].pop * ratio;
                if (mv < 0.5f) continue;
                /* Transfert pop + richesse proportionnelle */
                float wfrac = mv / fmaxf(src->strata[cl].pop, 1.f);
                float wmv   = src->strata[cl].wealth * wfrac;
                src->strata[cl].pop    -= mv;
                src->strata[cl].wealth -= wmv;
                dst->strata[cl].pop    += mv;
                dst->strata[cl].wealth += wmv;
                migrated += mv;
            }

            if (migrated < 0.5f) continue;

            /* Effet diaspora : la nouveauté apportée est la DISTANCE CULTURELLE
             * (L∞ de contenu, [0..1]) entre population source et hôte. Deux
             * populations proches n'innovent pas en se mêlant ; deux populations
             * éloignées fécondent l'hôte (au prix d'une friction d'intégration). */
            const PopCulture *csrc = &src->culture;
            const PopCulture *cdst = &dst->culture;
            Culture tmp_src = { .valeurs=csrc->valeurs, .subsistance=csrc->subsistance,
                                .parente=csrc->parente,  .religion=csrc->religion };
            Culture tmp_dst = { .valeurs=cdst->valeurs, .subsistance=cdst->subsistance,
                                .parente=cdst->parente,  .religion=cdst->religion };
            float novelty = culture_content_distance(&tmp_src, &tmp_dst) / 10.f; /* [0..1] */
            dst->diaspora_pop         += migrated;
            dst->diaspora_innovation  += migrated * novelty;
            flows++;
        }
    }
    return flows;
}

/* ====================================================================== */
/* RELOCALISATION FORCÉE                                                   */
/* ====================================================================== */
/* Déplace `amount` habitants (surtout laborers) de src → dst.
 * Génère un pic de coercition dans la source, proportionnel à la fraction
 * déplacée. La destination subit un léger choc d'accueil (stress social).  */

void econ_relocate_pop(WorldEconomy *e, int src_rid, int dst_rid, float amount) {
    if (src_rid<0||src_rid>=e->n_regions||dst_rid<0||dst_rid>=e->n_regions) return;
    RegionEconomy *src=&e->region[src_rid];
    RegionEconomy *dst=&e->region[dst_rid];
    if (!src->colonized||!dst->colonized||amount<1.f) return;

    float src_pop=0.f;
    for (int c=0;c<CLASS_COUNT;c++) src_pop+=src->strata[c].pop;
    float take=fminf(amount, src_pop*0.5f);  /* limite : 50% de la source */
    if (take<1.f) return;

    /* Prélève d'abord les laborers (80%), puis les bourgeois si insuffisant. */
    float lab_take = fminf(take*0.8f, src->strata[CLASS_LABORER].pop);
    float bou_take = fminf(take - lab_take, src->strata[CLASS_BOURGEOIS].pop);
    src->strata[CLASS_LABORER].pop   -= lab_take;
    src->strata[CLASS_BOURGEOIS].pop -= bou_take;
    dst->strata[CLASS_LABORER].pop   += lab_take;
    dst->strata[CLASS_BOURGEOIS].pop += bou_take;

    /* Coercition source : proportionnelle à la fraction déplacée.
     * La destination a un léger choc de réception (10% du spike source). */
    float frac = take / fmaxf(src_pop, 1.f);
    src->coercion = fminf(1.f, src->coercion + RELOC_COERCION_BASE * frac * 4.f);
    dst->coercion = fminf(1.f, dst->coercion + RELOC_COERCION_BASE * 0.10f);
}

/* ====================================================================== */
/* AFFICHAGE CONSOLE                                                       */
/* ====================================================================== */

void econ_print_region(const WorldEconomy *e, const World *w, int region_id) {
    if (region_id<0||region_id>=e->n_regions) return;
    const RegionEconomy *re=&e->region[region_id];
    const Region *rg=&w->region[region_id];
    const char *status = re->impassable ? "[INFRANCHISSABLE]"
                       : re->active    ? ""
                       :                 "[inactive]";
    printf("\n┌─ Région #%d  « %s »  (tick %d) %s  hab=%.0f%%\n",
           region_id, rg->name[0]?rg->name:"—", e->tick,
           status, re->habitability*100.f);
    if (!re->active) return;

    printf("│ Population & classes\n");
    float tot=0.f; for(int c=0;c<CLASS_COUNT;c++) tot+=re->strata[c].pop;
    for (int c=0;c<CLASS_COUNT;c++) {
        const PopStratum *st=&re->strata[c];
        printf("│   %-10s  pop %7.0f (%4.1f%%)  richesse %8.0f  satisf %3.0f%%\n",
               social_class_name(c), st->pop, tot>0?100.f*st->pop/tot:0.f,
               st->wealth, st->satisfaction*100.f);
    }
    printf("│ Satisfaction générale : %.0f%%   PIB %.0f   Trésor %.0f   Tech %.1f\n",
           re->satisfaction*100.f, re->gdp, re->treasury, re->tech);
    if (re->diaspora_pop > 0.5f || re->coercion > 0.005f)
        printf("│ Diaspora : %.0f hab  innov %.2f  coercition %.0f%%\n",
               re->diaspora_pop, re->diaspora_innovation, re->coercion*100.f);

    printf("│ Manufactures\n");
    if (re->n_bld==0) printf("│   (aucune)\n");
    for (int i=0;i<re->n_bld;i++) {
        const Building *b=&re->bld[i];
        printf("│   %-28s niv %4.1f  emploi %6.0f\n",
               building_name(b->type), b->level, b->workers);
    }

    printf("│ Marché (biens actifs)\n");
    printf("│   %-18s %8s %8s %8s %8s\n","bien","prix","offre","demande","stock");
    for (int r=1;r<RES_COUNT;r++) {
        if (re->supply[r]<0.01f && re->demand[r]<0.01f && re->stock[r]<0.01f) continue;
        printf("│   %-18s %8.2f %8.1f %8.1f %8.1f\n",
               resource_name((Resource)r), re->price[r],
               re->supply[r], re->demand[r], re->stock[r]);
    }
    printf("└────────────────────────────────────────────\n");
}

void econ_print_summary(const WorldEconomy *e, const World *w) {
    double pop=0, tech=0, gdp=0, treas=0; float satw=0;
    int active=0, best=-1; float best_gdp=-1.f;
    for (int rid=0; rid<e->n_regions; rid++) {
        const RegionEconomy *re=&e->region[rid];
        if (!re->active) continue;
        active++;
        float rp=0.f; for(int c=0;c<CLASS_COUNT;c++) rp+=re->strata[c].pop;
        pop+=rp; tech+=re->tech; gdp+=re->gdp; treas+=re->treasury;
        satw+=re->satisfaction*rp;
        if (re->gdp>best_gdp){ best_gdp=re->gdp; best=rid; }
    }
    int impass=0;
    for (int rid=0;rid<e->n_regions;rid++) if (e->region[rid].impassable) impass++;
    printf("\n══ SOMMAIRE MONDE  (tick %d) ══\n", e->tick);
    printf("  régions actives : %d / %d  (dont %d infranchissables)\n",
           active, e->n_regions, impass);
    printf("  population tot. : %.0f\n", pop);
    printf("  satisf. moyenne : %.0f%%\n", pop>0?100.f*satw/pop:0.f);
    printf("  PIB cumulé      : %.0f\n", gdp);
    printf("  trésor (taxes)  : %.0f\n", treas);
    printf("  tech cumulée    : %.1f\n", tech);
    if (best>=0)
        printf("  région la plus riche : #%d « %s » (PIB %.0f)\n",
               best, w->region[best].name[0]?w->region[best].name:"—", best_gdp);
}
