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
"void main() {\n"
"    /* lambert global doux */\n"
"    float diff = max(dot(normalize(v_normal), normalize(u_light_dir)), 0.45);\n"
"    vec3 col = v_color * diff;\n"
"    /* lampe-torche radiale autour du joueur (eclaire les couloirs) */\n"
"    float pd = length(v_pos.xz - u_player_pos.xz);\n"
"    float plight = clamp(1.0 - pd / 6.5, 0.0, 1.0);\n"
"    plight = plight * plight;\n"
"    col += vec3(0.40, 0.30, 0.18) * plight;\n"
"    /* fog plus lointain : 16+ tiles avant attenuation */\n"
"    float fog = clamp(1.0 - (pd - 16.0) / 22.0, 0.0, 1.0);\n"
"    col = mix(vec3(0.030, 0.020, 0.060), col, fog);\n"
"    frag = vec4(col, 1.0);\n"
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
"void main() {\n"
"    v_normal = a_normal;\n"
"    v_color = a_color * u_tint;\n"
"    gl_Position = u_proj * u_view * u_model * vec4(a_pos, 1.0);\n"
"}\n";

static const char *FS_BB =
"#version 330 core\n"
"in vec3 v_normal;\n"
"in vec3 v_color;\n"
"out vec4 frag;\n"
"uniform vec3 u_light_dir;\n"
"void main() {\n"
"    /* ambiance plus lumineuse pour que les entites soient visibles */\n"
"    float diff = max(dot(normalize(v_normal), normalize(u_light_dir)), 0.55);\n"
"    frag = vec4(v_color * diff, 1.0);\n"
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
    if (!gc->terr_prog || !gc->bb_prog || !gc->ui_prog) {
        fprintf(stderr, "[gfx] shader programs absents\n");
        return false;
    }

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
    if (gc->ui_vao)    pglDeleteVertexArrays(1, &gc->ui_vao);
    if (gc->fbo_color) glDeleteTextures(1, &gc->fbo_color);
    if (gc->fbo_depth) pglDeleteRenderbuffers(1, &gc->fbo_depth);
    if (gc->fbo)       pglDeleteFramebuffers(1, &gc->fbo);
    if (gc->gl_ctx) SDL_GL_DeleteContext(gc->gl_ctx);
}

void gfx_frame_begin(GfxCtx *gc) {
    /* bind FBO offscreen */
    pglBindFramebuffer(GL_FRAMEBUFFER, gc->fbo);
    glViewport(0, 0, gc->fbo_w, gc->fbo_h);
    glClearColor(0.025f, 0.018f, 0.045f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

void gfx_frame_end(GfxCtx *gc) {
    /* blit FBO -> default framebuffer en upscaling NEAREST */
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, gc->win_w, gc->win_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    /* dessine un fullscreen quad samplant fbo_color : on emule via UI quad
       avec une ortho [0..win_w, 0..win_h] et on charge un shader passthrough.
       Plus simple : on utilise glBlitFramebuffer si disponible. */
    /* On a pglBindFramebuffer mais pas glBlitFramebuffer charge. Implementation
       legere via ui_prog en colorant un quad par pixel ne marche pas (texture).
       On va plutot uploader fbo_color via une approche differente : on dessine
       un quad plein ecran en samplant la texture FBO depuis un shader de blit. */
    /* pour rester KISS : on switch sur glBlitFramebuffer */
    static PFNGLBLITFRAMEBUFFERPROC pglBlitFramebuffer = NULL;
    if (!pglBlitFramebuffer) {
        pglBlitFramebuffer =
            (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebuffer");
    }
    if (pglBlitFramebuffer) {
        static PFNGLBINDFRAMEBUFFERPROC pglBindFB = NULL;
        if (!pglBindFB) pglBindFB = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
        pglBindFB(GL_READ_FRAMEBUFFER, gc->fbo);
        pglBindFB(GL_DRAW_FRAMEBUFFER, 0);
        pglBlitFramebuffer(0, 0, gc->fbo_w, gc->fbo_h,
                           0, 0, gc->win_w, gc->win_h,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
        pglBindFB(GL_FRAMEBUFFER, 0);
    }
}

void gfx_set_camera(GfxCtx *gc, m4 view, m4 proj) {
    gc->view = view;
    gc->proj = proj;
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

void gfx_terrain_draw(GfxCtx *gc, v3 player_pos) {
    if (gc->terr_vert_count <= 0) return;
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

void gfx_billboard_draw(GfxCtx *gc, v3 center, float w, float h,
                        float r, float g, float b, float a) {
    (void)a;
    /* approxime via une petite box verticale orientee camera : pour KISS,
       on dessine un box mince */
    gfx_box_draw(gc, center, v3_make(w, h, w * 0.2f), r, g, b);
}
