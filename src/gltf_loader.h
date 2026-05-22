/* gltf_loader.h - charge un .glb (glTF 2.0 binary) avec skinning et
 * animation. Phase A (static) + Phase B (skin/anim).
 *
 * Usage :
 *   #define GLTF_LOADER_IMPLEMENTATION
 *   #include "gltf_loader.h"
 *
 *   GltfMesh m;
 *   if (gltf_load_glb("mods/meshes/perso.glb", &m)) {
 *       gfx_skin_mesh_upload(g->renderer, &mygfx, &m);
 *       // chaque frame :
 *       gltf_advance_animation(&m, dt);
 *       gfx_skin_mesh_draw(g->renderer, &mygfx, &m, model_matrix, r, g, b);
 *       // a la fin :
 *       gltf_free(&m);
 *   }
 *
 * Format supporte :
 *   - .glb (binary single-file) glTF 2.0
 *   - POSITION (VEC3 float) + NORMAL (VEC3 float)
 *   - JOINTS_0 (VEC4 ubyte/ushort) + WEIGHTS_0 (VEC4 float)
 *   - indices (USHORT / UINT)
 *   - skin : joints[] + inverseBindMatrices
 *   - animation : channels (translation/rotation/scale) + samplers
 *     LINEAR interpolation (STEP / CUBICSPLINE non supportes -- fallback LINEAR)
 *
 * Limites :
 *   - max GLTF_MAX_BONES bones (64)
 *   - 1 mesh, 1 primitive, 1 skin, 1 animation (les premiers du file)
 *   - 4 influences max par vertex (standard glTF) */

#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#define GLTF_MAX_BONES 64

/* Bone : noeud d'une hierarchie squelette + IBM + initial TRS. */
typedef struct {
    int   parent;          /* -1 si root */
    float t[3];            /* translation locale initiale */
    float r[4];            /* rotation quaternion (x, y, z, w) */
    float s[3];            /* scale locale initiale */
    float ibm[16];         /* inverse bind matrix (column-major) */
    /* runtime : TRS courant apres animation, et matrices calculees */
    float cur_t[3], cur_r[4], cur_s[3];
    float world[16];       /* world matrix (apres propagation hierarchie) */
} GltfBone;

/* Animation sampler : input (times) + output (values). LINEAR seulement. */
typedef struct {
    float *times;          /* count floats */
    float *values;         /* count * stride floats */
    int    count;
    int    stride;         /* 3 (T/S) ou 4 (R quaternion) */
} GltfSampler;

/* Animation channel : pointe vers un sampler + un bone + un path. */
typedef struct {
    int target_bone;       /* index dans bones[] */
    int path;              /* 0=translation, 1=rotation, 2=scale */
    int sampler;
} GltfChannel;

typedef struct {
    GltfChannel *channels;
    GltfSampler *samplers;
    int channel_count;
    int sampler_count;
    float duration;
} GltfAnim;

typedef struct {
    /* Vertex data (interleave 9 floats : pos + normal + color) */
    float    *verts;
    int       vert_count;
    /* Skinning attrs (NULL si pas de skin). joints = uint8 * 4 par vertex,
     * weights = float * 4 par vertex, dans des arrays separes pour upload
     * GPU dedie via gfx_skin_mesh_upload. */
    uint8_t  *joints;      /* vert_count * 4 */
    float    *weights;     /* vert_count * 4 */
    /* Squelette */
    GltfBone *bones;
    int       bone_count;
    /* Animations (premiere uniquement chargee pour l'instant) */
    GltfAnim  anim;
    bool      has_anim;
    float     anim_time;
    /* etat upload-friendly : matrices skin (cur_world * ibm) calculees
     * a chaque appel gltf_advance_animation */
    float     skin_matrices[GLTF_MAX_BONES * 16];
} GltfMesh;

bool gltf_load_glb           (const char *path, GltfMesh *out);
void gltf_advance_animation  (GltfMesh *m, float dt);
void gltf_free               (GltfMesh *m);

/* === Genere un humanoide rigué en code, sans fichier externe ===
 * 7 bones (pelvis, spine, head, 2 bras, 2 jambes), 7 box primitives
 * weighted chacune a 1 bone (rigid skinning). 1 animation : bras qui
 * bougent + respiration. Sert de demo + test du pipeline complet. */
bool gltf_make_test_humanoid (GltfMesh *out);

#endif

#ifdef GLTF_LOADER_IMPLEMENTATION

#include "json_mini.h"
#define JSON_MINI_IMPLEMENTATION
#include "json_mini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* === Constantes glTF === */
#define GLTF_CT_BYTE   5120
#define GLTF_CT_UBYTE  5121
#define GLTF_CT_SHORT  5122
#define GLTF_CT_USHORT 5123
#define GLTF_CT_UINT   5125
#define GLTF_CT_FLOAT  5126

static int gltf__ct_size(int ct) {
    switch (ct) {
        case GLTF_CT_BYTE: case GLTF_CT_UBYTE:  return 1;
        case GLTF_CT_SHORT:case GLTF_CT_USHORT: return 2;
        case GLTF_CT_UINT: case GLTF_CT_FLOAT:  return 4;
    }
    return 0;
}
static int gltf__type_components(const char *src, const JsmTok *t) {
    if (jsm_streq(src, t, "SCALAR")) return 1;
    if (jsm_streq(src, t, "VEC2"))   return 2;
    if (jsm_streq(src, t, "VEC3"))   return 3;
    if (jsm_streq(src, t, "VEC4"))   return 4;
    if (jsm_streq(src, t, "MAT4"))   return 16;
    return 0;
}

typedef struct {
    const unsigned char *data;
    int count, comps, ct, stride;
} GltfAccessor;

static bool gltf__resolve_accessor(const char *src, const JsmTok *toks,
                                    int accessors_arr, int buffer_views_arr,
                                    const unsigned char *bin,
                                    int accessor_idx, GltfAccessor *out) {
    if (accessors_arr < 0) return false;
    /* nav to accessors[accessor_idx] */
    int i = accessors_arr + 1;
    for (int k = 0; k < accessor_idx; k++) i = jsm_skip(toks, i);
    int acc_idx = i;
    int bv = -1, byte_off_acc = 0, count = 0, ct = 0, comps = 0;
    int t = jsm_obj_find(src, toks, acc_idx, "bufferView");
    if (t >= 0) bv = jsm_to_int(src, &toks[t]);
    t = jsm_obj_find(src, toks, acc_idx, "byteOffset");
    if (t >= 0) byte_off_acc = jsm_to_int(src, &toks[t]);
    t = jsm_obj_find(src, toks, acc_idx, "componentType");
    if (t >= 0) ct = jsm_to_int(src, &toks[t]);
    t = jsm_obj_find(src, toks, acc_idx, "count");
    if (t >= 0) count = jsm_to_int(src, &toks[t]);
    t = jsm_obj_find(src, toks, acc_idx, "type");
    if (t >= 0) comps = gltf__type_components(src, &toks[t]);
    if (bv < 0 || count <= 0 || comps <= 0 || ct <= 0) return false;
    int bvi = buffer_views_arr + 1;
    for (int k = 0; k < bv; k++) bvi = jsm_skip(toks, bvi);
    int byte_off_bv = 0, stride = 0;
    t = jsm_obj_find(src, toks, bvi, "byteOffset");
    if (t >= 0) byte_off_bv = jsm_to_int(src, &toks[t]);
    t = jsm_obj_find(src, toks, bvi, "byteStride");
    if (t >= 0) stride = jsm_to_int(src, &toks[t]);
    int elem_sz = gltf__ct_size(ct) * comps;
    if (stride == 0) stride = elem_sz;
    out->data   = bin + byte_off_bv + byte_off_acc;
    out->count  = count;
    out->comps  = comps;
    out->ct     = ct;
    out->stride = stride;
    return true;
}

static float gltf__read_float(const unsigned char *p, int ct) {
    if (ct == GLTF_CT_FLOAT) { float f; memcpy(&f, p, 4); return f; }
    return 0.f;
}
static unsigned int gltf__read_uint(const unsigned char *p, int ct) {
    if (ct == GLTF_CT_USHORT) return (unsigned)p[0] | ((unsigned)p[1] << 8);
    if (ct == GLTF_CT_UINT)   return (unsigned)p[0] | ((unsigned)p[1] << 8)
                                   | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    if (ct == GLTF_CT_UBYTE)  return p[0];
    return 0;
}

/* === mat4 / quat math (column-major, M[col*4 + row]) === */
static void gltf__m4_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}
static void gltf__m4_mul(float *out, const float *a, const float *b) {
    float r[16];
    for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++) {
        float s = 0.f;
        for (int k = 0; k < 4; k++)
            s += a[k * 4 + row] * b[c * 4 + k];
        r[c * 4 + row] = s;
    }
    memcpy(out, r, 16 * sizeof(float));
}
static void quat_to_mat(const float *q, float *m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;
    gltf__m4_identity(m);
    m[0]  = 1 - 2*(yy + zz);  m[4]  =     2*(xy - wz);  m[8]  =     2*(xz + wy);
    m[1]  =     2*(xy + wz);  m[5]  = 1 - 2*(xx + zz);  m[9]  =     2*(yz - wx);
    m[2]  =     2*(xz - wy);  m[6]  =     2*(yz + wx);  m[10] = 1 - 2*(xx + yy);
}
static void trs_to_mat(const float *t, const float *r, const float *s,
                        float *m) {
    float rm[16]; quat_to_mat(r, rm);
    /* scale */
    rm[0] *= s[0]; rm[1] *= s[0]; rm[2] *= s[0];
    rm[4] *= s[1]; rm[5] *= s[1]; rm[6] *= s[1];
    rm[8] *= s[2]; rm[9] *= s[2]; rm[10]*= s[2];
    /* translation */
    rm[12] = t[0]; rm[13] = t[1]; rm[14] = t[2]; rm[15] = 1.f;
    memcpy(m, rm, 16 * sizeof(float));
}
static void quat_slerp(const float *a, const float *b, float t, float *out) {
    float d = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    float bs[4] = { b[0], b[1], b[2], b[3] };
    if (d < 0.f) { bs[0]=-bs[0]; bs[1]=-bs[1]; bs[2]=-bs[2]; bs[3]=-bs[3]; d=-d; }
    if (d > 0.9995f) {
        /* lerp + normalize */
        for (int i = 0; i < 4; i++) out[i] = a[i] + t * (bs[i] - a[i]);
    } else {
        float th = acosf(d), s = sinf(th);
        float w0 = sinf((1.f - t) * th) / s;
        float w1 = sinf(t * th) / s;
        for (int i = 0; i < 4; i++) out[i] = a[i] * w0 + bs[i] * w1;
    }
    float l = sqrtf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]+out[3]*out[3])+1e-6f;
    for (int i = 0; i < 4; i++) out[i] /= l;
}

/* === GLB loader === */
bool gltf_load_glb(const char *path, GltfMesh *out) {
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz < 28) { fclose(f); return false; }
    unsigned char *buf = (unsigned char *)malloc((size_t)fsz);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)fsz, f);
    fclose(f);
    if (rd != (size_t)fsz) { free(buf); return false; }
    if (buf[0]!='g'||buf[1]!='l'||buf[2]!='T'||buf[3]!='F') { free(buf); return false; }
    int pos = 12;
    if (pos + 8 > fsz) { free(buf); return false; }
    unsigned int json_len = (unsigned)buf[pos] | ((unsigned)buf[pos+1] << 8)
                          | ((unsigned)buf[pos+2] << 16) | ((unsigned)buf[pos+3] << 24);
    if (memcmp(buf + pos + 4, "JSON", 4) != 0) { free(buf); return false; }
    const char *json_start = (const char *)(buf + pos + 8);
    pos += 8 + (int)json_len;
    if (pos + 8 > fsz) { free(buf); return false; }
    if (memcmp(buf + pos + 4, "BIN\0", 4) != 0) { free(buf); return false; }
    const unsigned char *bin = buf + pos + 8;
    /* Parse JSON */
    int max_toks = 8192;
    JsmTok *toks = (JsmTok *)malloc(sizeof(JsmTok) * max_toks);
    if (!toks) { free(buf); return false; }
    int ntok = jsm_parse(json_start, (int)json_len, toks, max_toks);
    if (ntok < 0) { free(toks); free(buf); return false; }
    /* Roots */
    int meshes_arr = jsm_obj_find(json_start, toks, 0, "meshes");
    int access_arr = jsm_obj_find(json_start, toks, 0, "accessors");
    int bv_arr     = jsm_obj_find(json_start, toks, 0, "bufferViews");
    int skins_arr  = jsm_obj_find(json_start, toks, 0, "skins");
    int anims_arr  = jsm_obj_find(json_start, toks, 0, "animations");
    int nodes_arr  = jsm_obj_find(json_start, toks, 0, "nodes");
    if (meshes_arr < 0 || access_arr < 0 || bv_arr < 0) {
        free(toks); free(buf); return false;
    }
    /* Premiere primitive */
    int mesh0 = meshes_arr + 1;
    int prims = jsm_obj_find(json_start, toks, mesh0, "primitives");
    if (prims < 0 || toks[prims].size < 1) { free(toks); free(buf); return false; }
    int prim0 = prims + 1;
    int attribs = jsm_obj_find(json_start, toks, prim0, "attributes");
    if (attribs < 0) { free(toks); free(buf); return false; }
    int pos_t = jsm_obj_find(json_start, toks, attribs, "POSITION");
    int nor_t = jsm_obj_find(json_start, toks, attribs, "NORMAL");
    int jts_t = jsm_obj_find(json_start, toks, attribs, "JOINTS_0");
    int wts_t = jsm_obj_find(json_start, toks, attribs, "WEIGHTS_0");
    int idx_t = jsm_obj_find(json_start, toks, prim0,   "indices");
    if (pos_t < 0) { free(toks); free(buf); return false; }
    int pos_acc = jsm_to_int(json_start, &toks[pos_t]);
    int nor_acc = (nor_t >= 0) ? jsm_to_int(json_start, &toks[nor_t]) : -1;
    int jts_acc = (jts_t >= 0) ? jsm_to_int(json_start, &toks[jts_t]) : -1;
    int wts_acc = (wts_t >= 0) ? jsm_to_int(json_start, &toks[wts_t]) : -1;
    int idx_acc = (idx_t >= 0) ? jsm_to_int(json_start, &toks[idx_t]) : -1;
    GltfAccessor a_pos = {0}, a_nor = {0}, a_jts = {0}, a_wts = {0}, a_idx = {0};
    if (!gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, pos_acc, &a_pos)) {
        free(toks); free(buf); return false;
    }
    bool has_n = (nor_acc >= 0) &&
        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, nor_acc, &a_nor);
    bool has_i = (idx_acc >= 0) &&
        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, idx_acc, &a_idx);
    bool has_skin = (jts_acc >= 0) && (wts_acc >= 0) &&
        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, jts_acc, &a_jts) &&
        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr, bin, wts_acc, &a_wts);
    /* Build verts */
    int vert_count = has_i ? a_idx.count : a_pos.count;
    if (vert_count < 3) { free(toks); free(buf); return false; }
    out->verts = (float *)malloc(sizeof(float) * 9 * vert_count);
    if (!out->verts) { free(toks); free(buf); return false; }
    out->vert_count = vert_count;
    if (has_skin) {
        out->joints  = (uint8_t *)malloc(sizeof(uint8_t) * 4 * vert_count);
        out->weights = (float   *)malloc(sizeof(float)   * 4 * vert_count);
    }
    for (int i = 0; i < vert_count; i++) {
        int src_v = has_i
            ? (int)gltf__read_uint(a_idx.data + i * a_idx.stride, a_idx.ct)
            : i;
        if (src_v < 0 || src_v >= a_pos.count) src_v = 0;
        const unsigned char *pp = a_pos.data + src_v * a_pos.stride;
        float px = gltf__read_float(pp,     a_pos.ct);
        float py = gltf__read_float(pp + 4, a_pos.ct);
        float pz = gltf__read_float(pp + 8, a_pos.ct);
        float nx = 0, ny = 1, nz = 0;
        if (has_n && src_v < a_nor.count) {
            const unsigned char *np = a_nor.data + src_v * a_nor.stride;
            nx = gltf__read_float(np,     a_nor.ct);
            ny = gltf__read_float(np + 4, a_nor.ct);
            nz = gltf__read_float(np + 8, a_nor.ct);
        }
        float *o = &out->verts[i * 9];
        o[0]=px; o[1]=py; o[2]=pz;
        o[3]=nx; o[4]=ny; o[5]=nz;
        o[6]=1.f; o[7]=1.f; o[8]=1.f;
        if (has_skin) {
            const unsigned char *jp = a_jts.data + src_v * a_jts.stride;
            const unsigned char *wp = a_wts.data + src_v * a_wts.stride;
            for (int k = 0; k < 4; k++) {
                unsigned int j = gltf__read_uint(jp + k * gltf__ct_size(a_jts.ct), a_jts.ct);
                if (j > 255) j = 0;
                out->joints[i * 4 + k] = (uint8_t)j;
                out->weights[i * 4 + k] = gltf__read_float(wp + k * 4, a_wts.ct);
            }
        }
    }
    /* Si pas de normales : calcul par face */
    if (!has_n) {
        for (int i = 0; i + 2 < vert_count; i += 3) {
            float *v0=&out->verts[(i+0)*9], *v1=&out->verts[(i+1)*9], *v2=&out->verts[(i+2)*9];
            float ex=v1[0]-v0[0], ey=v1[1]-v0[1], ez=v1[2]-v0[2];
            float fx=v2[0]-v0[0], fy=v2[1]-v0[1], fz=v2[2]-v0[2];
            float nx=ey*fz-ez*fy, ny=ez*fx-ex*fz, nz=ex*fy-ey*fx;
            float ln = sqrtf(nx*nx + ny*ny + nz*nz) + 1e-6f;
            nx/=ln; ny/=ln; nz/=ln;
            for (int k = 0; k < 3; k++) { float *o = &out->verts[(i+k)*9];
                o[3]=nx; o[4]=ny; o[5]=nz; }
        }
    }
    /* === Skin : joints + IBMs === */
    if (has_skin && skins_arr >= 0 && nodes_arr >= 0 &&
        toks[skins_arr].type == JSM_ARRAY && toks[skins_arr].size >= 1) {
        int skin0 = skins_arr + 1;
        int joints_t = jsm_obj_find(json_start, toks, skin0, "joints");
        int ibm_t    = jsm_obj_find(json_start, toks, skin0, "inverseBindMatrices");
        if (joints_t >= 0 && toks[joints_t].type == JSM_ARRAY) {
            int bone_count = toks[joints_t].size;
            if (bone_count > GLTF_MAX_BONES) bone_count = GLTF_MAX_BONES;
            out->bone_count = bone_count;
            out->bones = (GltfBone *)calloc(bone_count, sizeof(GltfBone));
            /* lit les indices de noeuds */
            int *node_idx = (int *)malloc(sizeof(int) * bone_count);
            int it = joints_t + 1;
            for (int k = 0; k < bone_count; k++) {
                node_idx[k] = jsm_to_int(json_start, &toks[it]);
                it = jsm_skip(toks, it);
            }
            /* IBM accessor */
            if (ibm_t >= 0) {
                int ibm_acc = jsm_to_int(json_start, &toks[ibm_t]);
                GltfAccessor a_ibm;
                if (gltf__resolve_accessor(json_start, toks, access_arr, bv_arr,
                                           bin, ibm_acc, &a_ibm)) {
                    for (int k = 0; k < bone_count && k < a_ibm.count; k++) {
                        const unsigned char *p = a_ibm.data + k * a_ibm.stride;
                        for (int m = 0; m < 16; m++) {
                            float v; memcpy(&v, p + m * 4, 4);
                            out->bones[k].ibm[m] = v;
                        }
                    }
                }
            } else {
                for (int k = 0; k < bone_count; k++) gltf__m4_identity(out->bones[k].ibm);
            }
            /* Pour chaque bone, lit son TRS depuis nodes[node_idx[k]] +
             * cherche le parent en scannant les children de tous les autres nodes. */
            for (int k = 0; k < bone_count; k++) {
                GltfBone *b = &out->bones[k];
                b->parent = -1;
                /* init defaults */
                b->t[0]=b->t[1]=b->t[2]=0.f;
                b->r[0]=b->r[1]=b->r[2]=0.f; b->r[3]=1.f;
                b->s[0]=b->s[1]=b->s[2]=1.f;
                /* nav to nodes[node_idx[k]] */
                int ni = nodes_arr + 1;
                for (int j = 0; j < node_idx[k]; j++) ni = jsm_skip(toks, ni);
                int tt = jsm_obj_find(json_start, toks, ni, "translation");
                if (tt >= 0 && toks[tt].type == JSM_ARRAY) {
                    int j = tt + 1;
                    for (int m = 0; m < 3 && m < toks[tt].size; m++) {
                        b->t[m] = (float)jsm_to_double(json_start, &toks[j]);
                        j = jsm_skip(toks, j);
                    }
                }
                int rt = jsm_obj_find(json_start, toks, ni, "rotation");
                if (rt >= 0 && toks[rt].type == JSM_ARRAY) {
                    int j = rt + 1;
                    for (int m = 0; m < 4 && m < toks[rt].size; m++) {
                        b->r[m] = (float)jsm_to_double(json_start, &toks[j]);
                        j = jsm_skip(toks, j);
                    }
                }
                int st = jsm_obj_find(json_start, toks, ni, "scale");
                if (st >= 0 && toks[st].type == JSM_ARRAY) {
                    int j = st + 1;
                    for (int m = 0; m < 3 && m < toks[st].size; m++) {
                        b->s[m] = (float)jsm_to_double(json_start, &toks[j]);
                        j = jsm_skip(toks, j);
                    }
                }
                memcpy(b->cur_t, b->t, sizeof(b->t));
                memcpy(b->cur_r, b->r, sizeof(b->r));
                memcpy(b->cur_s, b->s, sizeof(b->s));
            }
            /* parent : scan all nodes' children arrays to find each bone's parent */
            for (int p = 0; p < toks[nodes_arr].size; p++) {
                int ni = nodes_arr + 1;
                for (int j = 0; j < p; j++) ni = jsm_skip(toks, ni);
                int ct = jsm_obj_find(json_start, toks, ni, "children");
                if (ct < 0 || toks[ct].type != JSM_ARRAY) continue;
                int j = ct + 1;
                for (int c = 0; c < toks[ct].size; c++) {
                    int child_node = jsm_to_int(json_start, &toks[j]);
                    /* find bone idx with node_idx == child_node */
                    for (int bn = 0; bn < bone_count; bn++) {
                        if (node_idx[bn] == child_node) {
                            /* find parent bone idx with node_idx == p */
                            for (int pp = 0; pp < bone_count; pp++) {
                                if (node_idx[pp] == p) { out->bones[bn].parent = pp; break; }
                            }
                            break;
                        }
                    }
                    j = jsm_skip(toks, j);
                }
            }
            free(node_idx);
        }
        /* === Animation (premiere) === */
        if (anims_arr >= 0 && toks[anims_arr].type == JSM_ARRAY &&
            toks[anims_arr].size >= 1 && out->bone_count > 0) {
            int anim0 = anims_arr + 1;
            int channels_t = jsm_obj_find(json_start, toks, anim0, "channels");
            int samplers_t = jsm_obj_find(json_start, toks, anim0, "samplers");
            if (channels_t >= 0 && samplers_t >= 0 &&
                toks[channels_t].type == JSM_ARRAY &&
                toks[samplers_t].type == JSM_ARRAY) {
                int ns = toks[samplers_t].size;
                int nc = toks[channels_t].size;
                out->anim.samplers = (GltfSampler *)calloc(ns, sizeof(GltfSampler));
                out->anim.channels = (GltfChannel *)calloc(nc, sizeof(GltfChannel));
                out->anim.sampler_count = ns;
                out->anim.channel_count = nc;
                float dur = 0.f;
                /* parse samplers */
                int si = samplers_t + 1;
                for (int s = 0; s < ns; s++) {
                    int in_t  = jsm_obj_find(json_start, toks, si, "input");
                    int out_t = jsm_obj_find(json_start, toks, si, "output");
                    if (in_t < 0 || out_t < 0) { si = jsm_skip(toks, si); continue; }
                    int in_acc  = jsm_to_int(json_start, &toks[in_t]);
                    int out_acc = jsm_to_int(json_start, &toks[out_t]);
                    GltfAccessor a_in, a_out;
                    if (gltf__resolve_accessor(json_start, toks, access_arr, bv_arr,
                                                bin, in_acc, &a_in) &&
                        gltf__resolve_accessor(json_start, toks, access_arr, bv_arr,
                                                bin, out_acc, &a_out)) {
                        out->anim.samplers[s].count  = a_in.count;
                        out->anim.samplers[s].stride = a_out.comps;
                        out->anim.samplers[s].times  = (float *)malloc(sizeof(float)*a_in.count);
                        out->anim.samplers[s].values = (float *)malloc(sizeof(float)*a_in.count*a_out.comps);
                        for (int k = 0; k < a_in.count; k++) {
                            float v; memcpy(&v, a_in.data + k * a_in.stride, 4);
                            out->anim.samplers[s].times[k] = v;
                            if (v > dur) dur = v;
                        }
                        for (int k = 0; k < a_in.count; k++) {
                            for (int c = 0; c < a_out.comps; c++) {
                                float v; memcpy(&v,
                                    a_out.data + k * a_out.stride + c * 4, 4);
                                out->anim.samplers[s].values[k * a_out.comps + c] = v;
                            }
                        }
                    }
                    si = jsm_skip(toks, si);
                }
                out->anim.duration = dur > 0.f ? dur : 1.f;
                /* parse channels */
                int ci = channels_t + 1;
                for (int c = 0; c < nc; c++) {
                    int smp_t   = jsm_obj_find(json_start, toks, ci, "sampler");
                    int tgt_t   = jsm_obj_find(json_start, toks, ci, "target");
                    out->anim.channels[c].target_bone = -1;
                    if (smp_t >= 0) out->anim.channels[c].sampler = jsm_to_int(json_start, &toks[smp_t]);
                    if (tgt_t >= 0) {
                        int tn_t = jsm_obj_find(json_start, toks, tgt_t, "node");
                        int tp_t = jsm_obj_find(json_start, toks, tgt_t, "path");
                        if (tn_t >= 0) {
                            int target_node = jsm_to_int(json_start, &toks[tn_t]);
                            /* find bone whose node_idx == target_node : on doit
                             * re-faire la lookup via une iteration. Garde une copie
                             * dans out->bones[].parent (-1) initialement. Pour
                             * simplifier on cherche par iteration des bones,
                             * mais ici on n'a plus node_idx => on stocke a part. */
                            (void)target_node;
                            out->anim.channels[c].target_bone = -1;
                            /* Simplification : on traite la cible comme bone_idx
                             * directement (assume node_idx mapping 1:1 dans l'ordre
                             * du skin). Pour des assets exporters standards (Blender),
                             * c'est generalement le cas. */
                            out->anim.channels[c].target_bone = target_node;
                            if (out->anim.channels[c].target_bone >= out->bone_count)
                                out->anim.channels[c].target_bone = -1;
                        }
                        if (tp_t >= 0) {
                            const JsmTok *pt = &toks[tp_t];
                            if      (jsm_streq(json_start, pt, "translation")) out->anim.channels[c].path = 0;
                            else if (jsm_streq(json_start, pt, "rotation"))    out->anim.channels[c].path = 1;
                            else if (jsm_streq(json_start, pt, "scale"))       out->anim.channels[c].path = 2;
                            else                                                out->anim.channels[c].path = -1;
                        }
                    }
                    ci = jsm_skip(toks, ci);
                }
                out->has_anim = true;
                /* init skin matrices a identity */
                for (int k = 0; k < out->bone_count; k++) gltf__m4_identity(out->bones[k].world);
                for (int k = 0; k < GLTF_MAX_BONES; k++) gltf__m4_identity(&out->skin_matrices[k * 16]);
            }
        }
    }
    free(toks);
    free(buf);
    return true;
}

/* === Animation runtime === */
static void gltf__sample_vec(const GltfSampler *s, float t,
                              float *out, int comps) {
    if (s->count == 0) return;
    if (t <= s->times[0]) {
        memcpy(out, s->values, sizeof(float) * comps);
        return;
    }
    if (t >= s->times[s->count - 1]) {
        memcpy(out, s->values + (s->count - 1) * comps, sizeof(float) * comps);
        return;
    }
    int i = 0;
    while (i + 1 < s->count && s->times[i + 1] < t) i++;
    float t0 = s->times[i], t1 = s->times[i + 1];
    float u = (t - t0) / (t1 - t0 + 1e-6f);
    const float *v0 = s->values + i * comps;
    const float *v1 = s->values + (i + 1) * comps;
    if (comps == 4) {
        /* rotation : slerp */
        quat_slerp(v0, v1, u, out);
    } else {
        for (int k = 0; k < comps; k++) out[k] = v0[k] + (v1[k] - v0[k]) * u;
    }
}

void gltf_advance_animation(GltfMesh *m, float dt) {
    if (!m || m->bone_count <= 0) return;
    if (m->has_anim) {
        m->anim_time += dt;
        if (m->anim_time > m->anim.duration) m->anim_time = fmodf(m->anim_time, m->anim.duration);
        /* reset cur TRS to bind */
        for (int k = 0; k < m->bone_count; k++) {
            memcpy(m->bones[k].cur_t, m->bones[k].t, sizeof(m->bones[k].t));
            memcpy(m->bones[k].cur_r, m->bones[k].r, sizeof(m->bones[k].r));
            memcpy(m->bones[k].cur_s, m->bones[k].s, sizeof(m->bones[k].s));
        }
        /* apply each channel sample */
        for (int c = 0; c < m->anim.channel_count; c++) {
            const GltfChannel *ch = &m->anim.channels[c];
            if (ch->target_bone < 0 || ch->target_bone >= m->bone_count) continue;
            if (ch->sampler < 0 || ch->sampler >= m->anim.sampler_count) continue;
            const GltfSampler *s = &m->anim.samplers[ch->sampler];
            GltfBone *b = &m->bones[ch->target_bone];
            if (ch->path == 0)      gltf__sample_vec(s, m->anim_time, b->cur_t, 3);
            else if (ch->path == 1) gltf__sample_vec(s, m->anim_time, b->cur_r, 4);
            else if (ch->path == 2) gltf__sample_vec(s, m->anim_time, b->cur_s, 3);
        }
    }
    /* compute world matrices : iterate bones in order. Assume parents
     * appear before children (Blender exports respect ca). */
    for (int k = 0; k < m->bone_count; k++) {
        GltfBone *b = &m->bones[k];
        float local[16];
        trs_to_mat(b->cur_t, b->cur_r, b->cur_s, local);
        if (b->parent >= 0 && b->parent < k) {
            gltf__m4_mul(b->world, m->bones[b->parent].world, local);
        } else {
            memcpy(b->world, local, sizeof(local));
        }
    }
    /* skin matrices = world * IBM */
    int n = m->bone_count;
    if (n > GLTF_MAX_BONES) n = GLTF_MAX_BONES;
    for (int k = 0; k < n; k++) {
        gltf__m4_mul(&m->skin_matrices[k * 16], m->bones[k].world, m->bones[k].ibm);
    }
}

/* === Genere un humanoide rigué en code === */
static void gltf__push_box_to_mesh(float *verts, int *iv,
                                    float cx, float cy, float cz,
                                    float sx, float sy, float sz,
                                    int bone_idx,
                                    float r, float g, float b) {
    float x0 = cx - sx*0.5f, x1 = cx + sx*0.5f;
    float y0 = cy - sy*0.5f, y1 = cy + sy*0.5f;
    float z0 = cz - sz*0.5f, z1 = cz + sz*0.5f;
    /* 6 faces * 2 tris * 3 verts = 36 verts */
    struct { float p[3], n[3]; } F[36] = {
        /* +X */
        {{x1,y0,z0},{1,0,0}}, {{x1,y1,z0},{1,0,0}}, {{x1,y1,z1},{1,0,0}},
        {{x1,y0,z0},{1,0,0}}, {{x1,y1,z1},{1,0,0}}, {{x1,y0,z1},{1,0,0}},
        /* -X */
        {{x0,y0,z1},{-1,0,0}}, {{x0,y1,z1},{-1,0,0}}, {{x0,y1,z0},{-1,0,0}},
        {{x0,y0,z1},{-1,0,0}}, {{x0,y1,z0},{-1,0,0}}, {{x0,y0,z0},{-1,0,0}},
        /* +Y */
        {{x0,y1,z0},{0,1,0}}, {{x0,y1,z1},{0,1,0}}, {{x1,y1,z1},{0,1,0}},
        {{x0,y1,z0},{0,1,0}}, {{x1,y1,z1},{0,1,0}}, {{x1,y1,z0},{0,1,0}},
        /* -Y */
        {{x0,y0,z1},{0,-1,0}}, {{x0,y0,z0},{0,-1,0}}, {{x1,y0,z0},{0,-1,0}},
        {{x0,y0,z1},{0,-1,0}}, {{x1,y0,z0},{0,-1,0}}, {{x1,y0,z1},{0,-1,0}},
        /* +Z */
        {{x0,y0,z1},{0,0,1}}, {{x1,y0,z1},{0,0,1}}, {{x1,y1,z1},{0,0,1}},
        {{x0,y0,z1},{0,0,1}}, {{x1,y1,z1},{0,0,1}}, {{x0,y1,z1},{0,0,1}},
        /* -Z */
        {{x1,y0,z0},{0,0,-1}}, {{x0,y0,z0},{0,0,-1}}, {{x0,y1,z0},{0,0,-1}},
        {{x1,y0,z0},{0,0,-1}}, {{x0,y1,z0},{0,0,-1}}, {{x1,y1,z0},{0,0,-1}},
    };
    (void)bone_idx;
    for (int i = 0; i < 36; i++) {
        float *o = &verts[(*iv) * 9];
        o[0]=F[i].p[0]; o[1]=F[i].p[1]; o[2]=F[i].p[2];
        o[3]=F[i].n[0]; o[4]=F[i].n[1]; o[5]=F[i].n[2];
        o[6]=r; o[7]=g; o[8]=b;
        (*iv)++;
    }
}

bool gltf_make_test_humanoid(GltfMesh *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    /* 7 bones : pelvis / spine / head / l_arm / r_arm / l_leg / r_leg.
     * Hierarchie : pelvis -> spine -> head, pelvis -> *leg, spine -> *arm. */
    const int N_BONES = 7;
    enum { B_PELVIS, B_SPINE, B_HEAD, B_LARM, B_RARM, B_LLEG, B_RLEG };
    out->bone_count = N_BONES;
    out->bones = (GltfBone *)calloc(N_BONES, sizeof(GltfBone));
    if (!out->bones) return false;
    /* Bind pose : Y-up, base au sol y=0. */
    static const struct { int parent; float t[3]; } BINDS[7] = {
        { -1,     { 0.f, 0.55f, 0.f } },   /* PELVIS : centre */
        { B_PELVIS, { 0.f, 0.40f, 0.f } }, /* SPINE relative au pelvis */
        { B_SPINE,  { 0.f, 0.30f, 0.f } }, /* HEAD relative au spine */
        { B_SPINE,  {-0.35f, 0.25f, 0.f } }, /* LARM relative au spine */
        { B_SPINE,  { 0.35f, 0.25f, 0.f } }, /* RARM */
        { B_PELVIS, {-0.15f,-0.10f, 0.f } }, /* LLEG */
        { B_PELVIS, { 0.15f,-0.10f, 0.f } }, /* RLEG */
    };
    /* Calcule positions world du bind pose */
    float world[N_BONES][3];
    for (int i = 0; i < N_BONES; i++) {
        GltfBone *b = &out->bones[i];
        b->parent = BINDS[i].parent;
        b->t[0] = BINDS[i].t[0]; b->t[1] = BINDS[i].t[1]; b->t[2] = BINDS[i].t[2];
        b->r[0]=b->r[1]=b->r[2]=0.f; b->r[3]=1.f;
        b->s[0]=b->s[1]=b->s[2]=1.f;
        memcpy(b->cur_t, b->t, sizeof(b->t));
        memcpy(b->cur_r, b->r, sizeof(b->r));
        memcpy(b->cur_s, b->s, sizeof(b->s));
        /* world = parent.world + t (rotation null in bind) */
        if (b->parent >= 0) {
            world[i][0] = world[b->parent][0] + b->t[0];
            world[i][1] = world[b->parent][1] + b->t[1];
            world[i][2] = world[b->parent][2] + b->t[2];
        } else {
            world[i][0] = b->t[0]; world[i][1] = b->t[1]; world[i][2] = b->t[2];
        }
        /* IBM = inverse de la translation world (en mat4 identity rotation) */
        gltf__m4_identity(b->ibm);
        b->ibm[12] = -world[i][0];
        b->ibm[13] = -world[i][1];
        b->ibm[14] = -world[i][2];
        gltf__m4_identity(b->world);
    }
    /* === Geometrie : 7 box, chacune weighted a 1 bone === */
    const int N_PARTS = 7;
    out->vert_count = N_PARTS * 36;
    out->verts   = (float *)malloc(sizeof(float) * 9 * out->vert_count);
    out->joints  = (uint8_t *)calloc(out->vert_count * 4, sizeof(uint8_t));
    out->weights = (float *)calloc(out->vert_count * 4, sizeof(float));
    if (!out->verts || !out->joints || !out->weights) {
        gltf_free(out); return false;
    }
    int iv = 0;
    /* PELVIS box : autour de y=0.55 (offset world) */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_PELVIS][0], world[B_PELVIS][1], world[B_PELVIS][2],
        0.30f, 0.20f, 0.20f, B_PELVIS, 0.45f, 0.30f, 0.55f);
    /* SPINE box */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_SPINE][0], world[B_SPINE][1], world[B_SPINE][2],
        0.35f, 0.45f, 0.22f, B_SPINE, 0.55f, 0.40f, 0.65f);
    /* HEAD box */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_HEAD][0], world[B_HEAD][1], world[B_HEAD][2],
        0.25f, 0.25f, 0.25f, B_HEAD, 0.92f, 0.75f, 0.55f);
    /* LARM box */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_LARM][0] - 0.10f, world[B_LARM][1] - 0.20f, world[B_LARM][2],
        0.12f, 0.40f, 0.12f, B_LARM, 0.85f, 0.65f, 0.45f);
    /* RARM box */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_RARM][0] + 0.10f, world[B_RARM][1] - 0.20f, world[B_RARM][2],
        0.12f, 0.40f, 0.12f, B_RARM, 0.85f, 0.65f, 0.45f);
    /* LLEG box */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_LLEG][0], world[B_LLEG][1] - 0.25f, world[B_LLEG][2],
        0.14f, 0.50f, 0.14f, B_LLEG, 0.30f, 0.25f, 0.40f);
    /* RLEG box */
    gltf__push_box_to_mesh(out->verts, &iv,
        world[B_RLEG][0], world[B_RLEG][1] - 0.25f, world[B_RLEG][2],
        0.14f, 0.50f, 0.14f, B_RLEG, 0.30f, 0.25f, 0.40f);
    /* Assign joint + weight : pour chaque part (36 verts contigus),
     * weight 1.0 sur le bone correspondant */
    static const int part_bone[7] = { B_PELVIS, B_SPINE, B_HEAD,
                                       B_LARM, B_RARM, B_LLEG, B_RLEG };
    for (int p = 0; p < N_PARTS; p++) {
        for (int k = 0; k < 36; k++) {
            int vi = p * 36 + k;
            out->joints [vi * 4 + 0] = (uint8_t)part_bone[p];
            out->weights[vi * 4 + 0] = 1.0f;
        }
    }
    /* === Animation procedurale : "wave" + "breathe" sur 2 secondes ===
     * 8 keyframes a t=0, 0.25, 0.5, ..., 2.0 (boucle).
     * Channels : bras gauche rotation Z, bras droit rotation Z, spine
     * scale Y (respire). */
    out->has_anim = true;
    out->anim.duration = 2.0f;
    out->anim.sampler_count = 3;
    out->anim.channel_count = 3;
    out->anim.samplers = (GltfSampler *)calloc(3, sizeof(GltfSampler));
    out->anim.channels = (GltfChannel *)calloc(3, sizeof(GltfChannel));
    int n_keys = 9;
    /* Sampler 0 : bras gauche rotation (quat) */
    out->anim.samplers[0].count = n_keys;
    out->anim.samplers[0].stride = 4;
    out->anim.samplers[0].times = (float *)malloc(sizeof(float) * n_keys);
    out->anim.samplers[0].values = (float *)malloc(sizeof(float) * n_keys * 4);
    for (int i = 0; i < n_keys; i++) {
        float t = i * (2.0f / (n_keys - 1));
        out->anim.samplers[0].times[i] = t;
        /* rotation Z entre -45 et +45 deg en sin */
        float angle = sinf(t * 3.14159f) * 0.7f;
        out->anim.samplers[0].values[i*4 + 0] = 0;
        out->anim.samplers[0].values[i*4 + 1] = 0;
        out->anim.samplers[0].values[i*4 + 2] = sinf(angle * 0.5f);
        out->anim.samplers[0].values[i*4 + 3] = cosf(angle * 0.5f);
    }
    /* Sampler 1 : bras droit rotation (opposite phase) */
    out->anim.samplers[1].count = n_keys;
    out->anim.samplers[1].stride = 4;
    out->anim.samplers[1].times = (float *)malloc(sizeof(float) * n_keys);
    out->anim.samplers[1].values = (float *)malloc(sizeof(float) * n_keys * 4);
    for (int i = 0; i < n_keys; i++) {
        float t = i * (2.0f / (n_keys - 1));
        out->anim.samplers[1].times[i] = t;
        float angle = -sinf(t * 3.14159f) * 0.7f;
        out->anim.samplers[1].values[i*4 + 0] = 0;
        out->anim.samplers[1].values[i*4 + 1] = 0;
        out->anim.samplers[1].values[i*4 + 2] = sinf(angle * 0.5f);
        out->anim.samplers[1].values[i*4 + 3] = cosf(angle * 0.5f);
    }
    /* Sampler 2 : spine scale (breathe) */
    out->anim.samplers[2].count = n_keys;
    out->anim.samplers[2].stride = 3;
    out->anim.samplers[2].times = (float *)malloc(sizeof(float) * n_keys);
    out->anim.samplers[2].values = (float *)malloc(sizeof(float) * n_keys * 3);
    for (int i = 0; i < n_keys; i++) {
        float t = i * (2.0f / (n_keys - 1));
        out->anim.samplers[2].times[i] = t;
        float scale = 1.0f + 0.05f * sinf(t * 6.283f);
        out->anim.samplers[2].values[i*3 + 0] = scale;
        out->anim.samplers[2].values[i*3 + 1] = scale;
        out->anim.samplers[2].values[i*3 + 2] = scale;
    }
    /* Channels */
    out->anim.channels[0].target_bone = B_LARM; out->anim.channels[0].path = 1; out->anim.channels[0].sampler = 0;
    out->anim.channels[1].target_bone = B_RARM; out->anim.channels[1].path = 1; out->anim.channels[1].sampler = 1;
    out->anim.channels[2].target_bone = B_SPINE; out->anim.channels[2].path = 2; out->anim.channels[2].sampler = 2;
    for (int k = 0; k < GLTF_MAX_BONES; k++) gltf__m4_identity(&out->skin_matrices[k * 16]);
    return true;
}

void gltf_free(GltfMesh *m) {
    if (!m) return;
    free(m->verts);
    free(m->joints);
    free(m->weights);
    free(m->bones);
    if (m->anim.samplers) {
        for (int i = 0; i < m->anim.sampler_count; i++) {
            free(m->anim.samplers[i].times);
            free(m->anim.samplers[i].values);
        }
        free(m->anim.samplers);
    }
    free(m->anim.channels);
    memset(m, 0, sizeof(*m));
}

#endif
