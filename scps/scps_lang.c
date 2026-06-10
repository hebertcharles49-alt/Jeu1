/*
 * scps_lang.c — les tables compilées + tr/tr_fmt (voir scps_lang.h).
 *
 * LE CONTRAT DU COMPILATEUR : TABLE_FR sort de la MÊME X-liste que l'enum
 * (divergence impossible) ; TABLE_EN se construit positionnellement sur sa
 * liste jumelle et l'assert de taille (C99, typedef tableau négatif) casse le
 * build si une ligne manque ou déborde. Pas de clé fantôme, pas de fichier
 * externe : `strings` de l'exe contient les deux langues.
 */
#include "scps_lang.h"
#include <stdarg.h>
#include <string.h>

#include "strings_en.h"

static const char *TABLE_FR[] = {
#define X(id, txt) txt,
    SCPS_STRINGS(X)
#undef X
};
static const char *TABLE_EN[] = {
#define X(id, txt) txt,
    SCPS_STRINGS_EN(X)
#undef X
};
/* complétude vérifiée À LA COMPILATION : retirer une ligne → taille négative */
typedef char scps_assert_fr_complete[(sizeof TABLE_FR/sizeof *TABLE_FR==STR__COUNT)?1:-1];
typedef char scps_assert_en_complete[(sizeof TABLE_EN/sizeof *TABLE_EN==STR__COUNT)?1:-1];

static Lang g_lang = LANG_FR;   /* la référence — un binaire headless n'y touche jamais */

void lang_set(Lang l){ if (l>=0 && l<LANG_COUNT) g_lang=l; }
Lang lang_get(void){ return g_lang; }
const char *lang_name(Lang l){
    return (l==LANG_EN) ? "English" : "Français";
}

const char *tr(StrId id){
    if (id<0 || id>=STR__COUNT) return "?";
    const char *s = (g_lang==LANG_EN) ? TABLE_EN[id] : TABLE_FR[id];
    return s ? s : "?";
}

const char *tr_band(StrId base, int idx, int count){
    if (idx<0 || idx>=count) return "?";
    return tr((StrId)(base+idx));
}

/* Substitution positionnelle {0}..{9}, bornée, sans allocation. On lit du
 * va_list exactement (max_index+1) arguments — le format fait le contrat,
 * dans TOUTES les langues (une clé EN "{1}…{0}" lit deux arguments aussi). */
void tr_fmt(char *out, size_t n, StrId id, ...){
    if (!out || n==0) return;
    const char *fmt = tr(id);
    const char *args[10]={0};
    int hi=-1;
    for (const char *p=fmt; *p; p++)
        if (p[0]=='{' && p[1]>='0' && p[1]<='9' && p[2]=='}'){
            int k=p[1]-'0'; if (k>hi) hi=k;
        }
    { va_list ap; va_start(ap, id);
      for (int k=0;k<=hi && k<10;k++){
          const char *a=va_arg(ap, const char*);
          args[k]=a?a:"";
      }
      va_end(ap); }
    size_t o=0;
    for (const char *p=fmt; *p && o+1<n; ){
        if (p[0]=='{' && p[1]>='0' && p[1]<='9' && p[2]=='}'){
            const char *a=args[p[1]-'0']; if (!a) a="";
            while (*a && o+1<n) out[o++]=*a++;
            p+=3;
        } else out[o++]=*p++;
    }
    out[o]='\0';
}
