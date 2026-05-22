/* json_mini.h - tokenizer JSON minimal (style jsmn).
 *
 * Parse strict (pas de trailing commas). Output : array de tokens
 * avec type / start / end / size. Le caller navigue dans la string
 * source via start/end et utilise size (nb children) pour iterer
 * objets / arrays.
 *
 * Usage :
 *   JsmTok toks[256];
 *   int n = jsm_parse(src, src_len, toks, 256);
 *   if (n < 0) error;
 *   for (int i = 1; i < n; i++) { ... }
 *
 * Designed pour glTF v2 mais convient pour la plupart des JSON. */

#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <stdbool.h>

typedef enum {
    JSM_UNDEFINED = 0,
    JSM_OBJECT,
    JSM_ARRAY,
    JSM_STRING,
    JSM_PRIMITIVE,   /* number, true, false, null */
} JsmType;

typedef struct {
    JsmType type;
    int     start, end;   /* offsets dans la source */
    int     size;         /* nb children (object/array) ou 0 */
} JsmTok;

/* Renvoie le nb de tokens parses, ou -1 si erreur. */
int  jsm_parse(const char *src, int src_len, JsmTok *toks, int max_toks);

/* Helpers : compare une string token avec un C-string. */
bool jsm_streq(const char *src, const JsmTok *t, const char *cstr);

/* Convert primitive token en double / int. */
double jsm_to_double(const char *src, const JsmTok *t);
int    jsm_to_int   (const char *src, const JsmTok *t);

/* Trouve une cle dans un objet. Renvoie l'index du VALUE token,
 * ou -1 si non trouve. obj_tok_idx = index du token de type JSM_OBJECT. */
int    jsm_obj_find (const char *src, const JsmTok *toks, int obj_idx,
                     const char *key);

/* Saute le sous-arbre a partir du token idx ; renvoie l'index apres. */
int    jsm_skip     (const JsmTok *toks, int idx);

#endif

#ifdef JSON_MINI_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

static JsmTok *jsm__alloc(JsmTok *toks, int max, int *out_idx) {
    if (*out_idx >= max) return NULL;
    JsmTok *t = &toks[*out_idx];
    (*out_idx)++;
    t->type = JSM_UNDEFINED;
    t->start = t->end = -1;
    t->size = 0;
    return t;
}

static int jsm__parse_value(const char *src, int len, int *pos,
                             JsmTok *toks, int max_toks, int *tok_idx,
                             int parent);

static int jsm__skip_ws(const char *src, int len, int pos) {
    while (pos < len) {
        char c = src[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') pos++;
        else break;
    }
    return pos;
}

static int jsm__parse_string(const char *src, int len, int *pos,
                              JsmTok *toks, int max_toks, int *tok_idx) {
    int start = *pos + 1;
    int p = start;
    while (p < len) {
        char c = src[p];
        if (c == '"') {
            JsmTok *t = jsm__alloc(toks, max_toks, tok_idx);
            if (!t) return -1;
            t->type = JSM_STRING;
            t->start = start;
            t->end = p;
            *pos = p + 1;
            return 0;
        }
        if (c == '\\' && p + 1 < len) p += 2;       /* skip escape */
        else p++;
    }
    return -1;
}

static int jsm__parse_primitive(const char *src, int len, int *pos,
                                  JsmTok *toks, int max_toks, int *tok_idx) {
    int start = *pos;
    int p = start;
    while (p < len) {
        char c = src[p];
        if (c == ',' || c == ']' || c == '}' || c == ' ' || c == '\t' ||
            c == '\r' || c == '\n') break;
        p++;
    }
    if (p == start) return -1;
    JsmTok *t = jsm__alloc(toks, max_toks, tok_idx);
    if (!t) return -1;
    t->type = JSM_PRIMITIVE;
    t->start = start;
    t->end = p;
    *pos = p;
    return 0;
}

static int jsm__parse_object(const char *src, int len, int *pos,
                              JsmTok *toks, int max_toks, int *tok_idx,
                              int parent) {
    (void)parent;
    JsmTok *obj = jsm__alloc(toks, max_toks, tok_idx);
    if (!obj) return -1;
    int self_idx = (int)(obj - toks);
    obj->type = JSM_OBJECT;
    obj->start = *pos;
    obj->size = 0;
    (*pos)++;     /* skip { */
    *pos = jsm__skip_ws(src, len, *pos);
    if (*pos < len && src[*pos] == '}') {
        obj->end = *pos + 1;
        (*pos)++;
        return 0;
    }
    while (*pos < len) {
        *pos = jsm__skip_ws(src, len, *pos);
        if (*pos >= len || src[*pos] != '"') return -1;
        /* key */
        if (jsm__parse_string(src, len, pos, toks, max_toks, tok_idx) < 0) return -1;
        obj->size++;
        *pos = jsm__skip_ws(src, len, *pos);
        if (*pos >= len || src[*pos] != ':') return -1;
        (*pos)++;
        *pos = jsm__skip_ws(src, len, *pos);
        /* value */
        if (jsm__parse_value(src, len, pos, toks, max_toks, tok_idx, self_idx) < 0) return -1;
        *pos = jsm__skip_ws(src, len, *pos);
        if (*pos < len && src[*pos] == ',') { (*pos)++; continue; }
        if (*pos < len && src[*pos] == '}') {
            toks[self_idx].end = *pos + 1;
            (*pos)++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int jsm__parse_array(const char *src, int len, int *pos,
                             JsmTok *toks, int max_toks, int *tok_idx,
                             int parent) {
    JsmTok *arr = jsm__alloc(toks, max_toks, tok_idx);
    if (!arr) return -1;
    int self_idx = (int)(arr - toks);
    arr->type = JSM_ARRAY;
    arr->start = *pos;
    arr->size = 0;
    (*pos)++;     /* skip [ */
    *pos = jsm__skip_ws(src, len, *pos);
    if (*pos < len && src[*pos] == ']') {
        arr->end = *pos + 1;
        (*pos)++;
        return 0;
    }
    while (*pos < len) {
        *pos = jsm__skip_ws(src, len, *pos);
        if (jsm__parse_value(src, len, pos, toks, max_toks, tok_idx, self_idx) < 0) return -1;
        arr->size++;
        *pos = jsm__skip_ws(src, len, *pos);
        if (*pos < len && src[*pos] == ',') { (*pos)++; continue; }
        if (*pos < len && src[*pos] == ']') {
            toks[self_idx].end = *pos + 1;
            (*pos)++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int jsm__parse_value(const char *src, int len, int *pos,
                             JsmTok *toks, int max_toks, int *tok_idx,
                             int parent) {
    (void)parent;
    if (*pos >= len) return -1;
    char c = src[*pos];
    if (c == '{') return jsm__parse_object(src, len, pos, toks, max_toks, tok_idx, *tok_idx);
    if (c == '[') return jsm__parse_array (src, len, pos, toks, max_toks, tok_idx, *tok_idx);
    if (c == '"') return jsm__parse_string(src, len, pos, toks, max_toks, tok_idx);
    return jsm__parse_primitive(src, len, pos, toks, max_toks, tok_idx);
}

int jsm_parse(const char *src, int src_len, JsmTok *toks, int max_toks) {
    int pos = 0, tok_idx = 0;
    pos = jsm__skip_ws(src, src_len, pos);
    if (jsm__parse_value(src, src_len, &pos, toks, max_toks, &tok_idx, -1) < 0) return -1;
    return tok_idx;
}

bool jsm_streq(const char *src, const JsmTok *t, const char *cstr) {
    if (t->type != JSM_STRING) return false;
    int len = t->end - t->start;
    int clen = (int)strlen(cstr);
    if (len != clen) return false;
    return memcmp(src + t->start, cstr, len) == 0;
}

double jsm_to_double(const char *src, const JsmTok *t) {
    char buf[64];
    int len = t->end - t->start;
    if (len <= 0 || len >= (int)sizeof(buf)) return 0.0;
    memcpy(buf, src + t->start, len);
    buf[len] = 0;
    return strtod(buf, NULL);
}

int jsm_to_int(const char *src, const JsmTok *t) {
    return (int)jsm_to_double(src, t);
}

int jsm_skip(const JsmTok *toks, int idx) {
    const JsmTok *t = &toks[idx];
    if (t->type != JSM_OBJECT && t->type != JSM_ARRAY) return idx + 1;
    int i = idx + 1;
    int children = (t->type == JSM_OBJECT) ? t->size * 2 : t->size;
    for (int k = 0; k < children; k++) i = jsm_skip(toks, i);
    return i;
}

int jsm_obj_find(const char *src, const JsmTok *toks, int obj_idx,
                  const char *key) {
    const JsmTok *obj = &toks[obj_idx];
    if (obj->type != JSM_OBJECT) return -1;
    int i = obj_idx + 1;
    for (int k = 0; k < obj->size; k++) {
        const JsmTok *kt = &toks[i];
        if (jsm_streq(src, kt, key)) return i + 1;
        i++;                          /* skip key */
        i = jsm_skip(toks, i);        /* skip value */
    }
    return -1;
}

#endif
