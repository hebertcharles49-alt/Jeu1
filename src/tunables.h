/*
 * tunables.h - constantes de balance gameplay, groupees par systeme.
 *
 * Regle du jeu : tout nombre qui pilote du gameplay (dmg, vitesse,
 * cooldown, seuil, probabilite, scaling) doit etre ici, pas en dur
 * dans un .c. Les nombres purement visuels (couleurs, positions
 * pixel, counts de particules) restent dans leur fichier.
 *
 * Les groupes ci-dessous sont organises pour rendre VISIBLES les
 * relations entre constantes : si tu changes T_X tu vois immediatement
 * ce qui depend de T_X plus bas.
 */
#ifndef TUNABLES_H
#define TUNABLES_H

/* ============================================================
 *  SCALING PAR ETAGE
 *  diff = pow(DIFF_FACTOR_PER_FLOOR, floor - 1)
 *  Plus haut = plus fort. 1.15 = HP/dmg double tous les ~5 etages.
 * ============================================================ */
#define DIFF_FACTOR_PER_FLOOR        1.15f

/* nb d'ennemis par salle. Lineaire au debut, accelere apres FLOOR_RAMP. */
#define ENEMIES_PER_ROOM_BASE        3
#define ENEMIES_PER_ROOM_RNG         3    /* jitter random ajoute */
#define ENEMIES_PER_ROOM_RAMP_FLOOR  5    /* floor a partir duquel on accelere */

/* ============================================================
 *  ELITE
 *  Roll : chance = 5% * floor (cap 50%).
 *  HP : maxhp * (BASE + RAMP * floor) -- soit 2.0 floor 1 a 2.5 floor 10.
 * ============================================================ */
#define ELITE_CHANCE_PER_FLOOR       0.05f
#define ELITE_CHANCE_CAP             0.50f
#define ELITE_BIOME_TINT_CHANCE_PCT  25      /* % d'elite qui prend l element du biome */
#define ELITE_HP_MUL_BASE            2.0f
#define ELITE_HP_MUL_PER_FLOOR       0.05f
#define ELITE_R_BONUS                1.5f
#define ELITE_COIN_MUL               3
#define ELITE_XP_MUL                 2

/* ============================================================
 *  BOSS
 *  Phases declenchees a frac HP. Aura visuelle pulse en phase 1+ et 2+.
 *  Enrage = sous ENRAGE_FRAC -> dt * BOSS_ENRAGE_DT_MUL.
 * ============================================================ */
#define BOSS_HP_BASE                 220.f  /* * diff */
#define BOSS_TELEGRAPH_AT_SPAWN      1.5f
#define BOSS_PHASE_1_HP_FRAC         0.55f
#define BOSS_PHASE_2_HP_FRAC         0.25f
#define BOSS_ENRAGE_HP_FRAC          0.10f
#define BOSS_ENRAGE_DT_MUL           1.6f
#define BOSS_PHASE_TRANSITION_INVULN 0.30f
#define BOSS_PHASE_TRANSITION_SHAKE  0.35f
#define BOSS_PHASE_TRANSITION_MAG    6.0f
#define BOSS_DEATH_T                 1.8f
#define BOSS_DEATH_SHAKE_T           0.9f
#define BOSS_DEATH_SHAKE_MAG         10.f
#define BOSS_DEATH_HITSTOP           0.45f
#define BOSS_DEATH_SLOWMO            0.30f  /* dt *= 0.30 pendant la mort */

/* contact dmg boss : (BASE + phase * STEP) * diff */
#define BOSS_CONTACT_DMG_BASE        14.f
#define BOSS_CONTACT_DMG_PER_PHASE   4.f

/* ============================================================
 *  KILLSTREAK / OVERDRIVE  (moment de rupture)
 *  WINDOW < T : on garantit qu'un nouvel overdrive ne re-trigger pas
 *  immediatement apres le precedent.
 * ============================================================ */
#define KILLSTREAK_TRIGGER           5
#define KILLSTREAK_WINDOW_S          3.0f
#define OVERDRIVE_T                  5.0f
#define OVERDRIVE_DMG_MUL            1.30f
#define OVERDRIVE_CD_MUL             0.67f   /* cd arme * 0.67 = +50% rate */

/* ============================================================
 *  KNOCKBACK -- impact mur + chain enemy-enemy
 *  dmg_impact = (kspeed - THRESH) * DMG_K, capped a CAP.
 * ============================================================ */
#define KNOCKBACK_IMPACT_THRESHOLD   80.f
#define KNOCKBACK_IMPACT_DMG_K       0.06f
#define KNOCKBACK_IMPACT_DMG_CAP     30.f
#define KNOCKBACK_CHAIN_THRESHOLD    100.f
#define KNOCKBACK_CHAIN_DMG_K        0.05f
#define KNOCKBACK_CHAIN_DMG_CAP      20.f
#define KNOCKBACK_CHAIN_TRANSFER     0.5f
#define KNOCKBACK_CHAIN_SELF_FRAC    0.5f    /* attaquant prend 50% du chain dmg */
#define KNOCKBACK_PROP_BUMP_SPEED    90.f
#define KNOCKBACK_PROP_BUMP_DECAY    0.60f

/* ============================================================
 *  LOOT FEEDBACK
 *  Drop legendaire ou unique -> hitstop + shake + 60 part dorees.
 * ============================================================ */
#define LOOT_LEG_HITSTOP             0.30f
#define LOOT_LEG_SHAKE_T             0.35f
#define LOOT_LEG_SHAKE_MAG           5.5f

/* ============================================================
 *  AOE PHYSIQUE
 *  Mur fissure casse si dmg AOE > BREAK_THRESHOLD.
 * ============================================================ */
#define AOE_WALL_BREAK_DMG_MIN       10.f

/* ============================================================
 *  META : achats permanents (sanctuaire)
 *  Cf game.h PERM_MAX_LEVEL. Cost = base * (1 + level).
 * ============================================================ */
#define HUB_HP_STEP                  10
#define HUB_ARMOR_STEP               1
#define HUB_SPEED_STEP               5
#define HUB_DMG_PCT_STEP             5
#define HUB_HP_BASE_COST             40
#define HUB_ARMOR_BASE_COST          60
#define HUB_SPEED_BASE_COST          50
#define HUB_DMG_BASE_COST            70

/* ============================================================
 *  TOASTS
 * ============================================================ */
#define TOAST_LIFE_DISCOVERY         4.0f
#define TOAST_LIFE_UNIQUE            5.5f
#define TOAST_LIFE_HERO              5.0f
#define TOAST_LIFE_OVERDRIVE         4.0f

#endif
