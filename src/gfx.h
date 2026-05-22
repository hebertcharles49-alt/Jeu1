/*
 * gfx.h - couche OpenGL 3.3 core minimale pour Element Dungeon
 *
 * - chargement des function pointers via SDL_GL_GetProcAddress
 * - shaders embarques (terrain, billboard, particle, ui)
 * - mesh voxel chunk (heightmap -> cube faces avec culling)
 * - batcher 2D pour l'UI (rects + texte bitmap via atlas font)
 * - math vec3 / mat4
 */
#ifndef GFX_H
#define GFX_H

#include <SDL.h>
#include <SDL_opengl.h>     /* defs GL legacy + GLuint, GLint, GLfloat, etc */
#include <stdbool.h>
#include <stdint.h>

/* ---------- math ---------- */
typedef struct { float x, y, z; } v3;
typedef struct { float m[16]; }   m4;   /* column-major */

v3 v3_make(float x, float y, float z);
v3 v3_add (v3 a, v3 b);
v3 v3_sub (v3 a, v3 b);
v3 v3_scl (v3 a, float s);
v3 v3_norm(v3 a);
v3 v3_cross(v3 a, v3 b);
float v3_dot(v3 a, v3 b);
float v3_len(v3 a);

m4 m4_identity(void);
m4 m4_mul(m4 a, m4 b);
m4 m4_perspective(float fovy, float aspect, float n, float f);
m4 m4_lookat(v3 eye, v3 center, v3 up);
m4 m4_ortho(float l, float r, float b, float t, float n, float f);
m4 m4_translate(v3 t);
m4 m4_scale(v3 s);

/* unproject screen pixel -> ray (origin + dir) en world space */
void gfx_unproject(int win_x, int win_y, int win_w, int win_h,
                   m4 view, m4 proj, v3 *out_origin, v3 *out_dir);

/* ---------- contexte global ---------- */
typedef struct GfxCtx {
    SDL_GLContext gl_ctx;

    /* shaders */
    GLuint terr_prog, bb_prog, ui_prog;

    /* terrain mesh (chunk) */
    GLuint terr_vao, terr_vbo;
    int    terr_vert_count;

    /* cube mesh partage (entites) */
    GLuint cube_vao, cube_vbo;
    int    cube_vert_count;

    /* UI batcher */
    GLuint ui_vao, ui_vbo;
    GLuint font_tex;
    int    ui_quad_count;
    int    ui_quad_max;
    float *ui_buf;       /* x,y,u,v,r,g,b,a per vertex, 6 verts per quad */

    /* offscreen FBO (rendu pixel-art chunky) */
    GLuint fbo;
    GLuint fbo_color;
    GLuint fbo_depth;
    int    fbo_w, fbo_h;

    /* === POST-PROCESS chain (bloom + ACES + grading) ===
     * bright : extract bright pixels from scene (threshold)
     * blur_a / blur_b : ping-pong gaussian blur (half-res)
     * fs_vao : fullscreen quad VBO
     * shaders : bright_prog, blur_prog (H + V via uniform),
     *           composite_prog (scene + bloom + ACES + grading) */
    GLuint post_fbo[5];          /* 0=bright, 1=blur_a, 2=blur_b, 3=q_a, 4=q_b */
    GLuint post_tex[5];          /* color textures matching fbos */
    int    post_w, post_h;       /* half-res */
    int    post_qw, post_qh;     /* quarter-res (wide bloom halo) */
    GLuint fs_vao, fs_vbo;
    GLuint bright_prog, blur_prog, composite_prog, sky_prog;
    /* grading uniforms (positionnees par gfx_set_grading depuis le
     * world render). Tint shadows / highlights + exposure. */
    v3     grade_shadow, grade_highlight;
    float  grade_exposure;
    float  bloom_intensity;

    /* viewport finale (fenetre) */
    int    win_w, win_h;

    /* etat batcher */
    float  cur_r, cur_g, cur_b, cur_a;
    bool   blend_on;

    /* matrices courantes */
    m4 view, proj;

    /* lighting + atmosphere */
    v3 light_dir;
    v3 fog_color;
    v3 sky_color;
    v3 player_world_pos;       /* relayee aux billboards via gfx_cube_draw */
    v3 cam_pos;                /* extraite de la view matrix par gfx_set_camera */
} GfxCtx;

/* ---------- init / shutdown ---------- */
bool gfx_init(GfxCtx *gc, SDL_Window *win, int fbo_w, int fbo_h, int win_w, int win_h);
void gfx_shutdown(GfxCtx *gc);

/* ---------- frame ---------- */
void gfx_frame_begin(GfxCtx *gc);
void gfx_frame_end  (GfxCtx *gc);   /* blit FBO -> backbuffer + present (caller fait SDL_GL_SwapWindow) */
void gfx_set_camera (GfxCtx *gc, m4 view, m4 proj);
/* Post-process : positionne le grading par biome (shadow_tint /
 * highlight_tint multiplicatif, exposure global). Lu par le shader
 * composite a la fin de la frame. */
void gfx_set_grading(GfxCtx *gc, v3 shadow, v3 highlight,
                     float exposure, float bloom);
/* Atmosphere : couleur du fog distance + couleur du ciel (ambient
 * hemispherique). Set en debut de frame depuis render_world. */
void gfx_set_atmosphere(GfxCtx *gc, v3 fog, v3 sky);
/* Limite le viewport a un rect en coords FBO (internes). Utile pour
 * rendre une scene 3D dans une zone UI (paperdoll inventaire).
 * Reset_viewport restore le viewport plein FBO. clear_depth efface la
 * profondeur dans le rect (pour ne pas voir la scene precedente). */
void gfx_set_viewport_rect(GfxCtx *gc, int x, int y, int w, int h);
void gfx_reset_viewport   (GfxCtx *gc);
void gfx_clear_depth_rect (GfxCtx *gc, int x, int y, int w, int h);

/* ---------- terrain chunk ---------- */
/* upload une mesh de chunk (positions 3f, normal 3f, color 3f par vertex) */
void gfx_terrain_upload(GfxCtx *gc, const float *verts, int vert_count);
void gfx_terrain_draw  (GfxCtx *gc, v3 player_pos);

/* ---------- entites (cube simple) ---------- */
/* dessine un cube colore unitaire transformee par model */
void gfx_cube_draw(GfxCtx *gc, m4 model, float r, float g, float b);

/* dessine une box rectangulaire (taille xyz, position center) - colore */
void gfx_box_draw(GfxCtx *gc, v3 center, v3 size, float r, float g, float b);

/* ---------- billboards (particles, sprites alpha) ---------- */
/* draw_quad_billboard : quad face camera positionne en world space */
void gfx_billboard_draw(GfxCtx *gc, v3 center, float w, float h,
                        float r, float g, float b, float a);

/* ---------- UI 2D (ortho) ---------- */
/* signatures compatibles avec l'ancien "fill_rect" SDL */
void gfx_ui_begin    (GfxCtx *gc);
void gfx_ui_end      (GfxCtx *gc);
void gfx_set_color   (GfxCtx *gc, uint32_t rgba);
void gfx_set_blend   (GfxCtx *gc, bool on);
void gfx_fill_rect   (GfxCtx *gc, int x, int y, int w, int h);
void gfx_blit_glyph  (GfxCtx *gc, int x, int y, int gx, int gy, int gw, int gh);

#endif
