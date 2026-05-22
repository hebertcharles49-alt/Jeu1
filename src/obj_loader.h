/* obj_loader.h - parser OBJ minimal (compatible Blender export).
 *
 * Usage :
 *   #define OBJ_LOADER_IMPLEMENTATION
 *   #include "obj_loader.h"
 *
 *   ObjMesh mesh;
 *   if (obj_load_file("models/sphere.obj", &mesh)) {
 *       use mesh.verts (pos 3f, normal 3f, color 3f per vertex)
 *       use mesh.vert_count (vertex count, triangulated)
 *       obj_free(&mesh);
 *   }
 *
 * Format supporte :
 *   v X Y Z          (positions)
 *   vn X Y Z         (normales)
 *   f v/vt/vn ...    (faces, triangulees automatiquement)
 *   f v//vn ...      (sans uv)
 *   f v ...          (positions seules ; normales calculees)
 *   #                (comments)
 *
 * Non supporte (ignore silencieusement) :
 *   - vt (UV) -> les UVs ne sont pas remontees (pas de texture dans le pipeline)
 *   - mtllib/usemtl -> couleur fixe par defaut
 *   - groupes / objets
 *   - splines / surfaces */

#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <stdbool.h>

typedef struct {
    float   *verts;        /* interleave : pos (3f), normal (3f), color (3f) */
    int      vert_count;   /* nb vertices (triangulated, multiple de 3) */
} ObjMesh;

bool obj_load_file(const char *path, ObjMesh *out);
bool obj_load_string(const char *data, ObjMesh *out);
void obj_free(ObjMesh *m);

#endif

#ifdef OBJ_LOADER_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { float x, y, z; } objv3_t;

static char *obj__read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = 0;
    return buf;
}

bool obj_load_string(const char *data, ObjMesh *out) {
    if (!data || !out) return false;
    /* Pass 1 : compte v / vn / faces (triangulees) pour allouer. */
    int n_pos = 0, n_nrm = 0, n_face_tri = 0;
    const char *p = data;
    while (*p) {
        if (p[0] == 'v' && p[1] == ' ') n_pos++;
        else if (p[0] == 'v' && p[1] == 'n') n_nrm++;
        else if (p[0] == 'f' && p[1] == ' ') {
            /* compte les sommets de cette face pour trianguler en fan */
            const char *e = p; int nv = 0;
            while (*e && *e != '\n') {
                if (*e == ' ' || *e == '\t') {
                    /* skip whitespaces, look ahead for a digit */
                    while (*e == ' ' || *e == '\t') e++;
                    if (*e && *e != '\n' && *e != '#') { nv++; }
                    while (*e && *e != ' ' && *e != '\t' && *e != '\n') e++;
                } else e++;
            }
            if (nv >= 3) n_face_tri += (nv - 2);   /* fan triangulation */
        }
        /* skip line */
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    if (n_pos == 0 || n_face_tri == 0) return false;
    objv3_t *pos = (objv3_t *)malloc(sizeof(objv3_t) * n_pos);
    objv3_t *nrm = (objv3_t *)malloc(sizeof(objv3_t) * (n_nrm > 0 ? n_nrm : 1));
    if (!pos || !nrm) { free(pos); free(nrm); return false; }
    out->vert_count = n_face_tri * 3;
    out->verts = (float *)malloc((size_t)out->vert_count * 9 * sizeof(float));
    if (!out->verts) { free(pos); free(nrm); return false; }
    /* Pass 2 : parse */
    int ip = 0, in = 0, iv = 0;
    p = data;
    while (*p) {
        if (p[0] == 'v' && p[1] == ' ') {
            objv3_t v;
            sscanf(p + 2, "%f %f %f", &v.x, &v.y, &v.z);
            if (ip < n_pos) pos[ip++] = v;
        } else if (p[0] == 'v' && p[1] == 'n') {
            objv3_t v;
            sscanf(p + 3, "%f %f %f", &v.x, &v.y, &v.z);
            if (in < n_nrm) nrm[in++] = v;
        } else if (p[0] == 'f' && p[1] == ' ') {
            /* Parse face indices. Supporte v, v/vt, v//vn, v/vt/vn. */
            int v_idx[64], n_idx[64], nv = 0;
            const char *q = p + 2;
            while (*q && *q != '\n' && nv < 64) {
                while (*q == ' ' || *q == '\t') q++;
                if (!*q || *q == '\n' || *q == '#') break;
                int vi = 0, ti = 0, ni = 0;
                if (sscanf(q, "%d/%d/%d", &vi, &ti, &ni) == 3) {}
                else if (sscanf(q, "%d//%d", &vi, &ni) == 2) {}
                else if (sscanf(q, "%d/%d", &vi, &ti) == 2) { ni = 0; }
                else if (sscanf(q, "%d", &vi) == 1) { ni = 0; }
                v_idx[nv] = (vi > 0) ? vi - 1 : 0;
                n_idx[nv] = (ni > 0) ? ni - 1 : -1;
                nv++;
                while (*q && *q != ' ' && *q != '\t' && *q != '\n') q++;
            }
            /* Triangulation fan : (0, k, k+1) pour k=1..nv-2 */
            for (int k = 1; k < nv - 1; k++) {
                int idx_tri[3] = { 0, k, k + 1 };
                /* Calculer la normale si pas fournie : produit vec face */
                objv3_t fn = { 0, 1, 0 };
                if (n_idx[0] < 0) {
                    objv3_t a = pos[v_idx[idx_tri[0]]];
                    objv3_t b = pos[v_idx[idx_tri[1]]];
                    objv3_t c = pos[v_idx[idx_tri[2]]];
                    objv3_t e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
                    objv3_t e2 = { c.x - a.x, c.y - a.y, c.z - a.z };
                    fn.x = e1.y * e2.z - e1.z * e2.y;
                    fn.y = e1.z * e2.x - e1.x * e2.z;
                    fn.z = e1.x * e2.y - e1.y * e2.x;
                    float ln = sqrtf(fn.x*fn.x + fn.y*fn.y + fn.z*fn.z) + 1e-6f;
                    fn.x /= ln; fn.y /= ln; fn.z /= ln;
                }
                for (int t = 0; t < 3; t++) {
                    int sv = v_idx[idx_tri[t]];
                    int sn = n_idx[idx_tri[t]];
                    if (sv < 0 || sv >= n_pos) sv = 0;
                    objv3_t v = pos[sv];
                    objv3_t n = (sn >= 0 && sn < n_nrm) ? nrm[sn] : fn;
                    float *o = &out->verts[iv * 9]; iv++;
                    o[0] = v.x; o[1] = v.y; o[2] = v.z;
                    o[3] = n.x; o[4] = n.y; o[5] = n.z;
                    o[6] = 1.f; o[7] = 1.f; o[8] = 1.f;       /* couleur defaut */
                }
            }
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    out->vert_count = iv;
    free(pos); free(nrm);
    return true;
}

bool obj_load_file(const char *path, ObjMesh *out) {
    char *data = obj__read_file(path);
    if (!data) return false;
    bool ok = obj_load_string(data, out);
    free(data);
    return ok;
}

void obj_free(ObjMesh *m) {
    if (!m) return;
    free(m->verts);
    m->verts = NULL;
    m->vert_count = 0;
}

#endif
