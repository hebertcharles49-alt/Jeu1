/* gltf_loader.h - charge un .glb (glTF 2.0 binary) en static mesh.
 *
 * PHASE A : extraction static mesh (POSITION + NORMAL, indexes).
 * Output au format compatible obj_loader (verts 9f interleave).
 *
 * PHASE B (futur commit) : skinning + animation.
 *   - parsing JOINTS_0 + WEIGHTS_0
 *   - parsing skin (joints + inverseBindMatrices)
 *   - parsing animations (channels + samplers)
 *   - bone matrices runtime + skinning vertex shader
 *
 * Usage :
 *   #define GLTF_LOADER_IMPLEMENTATION
 *   #include "gltf_loader.h"
 *   GltfMesh m;
 *   if (gltf_load_glb("mods/meshes/perso.glb", &m)) {
 *       gfx_mesh_upload(g->renderer, &mygfx, m.verts, m.vert_count);
 *       gltf_free(&m);
 *   }
 *
 * Format .glb supporte :
 *   header 12 bytes (magic 0x46546C67, version 2)
 *   chunk JSON (type 0x4E4F534A)
 *   chunk BIN (type 0x004E4942)
 * .gltf separate (+.bin) NON supporte ce coup-ci (.glb couvre 99%
 * des exports Blender).
 *
 * Composantes supportees :
 *   - POSITION (VEC3 float)
 *   - NORMAL (VEC3 float) -- calcule par face si absent
 *   - indices (UNSIGNED_SHORT ou UNSIGNED_INT)
 *   - le premier mesh / premiere primitive du file. */

#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include <stdbool.h>

typedef struct {
    float   *verts;        /* pos 3f + normal 3f + color 3f (interleave) */
    int      vert_count;
} GltfMesh;

bool gltf_load_glb (const char *path, GltfMesh *out);
void gltf_free     (GltfMesh *m);

#endif

#ifdef GLTF_LOADER_IMPLEMENTATION

#include "json_mini.h"
#define JSON_MINI_IMPLEMENTATION
#include "json_mini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* glTF accessor componentType */
#define GLTF_CT_BYTE   5120
#define GLTF_CT_UBYTE  5121
#define GLTF_CT_SHORT  5122
#define GLTF_CT_USHORT 5123
#define GLTF_CT_UINT   5125
#define GLTF_CT_FLOAT  5126

static int gltf__ct_size(int ct) {
    switch (ct) {
        case GLTF_CT_BYTE:
        case GLTF_CT_UBYTE:  return 1;
        case GLTF_CT_SHORT:
        case GLTF_CT_USHORT: return 2;
        case GLTF_CT_UINT:
        case GLTF_CT_FLOAT:  return 4;
    }
    return 0;
}
static int gltf__type_components(const char *src, const JsmTok *t) {
    /* "SCALAR" / "VEC2" / "VEC3" / "VEC4" / "MAT4" */
    if (jsm_streq(src, t, "SCALAR")) return 1;
    if (jsm_streq(src, t, "VEC2"))   return 2;
    if (jsm_streq(src, t, "VEC3"))   return 3;
    if (jsm_streq(src, t, "VEC4"))   return 4;
    if (jsm_streq(src, t, "MAT4"))   return 16;
    return 0;
}

/* Accesseur resolu : pointe dans le buffer + count + comp count + comp size. */
typedef struct {
    const unsigned char *data;
    int count;
    int comps;     /* 1 / 2 / 3 / 4 / 16 */
    int ct;        /* componentType */
    int stride;    /* en bytes par element */
} Accessor;

static bool gltf__resolve_accessor(const char *src, const JsmTok *toks,
                                    int accessors_arr, int buffer_views_arr,
                                    const unsigned char *bin,
                                    int accessor_idx, Accessor *out) {
    if (accessors_arr < 0) return false;
    const JsmTok *arr = &toks[accessors_arr];
    if (arr->type != JSM_ARRAY) return false;
    /* nth element */
    int i = accessors_arr + 1;
    for (int k = 0; k < accessor_idx; k++) i = jsm_skip(toks, i);
    int acc_idx = i;
    int bv      = -1;
    int byte_off_acc = 0;
    int count = 0;
    int ct = 0;
    int comps = 0;
    /* parse accessors[acc] fields */
    int bv_t = jsm_obj_find(src, toks, acc_idx, "bufferView");
    if (bv_t >= 0) bv = jsm_to_int(src, &toks[bv_t]);
    int bo_t = jsm_obj_find(src, toks, acc_idx, "byteOffset");
    if (bo_t >= 0) byte_off_acc = jsm_to_int(src, &toks[bo_t]);
    int ct_t = jsm_obj_find(src, toks, acc_idx, "componentType");
    if (ct_t >= 0) ct = jsm_to_int(src, &toks[ct_t]);
    int cn_t = jsm_obj_find(src, toks, acc_idx, "count");
    if (cn_t >= 0) count = jsm_to_int(src, &toks[cn_t]);
    int ty_t = jsm_obj_find(src, toks, acc_idx, "type");
    if (ty_t >= 0) comps = gltf__type_components(src, &toks[ty_t]);
    if (bv < 0 || count <= 0 || comps <= 0 || ct <= 0) return false;
    /* resolve bufferView */
    int bvi = buffer_views_arr + 1;
    for (int k = 0; k < bv; k++) bvi = jsm_skip(toks, bvi);
    int byte_off_bv = 0, byte_len = 0, stride = 0;
    int bot = jsm_obj_find(src, toks, bvi, "byteOffset");
    if (bot >= 0) byte_off_bv = jsm_to_int(src, &toks[bot]);
    int blt = jsm_obj_find(src, toks, bvi, "byteLength");
    if (blt >= 0) byte_len = jsm_to_int(src, &toks[blt]);
    int bst = jsm_obj_find(src, toks, bvi, "byteStride");
    if (bst >= 0) stride = jsm_to_int(src, &toks[bst]);
    if (byte_len <= 0) return false;
    int comp_sz = gltf__ct_size(ct);
    int elem_sz = comp_sz * comps;
    if (stride == 0) stride = elem_sz;
    out->data   = bin + byte_off_bv + byte_off_acc;
    out->count  = count;
    out->comps  = comps;
    out->ct     = ct;
    out->stride = stride;
    return true;
}

static float gltf__read_float(const unsigned char *p, int ct) {
    if (ct == GLTF_CT_FLOAT) {
        float f; memcpy(&f, p, 4); return f;
    }
    /* fallback : treat as float ; glTF position should be FLOAT */
    return 0.f;
}

static unsigned int gltf__read_uint(const unsigned char *p, int ct) {
    if (ct == GLTF_CT_USHORT) return (unsigned)p[0] | ((unsigned)p[1] << 8);
    if (ct == GLTF_CT_UINT)   return (unsigned)p[0] | ((unsigned)p[1] << 8)
                                   | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    if (ct == GLTF_CT_UBYTE)  return p[0];
    return 0;
}

bool gltf_load_glb(const char *path, GltfMesh *out) {
    if (!path || !out) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz < 28) { fclose(f); return false; }
    unsigned char *buf = (unsigned char *)malloc((size_t)fsz);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)fsz, f);
    fclose(f);
    if (rd != (size_t)fsz) { free(buf); return false; }
    /* header */
    if (buf[0]!='g' || buf[1]!='l' || buf[2]!='T' || buf[3]!='F') {
        free(buf); return false;
    }
    /* version a buf[4..7], total length a buf[8..11] */
    int pos = 12;
    /* JSON chunk */
    if (pos + 8 > fsz) { free(buf); return false; }
    unsigned int json_len = (unsigned)buf[pos] | ((unsigned)buf[pos+1] << 8)
                          | ((unsigned)buf[pos+2] << 16) | ((unsigned)buf[pos+3] << 24);
    /* type at pos+4 should be "JSON" */
    if (memcmp(buf + pos + 4, "JSON", 4) != 0) { free(buf); return false; }
    const char *json_start = (const char *)(buf + pos + 8);
    pos += 8 + (int)json_len;
    if (pos + 8 > fsz) { free(buf); return false; }
    /* BIN chunk */
    unsigned int bin_len = (unsigned)buf[pos] | ((unsigned)buf[pos+1] << 8)
                         | ((unsigned)buf[pos+2] << 16) | ((unsigned)buf[pos+3] << 24);
    if (memcmp(buf + pos + 4, "BIN\0", 4) != 0) { free(buf); return false; }
    const unsigned char *bin = buf + pos + 8;
    (void)bin_len;
    /* Parse JSON */
    int max_toks = 4096;
    JsmTok *toks = (JsmTok *)malloc(sizeof(JsmTok) * max_toks);
    if (!toks) { free(buf); return false; }
    int ntok = jsm_parse(json_start, (int)json_len, toks, max_toks);
    if (ntok < 0) { free(toks); free(buf); return false; }
    /* Find meshes / accessors / bufferViews */
    int meshes_arr = jsm_obj_find(json_start, toks, 0, "meshes");
    int access_arr = jsm_obj_find(json_start, toks, 0, "accessors");
    int bv_arr     = jsm_obj_find(json_start, toks, 0, "bufferViews");
    if (meshes_arr < 0 || access_arr < 0 || bv_arr < 0) {
        free(toks); free(buf); return false;
    }
    if (toks[meshes_arr].type != JSM_ARRAY || toks[meshes_arr].size < 1) {
        free(toks); free(buf); return false;
    }
    int mesh0 = meshes_arr + 1;     /* premier mesh */
    int prims = jsm_obj_find(json_start, toks, mesh0, "primitives");
    if (prims < 0 || toks[prims].size < 1) { free(toks); free(buf); return false; }
    int prim0 = prims + 1;
    int attribs = jsm_obj_find(json_start, toks, prim0, "attributes");
    if (attribs < 0) { free(toks); free(buf); return false; }
    int pos_acc_t = jsm_obj_find(json_start, toks, attribs, "POSITION");
    int nor_acc_t = jsm_obj_find(json_start, toks, attribs, "NORMAL");
    int idx_acc_t = jsm_obj_find(json_start, toks, prim0,   "indices");
    if (pos_acc_t < 0) { free(toks); free(buf); return false; }
    int pos_acc = jsm_to_int(json_start, &toks[pos_acc_t]);
    int nor_acc = (nor_acc_t >= 0) ? jsm_to_int(json_start, &toks[nor_acc_t]) : -1;
    int idx_acc = (idx_acc_t >= 0) ? jsm_to_int(json_start, &toks[idx_acc_t]) : -1;
    /* Resolve */
    Accessor a_pos, a_nor, a_idx;
    if (!gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, pos_acc, &a_pos)) {
        free(toks); free(buf); return false;
    }
    bool has_normal = (nor_acc >= 0) &&
        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, nor_acc, &a_nor);
    bool has_indices = (idx_acc >= 0) &&
        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, idx_acc, &a_idx);
    /* Construit le mesh : si indices, on materialise un vertex par index. */
    int vert_count = has_indices ? a_idx.count : a_pos.count;
    /* vert_count doit etre un multiple de 3 (triangles) */
    if (vert_count < 3) { free(toks); free(buf); return false; }
    out->verts = (float *)malloc(sizeof(float) * 9 * vert_count);
    if (!out->verts) { free(toks); free(buf); return false; }
    out->vert_count = vert_count;
    for (int i = 0; i < vert_count; i++) {
        int src_v = has_indices
            ? (int)gltf__read_uint(a_idx.data + i * a_idx.stride, a_idx.ct)
            : i;
        if (src_v < 0 || src_v >= a_pos.count) src_v = 0;
        const unsigned char *pp = a_pos.data + src_v * a_pos.stride;
        float px = gltf__read_float(pp,     a_pos.ct);
        float py = gltf__read_float(pp + 4, a_pos.ct);
        float pz = gltf__read_float(pp + 8, a_pos.ct);
        float nx = 0, ny = 1, nz = 0;
        if (has_normal && src_v < a_nor.count) {
            const unsigned char *np = a_nor.data + src_v * a_nor.stride;
            nx = gltf__read_float(np,     a_nor.ct);
            ny = gltf__read_float(np + 4, a_nor.ct);
            nz = gltf__read_float(np + 8, a_nor.ct);
        }
        float *o = &out->verts[i * 9];
        o[0]=px; o[1]=py; o[2]=pz;
        o[3]=nx; o[4]=ny; o[5]=nz;
        o[6]=1.f; o[7]=1.f; o[8]=1.f;
    }
    /* Si pas de normales, calculer par face (groupes de 3 verts) */
    if (!has_normal) {
        for (int i = 0; i + 2 < vert_count; i += 3) {
            float *v0 = &out->verts[(i+0)*9];
            float *v1 = &out->verts[(i+1)*9];
            float *v2 = &out->verts[(i+2)*9];
            float ex = v1[0]-v0[0], ey = v1[1]-v0[1], ez = v1[2]-v0[2];
            float fx = v2[0]-v0[0], fy = v2[1]-v0[1], fz = v2[2]-v0[2];
            float nx = ey*fz - ez*fy;
            float ny = ez*fx - ex*fz;
            float nz = ex*fy - ey*fx;
            float ln = sqrtf(nx*nx + ny*ny + nz*nz) + 1e-6f;
            nx/=ln; ny/=ln; nz/=ln;
            for (int k = 0; k < 3; k++) {
                float *o = &out->verts[(i+k)*9];
                o[3]=nx; o[4]=ny; o[5]=nz;
            }
        }
    }
    free(toks);
    free(buf);
    return true;
}

void gltf_free(GltfMesh *m) {
    if (!m) return;
    free(m->verts);
    m->verts = NULL;
    m->vert_count = 0;
}

#endif
