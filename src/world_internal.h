/*
 * world_internal.h - helpers partages entre les fichiers gameplay
 * (entities.c / player.c / enemies.c / world.c). Pas vise pour
 * l'inclusion depuis le rendu ou les menus.
 */
#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H
#include "game.h"

/* collision tile : aabb 2*r autour de (x, y) intersecte un T_VOID/T_WALL */
bool aabb_solid(Game *g, float x, float y, float r);

#endif
