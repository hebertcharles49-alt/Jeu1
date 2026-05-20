/*
 * heroes.c - identite des classes de heros + nom de sous-classe (couple
 * d'armes). Extrait de combat.c pour decoupler les donnees de classe du
 * code de combat proprement dit.
 */
#include "game.h"

void hero_palette(HeroClass h, uint32_t *cape, uint32_t *tunic) {
    *cape = 0x303040FF; *tunic = 0x6A4A2AFF;
    switch (h) {
        case HERO_GUERRIER:  *cape = 0x802020FF; *tunic = 0x707080FF; break;
        case HERO_VOLEUR:    *cape = 0x305030FF; *tunic = 0x202028FF; break;
        case HERO_MAGE:      *cape = 0x402070FF; *tunic = 0x6040A0FF; break;
        case HERO_BERSERKER: *cape = 0x202020FF; *tunic = 0x803020FF; break;
        case HERO_PALADIN:   *cape = 0xFFD040FF; *tunic = 0xC0C0D0FF; break;
        case HERO_DRUIDE:    *cape = 0x305020FF; *tunic = 0x60804030; break;
        case HERO_ASSASSIN:  *cape = 0x101018FF; *tunic = 0x301030FF; break;
        case HERO_RANGER:    *cape = 0x405028FF; *tunic = 0x806030FF; break;
        case HERO_TEMPLIER:  *cape = 0xC0C0C8FF; *tunic = 0x808088FF; break;
        case HERO_NECROMANT: *cape = 0x202840FF; *tunic = 0x303060FF; break;
        default: break;
    }
}

const char *hero_name(HeroClass h) {
    switch (h) {
        case HERO_GUERRIER:  return "Guerrier";
        case HERO_VOLEUR:    return "Voleur";
        case HERO_MAGE:      return "Mage";
        case HERO_BERSERKER: return "Berserker";
        case HERO_PALADIN:   return "Paladin";
        case HERO_DRUIDE:    return "Druide";
        case HERO_ASSASSIN:  return "Assassin";
        case HERO_RANGER:    return "Ranger";
        case HERO_TEMPLIER:  return "Templier";
        case HERO_NECROMANT: return "Necromant";
        default:             return "?";
    }
}

const char *hero_desc(HeroClass h) {
    switch (h) {
        case HERO_GUERRIER:  return "+25 PV   +15% degats melee";
        case HERO_VOLEUR:    return "+20 vitesse   dash plus long";
        case HERO_MAGE:      return "+30% degats elementaires   PV bas";
        case HERO_BERSERKER: return "+8% vol de vie   +20% degats   fragile";
        case HERO_PALADIN:   return "+2 armure   regen 1 PV/s";
        case HERO_DRUIDE:    return "+50% degats elem   -30% degats melee";
        case HERO_ASSASSIN:  return "+25% crit   x2 crit dmg   -25 PV max";
        case HERO_RANGER:    return "+40% degats distance   -25% degats melee";
        case HERO_TEMPLIER:  return "+3 armure   +15 PV   -15% atk speed";
        case HERO_NECROMANT: return "+15% vol de vie   -1 regen/s   +20% dmg vide";
        default: return "";
    }
}

const char *enemy_name(EnemyKind k) {
    switch (k) {
        case EK_ZOMBIE:  return "Zombie";
        case EK_BANDIT:  return "Bandit";
        case EK_DEMON:   return "Demon";
        case EK_SLIME:   return "Slime";
        case EK_RAT:     return "Rat";
        case EK_GHOST:   return "Spectre";
        case EK_CHARGER: return "Taureau";
        case EK_MAGE:        return "Sorcier";
        case EK_HEALER:      return "Hierophante";
        case EK_BUFFER:      return "Totem";
        case EK_NECROMANCER: return "Necromancien";
        case EK_BOSS:        return "Boss";
        default: return "?";
    }
}

/* Classification thematique pour les trinkets specifiques. */
EnemyCategory enemy_category(EnemyKind k) {
    switch (k) {
        case EK_SLIME:
        case EK_RAT:
        case EK_CHARGER:     return ENEMY_CAT_BEAST;
        case EK_DEMON:
        case EK_BUFFER:      return ENEMY_CAT_DEMON;
        case EK_ZOMBIE:
        case EK_GHOST:       return ENEMY_CAT_UNDEAD;
        case EK_BANDIT:
        case EK_HEALER:
        case EK_MAGE:
        case EK_NECROMANCER: return ENEMY_CAT_HUMAN;
        case EK_BOSS:        return ENEMY_CAT_BOSS;
        default:             return ENEMY_CAT_HUMAN;
    }
}

const char *enemy_category_name(EnemyCategory c) {
    switch (c) {
        case ENEMY_CAT_BEAST:  return "Bete";
        case ENEMY_CAT_DEMON:  return "Demon";
        case ENEMY_CAT_UNDEAD: return "Mort-vivant";
        case ENEMY_CAT_HUMAN:  return "Humain";
        case ENEMY_CAT_BOSS:   return "Boss";
        default:               return "?";
    }
}

/* nom thematique du boss en fonction du biome / variant. variant est
 * tire de boss_for_floor (cf entities.c) et correspond 1:1 au biome. */
const char *boss_title_for_variant(int variant) {
    switch (variant) {
        case 0: return "Necropante";
        case 1: return "Geant de Pierre";
        case 2: return "Hydre Putride";
        case 3: return "Forgeron des Enfers";
        case 4: return "Avatar Divin";
        default: return "Gardien";
    }
}

/* sous-classe en fonction de la combinaison de 2 armes */
const char *subclass_name(WeaponKind a, WeaponKind b) {
    if (a > b) { WeaponKind t = a; a = b; b = t; }
    if (a == W_FISTS && b == W_FISTS) return "Pugiliste";
    if (a == W_FISTS) {
        switch (b) {
            case W_SWORD:  return "Spadassin";
            case W_SHIELD: return "Sentinelle";
            case W_BOW:    return "Archer";
            case W_WAND:   return "Initie";
            case W_AXE:    return "Bucheron";
            default: break;
        }
    }
    if (a == W_SWORD  && b == W_SHIELD) return "Garde";
    if (a == W_SWORD  && b == W_BOW)    return "Eclaireur";
    if (a == W_SWORD  && b == W_WAND)   return "Sorcelame";
    if (a == W_SWORD  && b == W_AXE)    return "Bretteur";
    if (a == W_SHIELD && b == W_BOW)    return "Sentinelle Royale";
    if (a == W_SHIELD && b == W_WAND)   return "Templier";
    if (a == W_SHIELD && b == W_AXE)    return "Croise";
    if (a == W_BOW    && b == W_WAND)   return "Archimage";
    if (a == W_BOW    && b == W_AXE)    return "Traqueur";
    if (a == W_WAND   && b == W_AXE)    return "Chamane";
    if (a == b) {
        switch (a) {
            case W_SWORD:  return "Duelliste";
            case W_SHIELD: return "Citadelle";
            case W_BOW:    return "Tireur d'Elite";
            case W_WAND:   return "Archonte";
            case W_AXE:    return "Boucher";
            default: break;
        }
    }
    return "Aventurier";
}
