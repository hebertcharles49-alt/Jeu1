/*
 * gfx.c - couche OpenGL 3.3 core minimale
 */
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================
 * function pointers GL3.3 (chargees via SDL_GL_GetProcAddress)
 * On utilise les PFN typedefs fournies par SDL_opengl.h.
 * ============================================================ */
static PFNGLCREATESHADERPROC      pglCreateShader;
static PFNGLSHADERSOURCEPROC      pglShaderSource;
static PFNGLCOMPILESHADERPROC     pglCompileShader;
static PFNGLGETSHADERIVPROC       pglGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC  pglGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC     pglCreateProgram;
static PFNGLATTACHSHADERPROC      pglAttachShader;
static PFNGLLINKPROGRAMPROC       pglLinkProgram;
static PFNGLGETPROGRAMIVPROC      pglGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC pglGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC        pglUseProgram;
static PFNGLDELETESHADERPROC      pglDeleteShader;
static PFNGLDELETEPROGRAMPROC     pglDeleteProgram;

static PFNGLGENBUFFERSPROC        pglGenBuffers;
static PFNGLBINDBUFFERPROC        pglBindBuffer;
static PFNGLBUFFERDATAPROC        pglBufferData;
static PFNGLBUFFERSUBDATAPROC     pglBufferSubData;
static PFNGLDELETEBUFFERSPROC     pglDeleteBuffers;

static PFNGLGENVERTEXARRAYSPROC   pglGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC   pglBindVertexArray;
static PFNGLDELETEVERTEXARRAYSPROC pglDeleteVertexArrays;
static PFNGLVERTEXATTRIBPOINTERPROC pglVertexAttribPointer;
static PFNGLENABLEVERTEXATTRIBARRAYPROC pglEnableVertexAttribArray;

static PFNGLGETUNIFORMLOCATIONPROC pglGetUniformLocation;
static PFNGLUNIFORM1IPROC          pglUniform1i;
static PFNGLUNIFORM1FPROC          pglUniform1f;
static PFNGLUNIFORM2FPROC          pglUniform2f;
static PFNGLUNIFORM3FPROC          pglUniform3f;
static PFNGLUNIFORMMATRIX4FVPROC   pglUniformMatrix4fv;

static PFNGLGENFRAMEBUFFERSPROC      pglGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC      pglBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC pglFramebufferTexture2D;
static PFNGLGENRENDERBUFFERSPROC     pglGenRenderbuffers;
static PFNGLBINDRENDERBUFFERPROC     pglBindRenderbuffer;
static PFNGLRENDERBUFFERSTORAGEPROC  pglRenderbufferStorage;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC pglFramebufferRenderbuffer;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC  pglCheckFramebufferStatus;
static PFNGLDELETEFRAMEBUFFERSPROC   pglDeleteFramebuffers;
static PFNGLDELETERENDERBUFFERSPROC  pglDeleteRenderbuffers;

static PFNGLACTIVETEXTUREPROC        pglActiveTexture;

#define gl(name) pgl##name

static void *gl_get(const char *name) {
    void *p = SDL_GL_GetProcAddress(name);
    if (!p) fprintf(stderr, "[gfx] glGetProcAddress: %s introuvable\n", name);
    return p;
}

#define L(P, N) p##N = (P) gl_get(#N)

static bool gfx_load_funcs(void) {
    L(PFNGLCREATESHADERPROC, glCreateShader);
    L(PFNGLSHADERSOURCEPROC, glShaderSource);
    L(PFNGLCOMPILESHADERPROC, glCompileShader);
    L(PFNGLGETSHADERIVPROC, glGetShaderiv);
    L(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
    L(PFNGLCREATEPROGRAMPROC, glCreateProgram);
    L(PFNGLATTACHSHADERPROC, glAttachShader);
    L(PFNGLLINKPROGRAMPROC, glLinkProgram);
    L(PFNGLGETPROGRAMIVPROC, glGetProgramiv);
    L(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
    L(PFNGLUSEPROGRAMPROC, glUseProgram);
    L(PFNGLDELETESHADERPROC, glDeleteShader);
    L(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
    L(PFNGLGENBUFFERSPROC, glGenBuffers);
    L(PFNGLBINDBUFFERPROC, glBindBuffer);
    L(PFNGLBUFFERDATAPROC, glBufferData);
    L(PFNGLBUFFERSUBDATAPROC, glBufferSubData);
    L(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);
    L(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays);
    L(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);
    L(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays);
    L(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
    L(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
    L(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    L(PFNGLUNIFORM1IPROC, glUniform1i);
    L(PFNGLUNIFORM1FPROC, glUniform1f);
    L(PFNGLUNIFORM2FPROC, glUniform2f);
    L(PFNGLUNIFORM3FPROC, glUniform3f);
    L(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);
    L(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
    L(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
    L(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D);
    L(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers);
    L(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer);
    L(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage);
    L(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer);
    L(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus);
    L(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);
    L(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers);
    L(PFNGLACTIVETEXTUREPROC, glActiveTexture);
    return pglCreateShader != NULL && pglGenVertexArrays != NULL;
}
#undef L

/* ============================================================
 *  Math vec3 / mat4
 * ============================================================ */
v3 v3_make(float x, float y, float z) { v3 v = { x, y, z }; return v; }
v3 v3_add (v3 a, v3 b) { return v3_make(a.x+b.x, a.y+b.y, a.z+b.z); }
v3 v3_sub (v3 a, v3 b) { return v3_make(a.x-b.x, a.y-b.y, a.z-b.z); }
v3 v3_scl (v3 a, float s) { return v3_make(a.x*s, a.y*s, a.z*s); }
float v3_dot(v3 a, v3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
float v3_len(v3 a) { return sqrtf(v3_dot(a, a)); }
v3 v3_norm(v3 a) {
    float l = v3_len(a);
    if (l < 1e-6f) return v3_make(0, 0, 0);
    return v3_scl(a, 1.f / l);
}
v3 v3_cross(v3 a, v3 b) {
    return v3_make(a.y*b.z - a.z*b.y,
                   a.z*b.x - a.x*b.z,
                   a.x*b.y - a.y*b.x);
}

m4 m4_identity(void) {
    m4 m = {{0}};
    m.m[0]=m.m[5]=m.m[10]=m.m[15]=1.f;
    return m;
}

m4 m4_mul(m4 a, m4 b) {
    m4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0.f;
            for (int k = 0; k < 4; k++)
                s += a.m[k*4 + rr] * b.m[c*4 + k];
            r.m[c*4 + rr] = s;
        }
    return r;
}

m4 m4_perspective(float fovy, float aspect, float n, float f) {
    m4 m = {{0}};
    float t = 1.f / tanf(fovy * 0.5f);
    m.m[0]  = t / aspect;
    m.m[5]  = t;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1.f;
    m.m[14] = (2.f * f * n) / (n - f);
    return m;
}

m4 m4_lookat(v3 eye, v3 center, v3 up) {
    v3 f = v3_norm(v3_sub(center, eye));
    v3 s = v3_norm(v3_cross(f, up));
    v3 u = v3_cross(s, f);
    m4 m = m4_identity();
    m.m[0]=s.x; m.m[4]=s.y; m.m[8]=s.z;
    m.m[1]=u.x; m.m[5]=u.y; m.m[9]=u.z;
    m.m[2]=-f.x; m.m[6]=-f.y; m.m[10]=-f.z;
    m.m[12] = -v3_dot(s, eye);
    m.m[13] = -v3_dot(u, eye);
    m.m[14] =  v3_dot(f, eye);
    return m;
}

m4 m4_ortho(float l, float r, float b, float t, float n, float f) {
    m4 m = {{0}};
    m.m[0]  =  2.f / (r - l);
    m.m[5]  =  2.f / (t - b);
    m.m[10] = -2.f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    m.m[15] =  1.f;
    return m;
}

m4 m4_translate(v3 t) {
    m4 m = m4_identity();
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

m4 m4_scale(v3 s) {
    m4 m = m4_identity();
    m.m[0] = s.x; m.m[5] = s.y; m.m[10] = s.z;
    return m;
}

/* unproject : pixel (win_x, win_y) -> ray world. (0,0) en haut a gauche. */
void gfx_unproject(int win_x, int win_y, int win_w, int win_h,
                   m4 view, m4 proj, v3 *out_origin, v3 *out_dir) {
    /* NDC */
    float nx = ((float)win_x / win_w) * 2.f - 1.f;
    float ny = 1.f - ((float)win_y / win_h) * 2.f;
    /* inverse de proj * view */
    m4 vp = m4_mul(proj, view);
    /* inversion 4x4 generique */
    float a[16]; memcpy(a, vp.m, sizeof(a));
    float inv[16];
    inv[0]= a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4]=-a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8]= a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12]=-a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1]=-a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5]= a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9]=-a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13]= a[0]*a[9]*a[14] - a[0]*a[10]*a[13]- a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2]= a[1]*a[6]*a[15]  - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    inv[6]=-a[0]*a[6]*a[15]  + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    inv[10]=a[0]*a[5]*a[15]  - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    inv[14]=-a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
    inv[3]=-a[1]*a[6]*a[11]  + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
    inv[7]= a[0]*a[6]*a[11]  - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
    inv[11]=-a[0]*a[5]*a[11] + a[0]*a[7]*a[9]  + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
    inv[15]=a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]  - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
    float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (fabsf(det) < 1e-9f) { *out_origin = v3_make(0,0,0); *out_dir = v3_make(0,0,-1); return; }
    float invd = 1.f / det;
    for (int i = 0; i < 16; i++) inv[i] *= invd;

    /* deux points : near (z=-1) et far (z=1) */
    float p1[4] = { nx, ny, -1.f, 1.f };
    float p2[4] = { nx, ny,  1.f, 1.f };
    float w1[4], w2[4];
    for (int i = 0; i < 4; i++) {
        w1[i] = inv[i + 0] * p1[0] + inv[i + 4] * p1[1] + inv[i + 8] * p1[2] + inv[i + 12] * p1[3];
        w2[i] = inv[i + 0] * p2[0] + inv[i + 4] * p2[1] + inv[i + 8] * p2[2] + inv[i + 12] * p2[3];
    }
    if (w1[3] != 0.f) { w1[0]/=w1[3]; w1[1]/=w1[3]; w1[2]/=w1[3]; }
    if (w2[3] != 0.f) { w2[0]/=w2[3]; w2[1]/=w2[3]; w2[2]/=w2[3]; }
    out_origin->x = w1[0]; out_origin->y = w1[1]; out_origin->z = w1[2];
    v3 dir = v3_norm(v3_make(w2[0]-w1[0], w2[1]-w1[1], w2[2]-w1[2]));
    *out_dir = dir;
}

/* ============================================================
 *  Shaders embarques
 * ============================================================ */
static const char *VS_TERR =
"#version 330 core\n"
"layout (location = 0) in vec3 a_pos;\n"
"layout (location = 1) in vec3 a_normal;\n"
"layout (location = 2) in vec3 a_color;\n"
"uniform mat4 u_view;\n"
"uniform mat4 u_proj;\n"
"out vec3 v_pos;\n"
"out vec3 v_normal;\n"
"out vec3 v_color;\n"
"void main() {\n"
"    v_pos = a_pos;\n"
"    v_normal = a_normal;\n"
"    v_color = a_color;\n"
"    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);\n"
"}\n";

static const char *FS_TERR =
"#version 330 core\n"
"in vec3 v_pos;\n"
"in vec3 v_normal;\n"
"in vec3 v_color;\n"
"out vec4 frag;\n"
"uniform vec3 u_light_dir;\n"
"uniform vec3 u_player_pos;\n"
"uniform vec3 u_fog_color;\n"
"uniform vec3 u_sky_color;\n"
"\n"
"/* === Procedural noise functions === */\n"
"float hash21(vec2 p) {\n"
"    p = fract(p * vec2(123.34, 456.21));\n"
"    p += dot(p, p + 45.32);\n"
"    return fract(p.x * p.y);\n"
"}\n"
"float value_noise(vec2 p) {\n"
"    vec2 i = floor(p), f = fract(p);\n"
"    float a = hash21(i);\n"
"    float b = hash21(i + vec2(1, 0));\n"
"    float c = hash21(i + vec2(0, 1));\n"
"    float d = hash21(i + vec2(1, 1));\n"
"    vec2 u = f * f * (3.0 - 2.0 * f);   /* smoothstep */\n"
"    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n"
"}\n"
"/* fbm 4 octaves */\n"
"float fbm(vec2 p) {\n"
"    float v = 0.0, amp = 0.5;\n"
"    for (int i = 0; i < 4; i++) {\n"
"        v += amp * value_noise(p);\n"
"        p *= 2.0; amp *= 0.5;\n"
"    }\n"
"    return v;\n"
"}\n"
"/* Voronoi cellular noise -- distance au point le plus proche */\n"
"vec2 voronoi(vec2 p) {\n"
"    vec2 i = floor(p), f = fract(p);\n"
"    float md1 = 8.0, md2 = 8.0;\n"
"    vec2 closest;\n"
"    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {\n"
"        vec2 g = vec2(x, y);\n"
"        float h = hash21(i + g);\n"
"        vec2 cell = g + vec2(h, fract(h * 31.7)) - f;\n"
"        float d = dot(cell, cell);\n"
"        if (d < md1) { md2 = md1; md1 = d; closest = i + g; }\n"
"        else if (d < md2) md2 = d;\n"
"    }\n"
"    return vec2(sqrt(md1), sqrt(md2) - sqrt(md1));\n"
"}\n"
"\n"
"void main() {\n"
"    vec3 n = normalize(v_normal);\n"
"    vec3 ld = normalize(u_light_dir);\n"
"    float ndotl = max(dot(n, ld), 0.0);\n"
"    \n"
"    /* === Procedural texturing par face ===\n"
"     * Selon l'orientation, on choisit le plan UV (XZ pour top/bottom,\n"
"     * XY pour faces +/-Z, YZ pour faces +/-X). Voronoi pour effet\n"
"     * stone-tile, fbm pour color variation. */\n"
"    vec2 uv;\n"
"    if (abs(n.y) > 0.7) uv = v_pos.xz * 1.5;\n"
"    else if (abs(n.x) > 0.7) uv = v_pos.yz * 2.0;\n"
"    else uv = v_pos.xy * 2.0;\n"
"    \n"
"    /* Voronoi : joints de pierre (mortier sombre sur edges)\n"
"     * vor.y = distance entre les 2 cellules les plus proches\n"
"     * faible -> on est pres d'un joint -> sombre */\n"
"    vec2 vor = voronoi(uv * 0.6);\n"
"    float mortar = smoothstep(0.0, 0.08, vor.y);\n"
"    \n"
"    /* fbm : variation organique de couleur (taches, mousses) */\n"
"    float n2 = fbm(uv * 1.5);\n"
"    \n"
"    /* Combine : mortier sombre + fbm variation */\n"
"    vec3 col = v_color;\n"
"    col *= (0.55 + 0.45 * mortar);          /* mortier */\n"
"    col *= (0.85 + 0.30 * n2);              /* fbm shade */\n"
"    \n"
"    /* Lambert + ambient hemispherique (cool fill from below) */\n"
"    vec3 diffuse = col * (0.25 + 0.75 * ndotl);\n"
"    float hemi = 0.5 + 0.5 * n.y;\n"
"    diffuse += col * u_sky_color * hemi * 0.12;\n"
"    \n"
"    /* torch radial chaude */\n"
"    float pd = length(v_pos.xz - u_player_pos.xz);\n"
"    float plight = clamp(1.0 - pd / 6.5, 0.0, 1.0);\n"
"    plight = plight * plight;\n"
"    diffuse += vec3(0.55, 0.38, 0.18) * plight;\n"
"    \n"
"    /* Fog dense quadratique (Valheim) */\n"
"    float fog_d = max((pd - 8.0) / 14.0, 0.0);\n"
"    float fog_t = clamp(1.0 - fog_d * fog_d, 0.0, 1.0);\n"
"    diffuse = mix(u_fog_color, diffuse, fog_t);\n"
"    frag = vec4(diffuse, 1.0);\n"
"}\n";

static const char *VS_BB =
"#version 330 core\n"
"layout (location = 0) in vec3 a_pos;\n"
"layout (location = 1) in vec3 a_normal;\n"
"layout (location = 2) in vec3 a_color;\n"
"uniform mat4 u_view;\n"
"uniform mat4 u_proj;\n"
"uniform mat4 u_model;\n"
"uniform vec3 u_tint;\n"
"out vec3 v_normal;\n"
"out vec3 v_color;\n"
"out vec3 v_world;\n"
"void main() {\n"
"    v_normal = a_normal;\n"
"    v_color = a_color * u_tint;\n"
"    vec4 wp = u_model * vec4(a_pos, 1.0);\n"
"    v_world = wp.xyz;\n"
"    gl_Position = u_proj * u_view * wp;\n"
"}\n";

static const char *FS_BB =
"#version 330 core\n"
"in vec3 v_normal;\n"
"in vec3 v_color;\n"
"in vec3 v_world;\n"
"out vec4 frag;\n"
"uniform vec3 u_light_dir;\n"
"uniform vec3 u_player_pos;\n"
"uniform vec3 u_fog_color;\n"
"uniform vec3 u_sky_color;\n"
"uniform vec3 u_cam_pos;\n"
"void main() {\n"
"    vec3 n = normalize(v_normal);\n"
"    vec3 ld = normalize(u_light_dir);\n"
"    float ndotl = max(dot(n, ld), 0.0);\n"
"    vec3 col = v_color * (0.35 + 0.65 * ndotl);\n"
"    /* hemi ambient depuis le ciel (subtil) */\n"
"    float hemi = 0.5 + 0.5 * n.y;\n"
"    col += v_color * u_sky_color * hemi * 0.10;\n"
"    /* Rim light : fresnel-like, plus marque aux silhouettes.\n"
"     * Eclaircit les bords avec la couleur du ciel, donne un look\n"
"     * stylise (les entites se decoupent sur le fond). */\n"
"    vec3 view_dir = normalize(u_cam_pos - v_world);\n"
"    float rim = 1.0 - max(dot(n, view_dir), 0.0);\n"
"    rim = pow(rim, 2.5);\n"
"    col += u_sky_color * rim * 0.35;\n"
"    /* fog matche le terrain pour cohesion */\n"
"    float pd = length(v_world.xz - u_player_pos.xz);\n"
"    float fog_d = max((pd - 8.0) / 14.0, 0.0);\n"
"    float fog_t = clamp(1.0 - fog_d * fog_d, 0.0, 1.0);\n"
"    col = mix(u_fog_color, col, fog_t);\n"
"    frag = vec4(col, 1.0);\n"
"}\n";

static const char *VS_UI =
"#version 330 core\n"
"layout (location = 0) in vec2 a_pos;\n"
"layout (location = 1) in vec2 a_uv;\n"
"layout (location = 2) in vec4 a_col;\n"
"uniform mat4 u_proj;\n"
"out vec4 v_col;\n"
"void main() {\n"
"    v_col = a_col;\n"
"    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
"}\n";

static const char *FS_UI =
"#version 330 core\n"
"in vec4 v_col;\n"
"out vec4 frag;\n"
"void main() { frag = v_col; }\n";

/* === POST-PROCESS shaders ===
 * VS_POST partage par bright/blur/composite : fullscreen triangle
 * "trick" -- 3 vertex sans VBO, on calcule la position depuis
 * gl_VertexID. Simple et rapide. */
static const char *VS_POST =
"#version 330 core\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    /* triangle plein-ecran : sommets a (-1,-1), (3,-1), (-1,3).\n"
"     * uv interpolees couvrent [0,1]x[0,1]. */\n"
"    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,\n"
"                  (gl_VertexID == 2) ? 3.0 : -1.0);\n"
"    v_uv = (p + 1.0) * 0.5;\n"
"    gl_Position = vec4(p, 0.0, 1.0);\n"
"}\n";

/* Bright pass : extrait les pixels au-dessus du seuil avec une
 * transition douce (smoothstep). Sortie = couleur des highlights
 * uniquement, le reste a zero. */
static const char *FS_BRIGHT =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"uniform sampler2D u_scene;\n"
"void main() {\n"
"    vec3 c = texture(u_scene, v_uv).rgb;\n"
"    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
"    /* seuil 0.65 -> 1.0 : extrait progressivement */\n"
"    float w = smoothstep(0.65, 1.0, lum);\n"
"    frag = vec4(c * w, 1.0);\n"
"}\n";

/* Gaussian blur 1D (9 taps, sigma ~2.0). u_direction est
 * (1,0)/texel pour horizontal, (0,1)/texel pour vertical. */
static const char *FS_BLUR =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"uniform sampler2D u_tex;\n"
"uniform vec2 u_direction;\n"
"void main() {\n"
"    /* coefficients gaussiens normalises 9 taps */\n"
"    float w0 = 0.227027;\n"
"    float w1 = 0.194594;\n"
"    float w2 = 0.121622;\n"
"    float w3 = 0.054054;\n"
"    float w4 = 0.016216;\n"
"    vec3 c = texture(u_tex, v_uv).rgb * w0;\n"
"    c += texture(u_tex, v_uv + u_direction * 1.0).rgb * w1;\n"
"    c += texture(u_tex, v_uv - u_direction * 1.0).rgb * w1;\n"
"    c += texture(u_tex, v_uv + u_direction * 2.0).rgb * w2;\n"
"    c += texture(u_tex, v_uv - u_direction * 2.0).rgb * w2;\n"
"    c += texture(u_tex, v_uv + u_direction * 3.0).rgb * w3;\n"
"    c += texture(u_tex, v_uv - u_direction * 3.0).rgb * w3;\n"
"    c += texture(u_tex, v_uv + u_direction * 4.0).rgb * w4;\n"
"    c += texture(u_tex, v_uv - u_direction * 4.0).rgb * w4;\n"
"    frag = vec4(c, 1.0);\n"
"}\n";

/* Composite final : scene + 2 niveaux de bloom (large + serre),
 * ACES, biome grading, vignette renforcee, aberration chromatique,
 * film grain. La couche "lab" qui pousse le rendu en cinematique. */
static const char *FS_COMPOSITE =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"uniform sampler2D u_scene;\n"
"uniform sampler2D u_bloom;\n"
"uniform sampler2D u_bloom_wide;\n"
"uniform vec3 u_shadow_tint;\n"
"uniform vec3 u_highlight_tint;\n"
"uniform float u_exposure;\n"
"uniform float u_bloom_intensity;\n"
"uniform float u_time;\n"
"vec3 aces(vec3 x) {\n"
"    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;\n"
"    return clamp((x * (a*x + b)) / (x * (c*x + d) + e), 0.0, 1.0);\n"
"}\n"
"/* hash 2D pour le grain */\n"
"float hash21(vec2 p) {\n"
"    p = fract(p * vec2(123.34, 456.21));\n"
"    p += dot(p, p + 45.32);\n"
"    return fract(p.x * p.y);\n"
"}\n"
"void main() {\n"
"    /* Aberration chromatique : decale legerement R et B depuis le\n"
"     * centre. Plus forte aux coins via la distance au centre. */\n"
"    vec2 d = v_uv - 0.5;\n"
"    float dist2 = dot(d, d);\n"
"    float ca = dist2 * 0.012;\n"
"    vec2 offs = normalize(d + vec2(0.0001, 0.0)) * ca;\n"
"    vec3 scn;\n"
"    scn.r = texture(u_scene, v_uv + offs).r;\n"
"    scn.g = texture(u_scene, v_uv         ).g;\n"
"    scn.b = texture(u_scene, v_uv - offs).b;\n"
"    /* 2 niveaux de bloom : serre (precis) + large (halo) */\n"
"    vec3 bl1 = texture(u_bloom,      v_uv).rgb;\n"
"    vec3 bl2 = texture(u_bloom_wide, v_uv).rgb;\n"
"    vec3 c = scn + bl1 * u_bloom_intensity + bl2 * (u_bloom_intensity * 0.55);\n"
"    /* exposure -> ACES */\n"
"    c *= u_exposure;\n"
"    c = aces(c);\n"
"    /* color grading par biome (lum-based mix) */\n"
"    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
"    vec3 shadow = c * u_shadow_tint;\n"
"    vec3 highlight = c * u_highlight_tint;\n"
"    c = mix(shadow, highlight, lum);\n"
"    /* contrast lift : sigmoid douce pour pousser noir/blanc */\n"
"    c = mix(c, smoothstep(0.0, 1.0, c), 0.15);\n"
"    /* vignette plus marquee (-20% aux coins) */\n"
"    float vig = 1.0 - dot(d, d) * 1.05;\n"
"    vig = clamp(vig, 0.0, 1.0);\n"
"    c *= vig;\n"
"    /* film grain anime, modere */\n"
"    float g = hash21(v_uv * 1024.0 + u_time * 60.0);\n"
"    c += (g - 0.5) * 0.025;\n"
"    frag = vec4(c, 1.0);\n"
"}\n";

/* === SPARKLE shader === Quad billboard cam-facing, gradient
 * circulaire en alpha pour creer le look "paillette additive" Valheim.
 * VS : prend un quad [-0.5,0.5] et l'oriente face camera + scale + translate.
 * FS : alpha = smoothstep(1.0, 0.2, length(uv)) * intensity. */
static const char *VS_SPARKLE =
"#version 330 core\n"
"layout (location = 0) in vec2 a_quad;\n"
"uniform mat4 u_view;\n"
"uniform mat4 u_proj;\n"
"uniform vec3 u_pos;\n"
"uniform float u_size;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"    /* extrait right / up de la view matrix (lignes 0 et 1) */\n"
"    vec3 right = vec3(u_view[0][0], u_view[1][0], u_view[2][0]);\n"
"    vec3 up    = vec3(u_view[0][1], u_view[1][1], u_view[2][1]);\n"
"    vec3 world = u_pos + (right * a_quad.x + up * a_quad.y) * u_size;\n"
"    v_uv = a_quad * 2.0;\n"
"    gl_Position = u_proj * u_view * vec4(world, 1.0);\n"
"}\n";

static const char *FS_SPARKLE =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"uniform vec3 u_color;\n"
"uniform float u_alpha;\n"
"void main() {\n"
"    float d = length(v_uv);\n"
"    /* coeur brillant + falloff smoothstep */\n"
"    float core = smoothstep(0.0, 0.3, 1.0 - d);\n"
"    float halo = smoothstep(0.0, 1.0, 1.0 - d);\n"
"    float a = (core * 0.7 + halo * 0.3) * u_alpha;\n"
"    /* coeur plus brillant que la couleur */\n"
"    vec3 col = u_color + vec3(core * 0.5);\n"
"    frag = vec4(col, a);\n"
"}\n";

/* Sky gradient : remplit le FBO avec un degrade vertical fog -> sky
 * avant le terrain. Donne un "horizon" subtle au lieu d'un fond plat. */
static const char *FS_SKY =
"#version 330 core\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"uniform vec3 u_fog_color;\n"
"uniform vec3 u_sky_color;\n"
"void main() {\n"
"    /* v_uv.y = 0 en haut, 1 en bas (post triangle).\n"
"     * sky en haut, fog en bas, mix non lineaire. */\n"
"    float t = 1.0 - v_uv.y;\n"
"    t = smoothstep(0.0, 1.0, t);\n"
"    vec3 c = mix(u_fog_color, u_sky_color * 1.15, t);\n"
"    frag = vec4(c, 1.0);\n"
"}\n";

static GLuint compile_shader(GLenum kind, const char *src) {
    GLuint s = pglCreateShader(kind);
    pglShaderSource(s, 1, &src, NULL);
    pglCompileShader(s);
    GLint ok = 0;
    pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n = 0;
        pglGetShaderInfoLog(s, sizeof(log), &n, log);
        fprintf(stderr, "[gfx] shader compile fail (%s):\n%s\n",
                kind == GL_VERTEX_SHADER ? "VS" : "FS", log);
        return 0;
    }
    return s;
}
static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = pglCreateProgram();
    pglAttachShader(p, vs);
    pglAttachShader(p, fs);
    pglLinkProgram(p);
    GLint ok = 0;
    pglGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n = 0;
        pglGetProgramInfoLog(p, sizeof(log), &n, log);
        fprintf(stderr, "[gfx] program link fail:\n%s\n", log);
        return 0;
    }
    return p;
}
static GLuint make_program(const char *vs_src, const char *fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    GLuint p = link_program(vs, fs);
    pglDeleteShader(vs); pglDeleteShader(fs);
    return p;
}

/* ============================================================
 *  Cube mesh : 36 vertices avec normales et vertex colors AO-ish
 * ============================================================ */
static void cube_mesh_init(GfxCtx *gc) {
    /* 6 faces, 2 tris par face. position en [0,1]^3, normales standard.
       Couleur = teinte par face pour rendre les arretes visibles (style Minecraft). */
    static const float V[36 * 9] = {
    /* +X face (right) - tint mid */
    1,0,0,  1,0,0, 0.78f,0.78f,0.78f,
    1,1,0,  1,0,0, 0.78f,0.78f,0.78f,
    1,1,1,  1,0,0, 0.78f,0.78f,0.78f,
    1,0,0,  1,0,0, 0.78f,0.78f,0.78f,
    1,1,1,  1,0,0, 0.78f,0.78f,0.78f,
    1,0,1,  1,0,0, 0.78f,0.78f,0.78f,
    /* -X face (left) - tint dark */
    0,0,1,  -1,0,0, 0.55f,0.55f,0.55f,
    0,1,1,  -1,0,0, 0.55f,0.55f,0.55f,
    0,1,0,  -1,0,0, 0.55f,0.55f,0.55f,
    0,0,1,  -1,0,0, 0.55f,0.55f,0.55f,
    0,1,0,  -1,0,0, 0.55f,0.55f,0.55f,
    0,0,0,  -1,0,0, 0.55f,0.55f,0.55f,
    /* +Y face (top) - tint bright */
    0,1,0,   0,1,0, 1.0f,1.0f,1.0f,
    0,1,1,   0,1,0, 1.0f,1.0f,1.0f,
    1,1,1,   0,1,0, 1.0f,1.0f,1.0f,
    0,1,0,   0,1,0, 1.0f,1.0f,1.0f,
    1,1,1,   0,1,0, 1.0f,1.0f,1.0f,
    1,1,0,   0,1,0, 1.0f,1.0f,1.0f,
    /* -Y face (bottom) - tint very dark */
    0,0,1,   0,-1,0, 0.35f,0.35f,0.35f,
    0,0,0,   0,-1,0, 0.35f,0.35f,0.35f,
    1,0,0,   0,-1,0, 0.35f,0.35f,0.35f,
    0,0,1,   0,-1,0, 0.35f,0.35f,0.35f,
    1,0,0,   0,-1,0, 0.35f,0.35f,0.35f,
    1,0,1,   0,-1,0, 0.35f,0.35f,0.35f,
    /* +Z face (front) - tint mid-dark */
    0,0,1,   0,0,1, 0.65f,0.65f,0.65f,
    1,0,1,   0,0,1, 0.65f,0.65f,0.65f,
    1,1,1,   0,0,1, 0.65f,0.65f,0.65f,
    0,0,1,   0,0,1, 0.65f,0.65f,0.65f,
    1,1,1,   0,0,1, 0.65f,0.65f,0.65f,
    0,1,1,   0,0,1, 0.65f,0.65f,0.65f,
    /* -Z face (back) - tint mid */
    1,0,0,   0,0,-1, 0.7f,0.7f,0.7f,
    0,0,0,   0,0,-1, 0.7f,0.7f,0.7f,
    0,1,0,   0,0,-1, 0.7f,0.7f,0.7f,
    1,0,0,   0,0,-1, 0.7f,0.7f,0.7f,
    0,1,0,   0,0,-1, 0.7f,0.7f,0.7f,
    1,1,0,   0,0,-1, 0.7f,0.7f,0.7f,
    };
    pglGenVertexArrays(1, &gc->cube_vao);
    pglGenBuffers(1, &gc->cube_vbo);
    pglBindVertexArray(gc->cube_vao);
    pglBindBuffer(GL_ARRAY_BUFFER, gc->cube_vbo);
    pglBufferData(GL_ARRAY_BUFFER, sizeof(V), V, GL_STATIC_DRAW);
    pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                           (const void*)(0));
    pglVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                           (const void*)(3 * sizeof(float)));
    pglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                           (const void*)(6 * sizeof(float)));
    pglEnableVertexAttribArray(0);
    pglEnableVertexAttribArray(1);
    pglEnableVertexAttribArray(2);
    gc->cube_vert_count = 36;
}

/* ============================================================
 *  UI batcher (quads colores en ortho)
 *  Format vertex : x, y, u, v, r, g, b, a (8 floats), 6 verts par quad.
 * ============================================================ */
#define UI_MAX_QUADS 16384

static void ui_init(GfxCtx *gc) {
    gc->ui_quad_max = UI_MAX_QUADS;
    gc->ui_quad_count = 0;
    gc->ui_buf = (float *)malloc((size_t)UI_MAX_QUADS * 6 * 8 * sizeof(float));
    pglGenVertexArrays(1, &gc->ui_vao);
    pglGenBuffers(1, &gc->ui_vbo);
    pglBindVertexArray(gc->ui_vao);
    pglBindBuffer(GL_ARRAY_BUFFER, gc->ui_vbo);
    pglBufferData(GL_ARRAY_BUFFER,
                  (GLsizeiptr)((size_t)UI_MAX_QUADS * 6 * 8 * sizeof(float)),
                  NULL, GL_DYNAMIC_DRAW);
    pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                           (const void*)(0));
    pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                           (const void*)(2 * sizeof(float)));
    pglVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                           (const void*)(4 * sizeof(float)));
    pglEnableVertexAttribArray(0);
    pglEnableVertexAttribArray(1);
    pglEnableVertexAttribArray(2);
}

static void ui_flush(GfxCtx *gc) {
    if (gc->ui_quad_count <= 0) return;
    pglUseProgram(gc->ui_prog);
    /* projection ortho fbo : (0,0) en haut-gauche, +Y vers le bas */
    m4 proj = m4_ortho(0, (float)gc->fbo_w, (float)gc->fbo_h, 0, -1, 1);
    GLint loc = pglGetUniformLocation(gc->ui_prog, "u_proj");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, proj.m);

    pglBindVertexArray(gc->ui_vao);
    pglBindBuffer(GL_ARRAY_BUFFER, gc->ui_vbo);
    pglBufferSubData(GL_ARRAY_BUFFER, 0,
                     (GLsizeiptr)(gc->ui_quad_count * 6 * 8 * sizeof(float)),
                     gc->ui_buf);
    glDrawArrays(GL_TRIANGLES, 0, gc->ui_quad_count * 6);
    gc->ui_quad_count = 0;
}

static void ui_push_quad(GfxCtx *gc, float x, float y, float w, float h,
                         float r, float g, float b, float a) {
    if (gc->ui_quad_count >= gc->ui_quad_max) ui_flush(gc);
    float *p = &gc->ui_buf[gc->ui_quad_count * 6 * 8];
    /* tri 1 : tl, br, tr */
    /* tri 2 : tl, bl, br */
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
#define V(X, Y) *p++ = (X); *p++ = (Y); *p++ = 0; *p++ = 0; \
                *p++ = r; *p++ = g; *p++ = b; *p++ = a;
    V(x0, y0); V(x1, y1); V(x1, y0);
    V(x0, y0); V(x0, y1); V(x1, y1);
#undef V
    gc->ui_quad_count++;
}

void gfx_ui_begin(GfxCtx *gc) {
    glDisable(GL_DEPTH_TEST);
    if (gc->blend_on) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    gc->ui_quad_count = 0;
}

void gfx_ui_end(GfxCtx *gc) {
    ui_flush(gc);
    glDisable(GL_BLEND);
}

void gfx_set_color(GfxCtx *gc, uint32_t rgba) {
    gc->cur_r = ((rgba >> 24) & 0xFF) / 255.f;
    gc->cur_g = ((rgba >> 16) & 0xFF) / 255.f;
    gc->cur_b = ((rgba >>  8) & 0xFF) / 255.f;
    gc->cur_a = ((rgba      ) & 0xFF) / 255.f;
}

void gfx_set_blend(GfxCtx *gc, bool on) {
    gc->blend_on = on;
    if (on) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void gfx_fill_rect(GfxCtx *gc, int x, int y, int w, int h) {
    ui_push_quad(gc, (float)x, (float)y, (float)w, (float)h,
                 gc->cur_r, gc->cur_g, gc->cur_b, gc->cur_a);
}

/* ============================================================
 *  init / shutdown
 * ============================================================ */
bool gfx_init(GfxCtx *gc, SDL_Window *win, int fbo_w, int fbo_h, int win_w, int win_h) {
    memset(gc, 0, sizeof(*gc));
    gc->fbo_w = fbo_w; gc->fbo_h = fbo_h;
    gc->win_w = win_w; gc->win_h = win_h;
    gc->light_dir = v3_norm(v3_make(0.4f, 1.0f, 0.6f));
    gc->cur_r = gc->cur_g = gc->cur_b = gc->cur_a = 1.f;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    gc->gl_ctx = SDL_GL_CreateContext(win);
    if (!gc->gl_ctx) {
        fprintf(stderr, "[gfx] SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);

    if (!gfx_load_funcs()) {
        fprintf(stderr, "[gfx] echec chargement des fonctions GL\n");
        return false;
    }

    gc->terr_prog = make_program(VS_TERR, FS_TERR);
    gc->bb_prog   = make_program(VS_BB,   FS_BB);
    gc->ui_prog   = make_program(VS_UI,   FS_UI);
    gc->bright_prog    = make_program(VS_POST, FS_BRIGHT);
    gc->blur_prog      = make_program(VS_POST, FS_BLUR);
    gc->composite_prog = make_program(VS_POST, FS_COMPOSITE);
    gc->sky_prog       = make_program(VS_POST, FS_SKY);
    gc->sparkle_prog   = make_program(VS_SPARKLE, FS_SPARKLE);
    if (!gc->terr_prog || !gc->bb_prog || !gc->ui_prog ||
        !gc->bright_prog || !gc->blur_prog || !gc->composite_prog ||
        !gc->sky_prog) {
        fprintf(stderr, "[gfx] shader programs absents\n");
        return false;
    }
    /* grading defaults : neutre, exposure 1, bloom 0.55 (Valheim-style) */
    gc->grade_shadow    = v3_make(0.85f, 0.92f, 1.10f);
    gc->grade_highlight = v3_make(1.08f, 1.02f, 0.92f);
    gc->grade_exposure  = 1.10f;
    gc->bloom_intensity = 0.55f;
    /* atmosphere defaults (nuit froide), override par render_world */
    gc->fog_color = v3_make(0.06f, 0.05f, 0.10f);
    gc->sky_color = v3_make(0.30f, 0.32f, 0.45f);

    /* terrain VBO/VAO (donne plus tard via gfx_terrain_upload) */
    pglGenVertexArrays(1, &gc->terr_vao);
    pglGenBuffers(1, &gc->terr_vbo);
    pglBindVertexArray(gc->terr_vao);
    pglBindBuffer(GL_ARRAY_BUFFER, gc->terr_vbo);
    pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                           (const void*)(0));
    pglVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                           (const void*)(3 * sizeof(float)));
    pglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                           (const void*)(6 * sizeof(float)));
    pglEnableVertexAttribArray(0);
    pglEnableVertexAttribArray(1);
    pglEnableVertexAttribArray(2);

    cube_mesh_init(gc);
    /* === Pyramide 4 cotes (base [0,1]^2 a y=0, apex (0.5, 1, 0.5)) === */
    {
        /* 4 cotes triangulaires + base (2 tris) = 6 tris = 18 verts.
         * Normales approx pour faces triangulaires inclinees. */
        const float nx = 0.894f, ny = 0.447f;   /* slope ~63 deg */
        static const float V[18 * 9] = {
            /* +Z front */
            0,0,1,  0,ny,nx, 1,1,1,
            1,0,1,  0,ny,nx, 1,1,1,
            0.5f,1,0.5f, 0,ny,nx, 1,1,1,
            /* +X right */
            1,0,1,  nx,ny,0, 0.9f,0.9f,0.9f,
            1,0,0,  nx,ny,0, 0.9f,0.9f,0.9f,
            0.5f,1,0.5f, nx,ny,0, 0.9f,0.9f,0.9f,
            /* -Z back */
            1,0,0,  0,ny,-nx, 0.85f,0.85f,0.85f,
            0,0,0,  0,ny,-nx, 0.85f,0.85f,0.85f,
            0.5f,1,0.5f, 0,ny,-nx, 0.85f,0.85f,0.85f,
            /* -X left */
            0,0,0,  -nx,ny,0, 0.95f,0.95f,0.95f,
            0,0,1,  -nx,ny,0, 0.95f,0.95f,0.95f,
            0.5f,1,0.5f, -nx,ny,0, 0.95f,0.95f,0.95f,
            /* base (under) 2 triangles */
            0,0,0, 0,-1,0, 0.5f,0.5f,0.5f,
            1,0,0, 0,-1,0, 0.5f,0.5f,0.5f,
            1,0,1, 0,-1,0, 0.5f,0.5f,0.5f,
            0,0,0, 0,-1,0, 0.5f,0.5f,0.5f,
            1,0,1, 0,-1,0, 0.5f,0.5f,0.5f,
            0,0,1, 0,-1,0, 0.5f,0.5f,0.5f,
        };
        pglGenVertexArrays(1, &gc->pyr_vao);
        pglGenBuffers(1, &gc->pyr_vbo);
        pglBindVertexArray(gc->pyr_vao);
        pglBindBuffer(GL_ARRAY_BUFFER, gc->pyr_vbo);
        pglBufferData(GL_ARRAY_BUFFER, sizeof(V), V, GL_STATIC_DRAW);
        pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)0);
        pglVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)(3*sizeof(float)));
        pglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)(6*sizeof(float)));
        pglEnableVertexAttribArray(0); pglEnableVertexAttribArray(1); pglEnableVertexAttribArray(2);
        gc->pyr_vert_count = 18;
    }
    /* === Octaedre (diamant) : centre (0.5,0.5,0.5), rayon 0.5 ===
     * Apex top (0.5,1,0.5), bottom (0.5,0,0.5), 4 equateur
     * (1,0.5,0.5) (0,0.5,0.5) (0.5,0.5,1) (0.5,0.5,0).
     * 8 faces triangulaires. */
    {
        const float s = 0.577f;        /* 1/sqrt(3) pour normales unitaires */
        static const float V[24 * 9] = {
            /* TOP-4 triangles (apex haut) -- 4 faces */
            /* +X+Z */
            0.5f,1,0.5f, 0.577f,0.577f,0.577f, 1,1,1,
            1.0f,0.5f,0.5f, 0.577f,0.577f,0.577f, 1,1,1,
            0.5f,0.5f,1.0f, 0.577f,0.577f,0.577f, 1,1,1,
            /* +X-Z */
            0.5f,1,0.5f, 0.577f,0.577f,-0.577f, 0.95f,0.95f,0.95f,
            0.5f,0.5f,0.0f, 0.577f,0.577f,-0.577f, 0.95f,0.95f,0.95f,
            1.0f,0.5f,0.5f, 0.577f,0.577f,-0.577f, 0.95f,0.95f,0.95f,
            /* -X-Z */
            0.5f,1,0.5f, -0.577f,0.577f,-0.577f, 0.90f,0.90f,0.90f,
            0.0f,0.5f,0.5f, -0.577f,0.577f,-0.577f, 0.90f,0.90f,0.90f,
            0.5f,0.5f,0.0f, -0.577f,0.577f,-0.577f, 0.90f,0.90f,0.90f,
            /* -X+Z */
            0.5f,1,0.5f, -0.577f,0.577f,0.577f, 0.92f,0.92f,0.92f,
            0.5f,0.5f,1.0f, -0.577f,0.577f,0.577f, 0.92f,0.92f,0.92f,
            0.0f,0.5f,0.5f, -0.577f,0.577f,0.577f, 0.92f,0.92f,0.92f,
            /* BOTTOM-4 triangles (apex bas) */
            /* +X+Z */
            0.5f,0,0.5f, 0.577f,-0.577f,0.577f, 0.65f,0.65f,0.65f,
            0.5f,0.5f,1.0f, 0.577f,-0.577f,0.577f, 0.65f,0.65f,0.65f,
            1.0f,0.5f,0.5f, 0.577f,-0.577f,0.577f, 0.65f,0.65f,0.65f,
            /* +X-Z */
            0.5f,0,0.5f, 0.577f,-0.577f,-0.577f, 0.62f,0.62f,0.62f,
            1.0f,0.5f,0.5f, 0.577f,-0.577f,-0.577f, 0.62f,0.62f,0.62f,
            0.5f,0.5f,0.0f, 0.577f,-0.577f,-0.577f, 0.62f,0.62f,0.62f,
            /* -X-Z */
            0.5f,0,0.5f, -0.577f,-0.577f,-0.577f, 0.60f,0.60f,0.60f,
            0.5f,0.5f,0.0f, -0.577f,-0.577f,-0.577f, 0.60f,0.60f,0.60f,
            0.0f,0.5f,0.5f, -0.577f,-0.577f,-0.577f, 0.60f,0.60f,0.60f,
            /* -X+Z */
            0.5f,0,0.5f, -0.577f,-0.577f,0.577f, 0.63f,0.63f,0.63f,
            0.0f,0.5f,0.5f, -0.577f,-0.577f,0.577f, 0.63f,0.63f,0.63f,
            0.5f,0.5f,1.0f, -0.577f,-0.577f,0.577f, 0.63f,0.63f,0.63f,
        };
        (void)s;
        pglGenVertexArrays(1, &gc->oct_vao);
        pglGenBuffers(1, &gc->oct_vbo);
        pglBindVertexArray(gc->oct_vao);
        pglBindBuffer(GL_ARRAY_BUFFER, gc->oct_vbo);
        pglBufferData(GL_ARRAY_BUFFER, sizeof(V), V, GL_STATIC_DRAW);
        pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)0);
        pglVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)(3*sizeof(float)));
        pglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)(6*sizeof(float)));
        pglEnableVertexAttribArray(0); pglEnableVertexAttribArray(1); pglEnableVertexAttribArray(2);
        gc->oct_vert_count = 24;
    }
    /* === Cone 8 cotes (radius 0.5, height 1) en [0,1]^2 base + apex (0.5,1,0.5) ===
     * Genere dynamiquement : 8 triangles cote + 8 triangles base (radial). */
    {
        const int N = 8;
        float V[N * 6 * 9];     /* 8 sides + 8 base, 3 verts each */
        int n = 0;
        for (int i = 0; i < N; i++) {
            float a0 = (i     / (float)N) * 6.2831f;
            float a1 = ((i+1) / (float)N) * 6.2831f;
            float x0 = 0.5f + cosf(a0) * 0.5f;
            float z0 = 0.5f + sinf(a0) * 0.5f;
            float x1 = 0.5f + cosf(a1) * 0.5f;
            float z1 = 0.5f + sinf(a1) * 0.5f;
            /* normale moyenne : pointe vers l'exterieur, leg incline */
            float nxa = cosf((a0 + a1) * 0.5f) * 0.89f;
            float nza = sinf((a0 + a1) * 0.5f) * 0.89f;
            float nya = 0.45f;
            /* cote */
            float *p = &V[n * 9]; n++;
            p[0]=x0; p[1]=0; p[2]=z0; p[3]=nxa; p[4]=nya; p[5]=nza;
            p[6]=1; p[7]=1; p[8]=1;
            p = &V[n * 9]; n++;
            p[0]=x1; p[1]=0; p[2]=z1; p[3]=nxa; p[4]=nya; p[5]=nza;
            p[6]=1; p[7]=1; p[8]=1;
            p = &V[n * 9]; n++;
            p[0]=0.5f; p[1]=1; p[2]=0.5f; p[3]=nxa; p[4]=nya; p[5]=nza;
            p[6]=1; p[7]=1; p[8]=1;
            /* base radial (under) */
            p = &V[n * 9]; n++;
            p[0]=0.5f; p[1]=0; p[2]=0.5f; p[3]=0; p[4]=-1; p[5]=0;
            p[6]=0.5f; p[7]=0.5f; p[8]=0.5f;
            p = &V[n * 9]; n++;
            p[0]=x1; p[1]=0; p[2]=z1; p[3]=0; p[4]=-1; p[5]=0;
            p[6]=0.5f; p[7]=0.5f; p[8]=0.5f;
            p = &V[n * 9]; n++;
            p[0]=x0; p[1]=0; p[2]=z0; p[3]=0; p[4]=-1; p[5]=0;
            p[6]=0.5f; p[7]=0.5f; p[8]=0.5f;
        }
        pglGenVertexArrays(1, &gc->cone_vao);
        pglGenBuffers(1, &gc->cone_vbo);
        pglBindVertexArray(gc->cone_vao);
        pglBindBuffer(GL_ARRAY_BUFFER, gc->cone_vbo);
        pglBufferData(GL_ARRAY_BUFFER, sizeof(V), V, GL_STATIC_DRAW);
        pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)0);
        pglVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)(3*sizeof(float)));
        pglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (const void*)(6*sizeof(float)));
        pglEnableVertexAttribArray(0); pglEnableVertexAttribArray(1); pglEnableVertexAttribArray(2);
        gc->cone_vert_count = N * 6;
    }
    /* === Sparkle quad : 6 verts (2 triangles), unit space [-0.5, 0.5] === */
    {
        static const float Q[6 * 2] = {
            -0.5f, -0.5f,
             0.5f, -0.5f,
             0.5f,  0.5f,
            -0.5f, -0.5f,
             0.5f,  0.5f,
            -0.5f,  0.5f,
        };
        pglGenVertexArrays(1, &gc->sparkle_vao);
        pglGenBuffers(1, &gc->sparkle_vbo);
        pglBindVertexArray(gc->sparkle_vao);
        pglBindBuffer(GL_ARRAY_BUFFER, gc->sparkle_vbo);
        pglBufferData(GL_ARRAY_BUFFER, sizeof(Q), Q, GL_STATIC_DRAW);
        pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (const void*)0);
        pglEnableVertexAttribArray(0);
    }
    ui_init(gc);

    /* offscreen FBO pour pixel-art look */
    pglGenFramebuffers(1, &gc->fbo);
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->fbo);
    glGenTextures(1, &gc->fbo_color);
    glBindTexture(GL_TEXTURE_2D, gc->fbo_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fbo_w, fbo_h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            gc->fbo_color, 0);
    pglGenRenderbuffers(1, &gc->fbo_depth);
    pglBindRenderbuffer(GL_RENDERBUFFER, gc->fbo_depth);
    pglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, fbo_w, fbo_h);
    pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                               gc->fbo_depth);
    GLenum st = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[gfx] FBO incomplete: 0x%x\n", st);
        return false;
    }
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* === POST-PROCESS : 5 FBOs ===
     *   0=bright (half-res)   1=blur_a (half)    2=blur_b (half)
     *   3=qblur_a (quart)     4=qblur_b (quart)
     * Le bloom large utilise les 2 FBOs quart-res pour un halo plus
     * spread (style "lens dirty"). */
    gc->post_w  = fbo_w / 2;
    gc->post_h  = fbo_h / 2;
    gc->post_qw = fbo_w / 4;
    gc->post_qh = fbo_h / 4;
    pglGenFramebuffers(5, gc->post_fbo);
    glGenTextures(5, gc->post_tex);
    for (int i = 0; i < 5; i++) {
        int pw = (i < 3) ? gc->post_w  : gc->post_qw;
        int ph = (i < 3) ? gc->post_h  : gc->post_qh;
        pglBindFramebuffer(GL_FRAMEBUFFER, gc->post_fbo[i]);
        glBindTexture(GL_TEXTURE_2D, gc->post_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, pw, ph, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, gc->post_tex[i], 0);
        if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[gfx] post FBO %d incomplete\n", i);
            return false;
        }
    }
    /* Scene FBO sample : on doit pouvoir LINEAR-filtrer pour le bright pass.
     * On remplace NEAREST par LINEAR sur fbo_color (effet : upscale lisse,
     * mais le "pixel-art chunky" reste car la resolution interne est basse). */
    glBindTexture(GL_TEXTURE_2D, gc->fbo_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* fullscreen VAO (vide -- VS_POST utilise gl_VertexID) */
    pglGenVertexArrays(1, &gc->fs_vao);
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    /* culling desactive : le winding cube/terrain n'est pas garanti
       coherent ; le surplus de fillrate est negligeable pour un donjon. */
    glDisable(GL_CULL_FACE);
    return true;
}

void gfx_shutdown(GfxCtx *gc) {
    if (gc->ui_buf) free(gc->ui_buf);
    if (gc->terr_prog) pglDeleteProgram(gc->terr_prog);
    if (gc->bb_prog)   pglDeleteProgram(gc->bb_prog);
    if (gc->ui_prog)   pglDeleteProgram(gc->ui_prog);
    if (gc->terr_vbo)  pglDeleteBuffers(1, &gc->terr_vbo);
    if (gc->cube_vbo)  pglDeleteBuffers(1, &gc->cube_vbo);
    if (gc->ui_vbo)    pglDeleteBuffers(1, &gc->ui_vbo);
    if (gc->terr_vao)  pglDeleteVertexArrays(1, &gc->terr_vao);
    if (gc->cube_vao)  pglDeleteVertexArrays(1, &gc->cube_vao);
    if (gc->pyr_vao)   pglDeleteVertexArrays(1, &gc->pyr_vao);
    if (gc->pyr_vbo)   pglDeleteBuffers(1, &gc->pyr_vbo);
    if (gc->oct_vao)   pglDeleteVertexArrays(1, &gc->oct_vao);
    if (gc->oct_vbo)   pglDeleteBuffers(1, &gc->oct_vbo);
    if (gc->cone_vao)  pglDeleteVertexArrays(1, &gc->cone_vao);
    if (gc->cone_vbo)  pglDeleteBuffers(1, &gc->cone_vbo);
    if (gc->sparkle_vao) pglDeleteVertexArrays(1, &gc->sparkle_vao);
    if (gc->sparkle_vbo) pglDeleteBuffers(1, &gc->sparkle_vbo);
    if (gc->sparkle_prog) pglDeleteProgram(gc->sparkle_prog);
    if (gc->ui_vao)    pglDeleteVertexArrays(1, &gc->ui_vao);
    if (gc->fbo_color) glDeleteTextures(1, &gc->fbo_color);
    if (gc->fbo_depth) pglDeleteRenderbuffers(1, &gc->fbo_depth);
    if (gc->fbo)       pglDeleteFramebuffers(1, &gc->fbo);
    /* post chain (5 FBOs : half + quart) */
    if (gc->post_tex[0]) glDeleteTextures(5, gc->post_tex);
    if (gc->post_fbo[0]) pglDeleteFramebuffers(5, gc->post_fbo);
    if (gc->fs_vao)      pglDeleteVertexArrays(1, &gc->fs_vao);
    if (gc->bright_prog)    pglDeleteProgram(gc->bright_prog);
    if (gc->blur_prog)      pglDeleteProgram(gc->blur_prog);
    if (gc->composite_prog) pglDeleteProgram(gc->composite_prog);
    if (gc->sky_prog)       pglDeleteProgram(gc->sky_prog);
    if (gc->gl_ctx) SDL_GL_DeleteContext(gc->gl_ctx);
}

void gfx_frame_begin(GfxCtx *gc) {
    /* bind FBO offscreen */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->fbo);
    glViewport(0, 0, gc->fbo_w, gc->fbo_h);
    glClear(GL_DEPTH_BUFFER_BIT);
    /* Sky gradient en pass full-screen (remplace le clear color flat).
     * Donne un horizon stylise fog-sky meme dans les arenes ouvertes. */
    glDisable(GL_DEPTH_TEST);
    pglUseProgram(gc->sky_prog);
    pglUniform3f(pglGetUniformLocation(gc->sky_prog, "u_fog_color"),
                 gc->fog_color.x, gc->fog_color.y, gc->fog_color.z);
    pglUniform3f(pglGetUniformLocation(gc->sky_prog, "u_sky_color"),
                 gc->sky_color.x, gc->sky_color.y, gc->sky_color.z);
    pglBindVertexArray(gc->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}

void gfx_set_viewport_rect(GfxCtx *gc, int x, int y, int w, int h) {
    /* Viewport en coords FBO. Y inverse car GL utilise origin bottom-left. */
    int gly = gc->fbo_h - y - h;
    glViewport(x, gly, w, h);
}

void gfx_reset_viewport(GfxCtx *gc) {
    glViewport(0, 0, gc->fbo_w, gc->fbo_h);
}

void gfx_clear_depth_rect(GfxCtx *gc, int x, int y, int w, int h) {
    int gly = gc->fbo_h - y - h;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, gly, w, h);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void gfx_set_grading(GfxCtx *gc, v3 shadow, v3 highlight,
                     float exposure, float bloom) {
    gc->grade_shadow    = shadow;
    gc->grade_highlight = highlight;
    gc->grade_exposure  = exposure;
    gc->bloom_intensity = bloom;
}

static void post_draw_fullscreen(GfxCtx *gc) {
    pglBindVertexArray(gc->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void gfx_frame_end(GfxCtx *gc) {
    /* === POST-PROCESS chain ===
     * 1. bright pass : scene -> post[0] (half)
     * 2. blur H+V : post[0] -> post[1] -> post[2] (bloom serre, half)
     * 3. downsample post[2] -> post[3] (quart) puis blur H+V -> post[4]
     *    -> post[3] (bloom large, halo lens dirty)
     * 4. composite : scene + bloom_serre (post[2]) + bloom_large (post[3])
     *    + ACES + grading + chromatic ab + grain + vignette */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    /* Pass 1 : bright */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->post_fbo[0]);
    glViewport(0, 0, gc->post_w, gc->post_h);
    pglUseProgram(gc->bright_prog);
    pglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gc->fbo_color);
    pglUniform1i(pglGetUniformLocation(gc->bright_prog, "u_scene"), 0);
    post_draw_fullscreen(gc);

    /* Pass 2a : blur H half */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->post_fbo[1]);
    pglUseProgram(gc->blur_prog);
    glBindTexture(GL_TEXTURE_2D, gc->post_tex[0]);
    pglUniform1i(pglGetUniformLocation(gc->blur_prog, "u_tex"), 0);
    pglUniform2f(pglGetUniformLocation(gc->blur_prog, "u_direction"),
                 1.0f / (float)gc->post_w, 0.0f);
    post_draw_fullscreen(gc);

    /* Pass 2b : blur V half */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->post_fbo[2]);
    glBindTexture(GL_TEXTURE_2D, gc->post_tex[1]);
    pglUniform2f(pglGetUniformLocation(gc->blur_prog, "u_direction"),
                 0.0f, 1.0f / (float)gc->post_h);
    post_draw_fullscreen(gc);

    /* Pass 3a : downsample vers quart-res (juste blur H avec direction
     * negligeable, fait du LINEAR sampling) */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->post_fbo[4]);
    glViewport(0, 0, gc->post_qw, gc->post_qh);
    glBindTexture(GL_TEXTURE_2D, gc->post_tex[2]);
    pglUniform2f(pglGetUniformLocation(gc->blur_prog, "u_direction"),
                 1.0f / (float)gc->post_qw, 0.0f);
    post_draw_fullscreen(gc);

    /* Pass 3b : blur V quart */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->post_fbo[3]);
    glBindTexture(GL_TEXTURE_2D, gc->post_tex[4]);
    pglUniform2f(pglGetUniformLocation(gc->blur_prog, "u_direction"),
                 0.0f, 1.0f / (float)gc->post_qh);
    post_draw_fullscreen(gc);

    /* Pass 4 : composite final vers backbuffer */
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, gc->win_w, gc->win_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pglUseProgram(gc->composite_prog);
    pglActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gc->fbo_color);
    pglUniform1i(pglGetUniformLocation(gc->composite_prog, "u_scene"), 0);
    pglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gc->post_tex[2]);
    pglUniform1i(pglGetUniformLocation(gc->composite_prog, "u_bloom"), 1);
    pglActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gc->post_tex[3]);
    pglUniform1i(pglGetUniformLocation(gc->composite_prog, "u_bloom_wide"), 2);
    pglUniform3f(pglGetUniformLocation(gc->composite_prog, "u_shadow_tint"),
                 gc->grade_shadow.x, gc->grade_shadow.y, gc->grade_shadow.z);
    pglUniform3f(pglGetUniformLocation(gc->composite_prog, "u_highlight_tint"),
                 gc->grade_highlight.x, gc->grade_highlight.y, gc->grade_highlight.z);
    pglUniform1f(pglGetUniformLocation(gc->composite_prog, "u_exposure"),
                 gc->grade_exposure);
    pglUniform1f(pglGetUniformLocation(gc->composite_prog, "u_bloom_intensity"),
                 gc->bloom_intensity);
    pglUniform1f(pglGetUniformLocation(gc->composite_prog, "u_time"),
                 (float)((double)SDL_GetTicks() / 1000.0));
    post_draw_fullscreen(gc);
    pglActiveTexture(GL_TEXTURE0);
}

void gfx_set_camera(GfxCtx *gc, m4 view, m4 proj) {
    gc->view = view;
    gc->proj = proj;
    /* Extrait la position camera depuis l'inverse de la view matrix
     * (necessaire pour le rim light + futures features world-space).
     * Pour une lookAt, la translation est -R*T avec R rotation et T
     * position camera, donc T = -R^T * view.translation. */
    float r00 = view.m[0],  r10 = view.m[1],  r20 = view.m[2];
    float r01 = view.m[4],  r11 = view.m[5],  r21 = view.m[6];
    float r02 = view.m[8],  r12 = view.m[9],  r22 = view.m[10];
    float tx  = view.m[12], ty  = view.m[13], tz  = view.m[14];
    gc->cam_pos.x = -(r00 * tx + r10 * ty + r20 * tz);
    gc->cam_pos.y = -(r01 * tx + r11 * ty + r21 * tz);
    gc->cam_pos.z = -(r02 * tx + r12 * ty + r22 * tz);
}

/* ============================================================
 *  terrain mesh
 * ============================================================ */
void gfx_terrain_upload(GfxCtx *gc, const float *verts, int vert_count) {
    pglBindVertexArray(gc->terr_vao);
    pglBindBuffer(GL_ARRAY_BUFFER, gc->terr_vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vert_count * 9 * sizeof(float)),
                  verts, GL_STATIC_DRAW);
    gc->terr_vert_count = vert_count;
}

void gfx_set_atmosphere(GfxCtx *gc, v3 fog, v3 sky) {
    gc->fog_color = fog;
    gc->sky_color = sky;
}

void gfx_terrain_draw(GfxCtx *gc, v3 player_pos) {
    if (gc->terr_vert_count <= 0) return;
    gc->player_world_pos = player_pos;
    pglUseProgram(gc->terr_prog);
    GLint loc;
    loc = pglGetUniformLocation(gc->terr_prog, "u_view");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->view.m);
    loc = pglGetUniformLocation(gc->terr_prog, "u_proj");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->proj.m);
    loc = pglGetUniformLocation(gc->terr_prog, "u_light_dir");
    pglUniform3f(loc, gc->light_dir.x, gc->light_dir.y, gc->light_dir.z);
    loc = pglGetUniformLocation(gc->terr_prog, "u_player_pos");
    pglUniform3f(loc, player_pos.x, player_pos.y, player_pos.z);
    loc = pglGetUniformLocation(gc->terr_prog, "u_fog_color");
    pglUniform3f(loc, gc->fog_color.x, gc->fog_color.y, gc->fog_color.z);
    loc = pglGetUniformLocation(gc->terr_prog, "u_sky_color");
    pglUniform3f(loc, gc->sky_color.x, gc->sky_color.y, gc->sky_color.z);

    pglBindVertexArray(gc->terr_vao);
    glDrawArrays(GL_TRIANGLES, 0, gc->terr_vert_count);
}

/* ============================================================
 *  cube draw (entites)
 * ============================================================ */
void gfx_cube_draw(GfxCtx *gc, m4 model, float r, float g, float b) {
    pglUseProgram(gc->bb_prog);
    GLint loc;
    loc = pglGetUniformLocation(gc->bb_prog, "u_view");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->view.m);
    loc = pglGetUniformLocation(gc->bb_prog, "u_proj");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->proj.m);
    loc = pglGetUniformLocation(gc->bb_prog, "u_model");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, model.m);
    loc = pglGetUniformLocation(gc->bb_prog, "u_light_dir");
    pglUniform3f(loc, gc->light_dir.x, gc->light_dir.y, gc->light_dir.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_tint");
    pglUniform3f(loc, r, g, b);
    loc = pglGetUniformLocation(gc->bb_prog, "u_player_pos");
    pglUniform3f(loc, gc->player_world_pos.x, gc->player_world_pos.y, gc->player_world_pos.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_fog_color");
    pglUniform3f(loc, gc->fog_color.x, gc->fog_color.y, gc->fog_color.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_sky_color");
    pglUniform3f(loc, gc->sky_color.x, gc->sky_color.y, gc->sky_color.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_cam_pos");
    pglUniform3f(loc, gc->cam_pos.x, gc->cam_pos.y, gc->cam_pos.z);

    pglBindVertexArray(gc->cube_vao);
    glDrawArrays(GL_TRIANGLES, 0, gc->cube_vert_count);
}

void gfx_box_draw(GfxCtx *gc, v3 center, v3 size, float r, float g, float b) {
    /* le cube unite est en [0,1]^3 ; on translate puis scale pour le centrer */
    m4 t = m4_translate(v3_make(center.x - size.x * 0.5f,
                                center.y - size.y * 0.5f,
                                center.z - size.z * 0.5f));
    m4 s = m4_scale(size);
    m4 model = m4_mul(t, s);
    gfx_cube_draw(gc, model, r, g, b);
}

/* Helper interne : envoie tous les uniforms bb_prog et dessine un mesh
 * arbitraire avec un model matrix. Factorise pyr/oct/cone qui sont
 * tous des meshes en [0,1] avec attribs identiques au cube. */
static void draw_bb_mesh(GfxCtx *gc, m4 model, GLuint vao, int vert_count,
                         float r, float g, float b) {
    pglUseProgram(gc->bb_prog);
    GLint loc;
    loc = pglGetUniformLocation(gc->bb_prog, "u_view");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->view.m);
    loc = pglGetUniformLocation(gc->bb_prog, "u_proj");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->proj.m);
    loc = pglGetUniformLocation(gc->bb_prog, "u_model");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, model.m);
    loc = pglGetUniformLocation(gc->bb_prog, "u_light_dir");
    pglUniform3f(loc, gc->light_dir.x, gc->light_dir.y, gc->light_dir.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_tint");
    pglUniform3f(loc, r, g, b);
    loc = pglGetUniformLocation(gc->bb_prog, "u_player_pos");
    pglUniform3f(loc, gc->player_world_pos.x, gc->player_world_pos.y, gc->player_world_pos.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_fog_color");
    pglUniform3f(loc, gc->fog_color.x, gc->fog_color.y, gc->fog_color.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_sky_color");
    pglUniform3f(loc, gc->sky_color.x, gc->sky_color.y, gc->sky_color.z);
    loc = pglGetUniformLocation(gc->bb_prog, "u_cam_pos");
    pglUniform3f(loc, gc->cam_pos.x, gc->cam_pos.y, gc->cam_pos.z);
    pglBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vert_count);
}

void gfx_pyramid_draw(GfxCtx *gc, v3 center, v3 size,
                      float r, float g, float b) {
    m4 t = m4_translate(v3_make(center.x - size.x * 0.5f,
                                center.y - size.y * 0.5f,
                                center.z - size.z * 0.5f));
    m4 s = m4_scale(size);
    m4 model = m4_mul(t, s);
    draw_bb_mesh(gc, model, gc->pyr_vao, gc->pyr_vert_count, r, g, b);
}

void gfx_octahedron_draw(GfxCtx *gc, v3 center, v3 size,
                         float r, float g, float b) {
    m4 t = m4_translate(v3_make(center.x - size.x * 0.5f,
                                center.y - size.y * 0.5f,
                                center.z - size.z * 0.5f));
    m4 s = m4_scale(size);
    m4 model = m4_mul(t, s);
    draw_bb_mesh(gc, model, gc->oct_vao, gc->oct_vert_count, r, g, b);
}

void gfx_sparkle_draw(GfxCtx *gc, v3 pos, float size,
                      float r, float g, float b, float a) {
    /* Additive blending pour le look "paillette lumineuse". Etat
     * sauvegarde pour ne pas polluer le reste du frame. */
    GLboolean prev_depth_write;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_write);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);    /* additif */

    pglUseProgram(gc->sparkle_prog);
    GLint loc;
    loc = pglGetUniformLocation(gc->sparkle_prog, "u_view");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->view.m);
    loc = pglGetUniformLocation(gc->sparkle_prog, "u_proj");
    pglUniformMatrix4fv(loc, 1, GL_FALSE, gc->proj.m);
    loc = pglGetUniformLocation(gc->sparkle_prog, "u_pos");
    pglUniform3f(loc, pos.x, pos.y, pos.z);
    loc = pglGetUniformLocation(gc->sparkle_prog, "u_size");
    pglUniform1f(loc, size);
    loc = pglGetUniformLocation(gc->sparkle_prog, "u_color");
    pglUniform3f(loc, r, g, b);
    loc = pglGetUniformLocation(gc->sparkle_prog, "u_alpha");
    pglUniform1f(loc, a);

    pglBindVertexArray(gc->sparkle_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* restore state */
    glDisable(GL_BLEND);
    glDepthMask(prev_depth_write);
}

void gfx_cone_draw(GfxCtx *gc, v3 center, float radius, float height,
                   float r, float g, float b) {
    /* base diametre = 2*radius. Cone mesh en [0,1]^2 base, height 1. */
    m4 t = m4_translate(v3_make(center.x - radius,
                                center.y - height * 0.5f,
                                center.z - radius));
    m4 s = m4_scale(v3_make(2.f * radius, height, 2.f * radius));
    m4 model = m4_mul(t, s);
    draw_bb_mesh(gc, model, gc->cone_vao, gc->cone_vert_count, r, g, b);
}

void gfx_billboard_draw(GfxCtx *gc, v3 center, float w, float h,
                        float r, float g, float b, float a) {
    (void)a;
    /* approxime via une petite box verticale orientee camera : pour KISS,
       on dessine un box mince */
    gfx_box_draw(gc, center, v3_make(w, h, w * 0.2f), r, g, b);
}
