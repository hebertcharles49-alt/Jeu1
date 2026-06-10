#ifndef SCPS_LANG_H
#define SCPS_LANG_H
/*
 * scps_lang.h — LOCALISATION COMPILÉE (brief table de chaînes)
 *
 * Doctrine : binaire-unique-zéro-asset. Les langues sont des TABLES compilées
 * dans l'exe, commutables au runtime (lang_set + redraw — aucune chaîne n'est
 * cachée dans un état persistant). Le français est la langue de référence ;
 * l'anglais est une table jumelle née copie du FR.
 *
 * Le contrat du COMPILATEUR : strings_ids.h est l'unique liste (X-macro) ;
 * chaque table couvre STR__COUNT par initialiseurs désignés — une entrée
 * manquante ou orpheline casse le build. Pas de clé fantôme, pas de runtime
 * check, pas de fichier externe.
 *
 * PÉRIMÈTRE (clôture, cf. CLAUDE.md) : seules les surfaces FACE-JOUEUR se
 * localisent (viewer, membrane readout, shell, previews). chronicle/econ_scan/
 * batch/télémétrie/commentaires : français pour toujours — l'outillage de
 * l'ingénieur, pas le jeu. Le défaut est LANG_FR : un binaire headless qui
 * n'appelle jamais lang_set est octet-pour-octet inchangé.
 */
#include <stddef.h>
#include "strings_ids.h"

typedef enum {
#define X(id, txt) id,
    SCPS_STRINGS(X)
#undef X
    STR__COUNT
} StrId;

typedef enum { LANG_FR = 0, LANG_EN, LANG_COUNT } Lang;

const char *tr(StrId id);
void lang_set(Lang l);
Lang lang_get(void);
const char *lang_name(Lang l);   /* « Français » / « English » (face-joueur) */

/* Une PLAGE d'ids (mots de bande, pages de tuto) : base + idx, borné. */
const char *tr_band(StrId base, int idx, int count);

/* Chaînes PARAMÉTRÉES : emplacements POSITIONNELS {0}..{9} — l'ordre des mots
 * n'est pas universel (l'anglais peut inverser sans toucher l'appelant).
 * Substitution bornée, sans allocation. Les arguments sont des chaînes ; le
 * site d'appel en passe autant que le PLUS GRAND indice utilisé par la clé
 * (dans TOUTES les langues). Pluriels : deux clés (STR_X_UN/STR_X_PLUSIEURS)
 * là où c'est nécessaire, et seulement là. */
void tr_fmt(char *out, size_t n, StrId id, ...);

#endif /* SCPS_LANG_H */
