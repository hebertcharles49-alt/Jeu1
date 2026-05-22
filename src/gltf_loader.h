/* gltf_loader.h - STUB pour glTF skeletal animation.
 *
 * Le parsing glTF complet (JSON + buffers binaires + accessors +
 * skeletal anim) demande un JSON parser et environ 500-800 lignes.
 * C'est un chantier dedie qui merite son propre commit.
 *
 * Cette interface POSE l'API que le futur loader exposera. Elle
 * retourne toujours false pour l'instant.
 *
 * Si tu veux la version reelle :
 *   - vendor cgltf.h depuis https://github.com/jkuhlmann/cgltf
 *   - 1 fichier ~6000 lignes, MIT license
 *   - includelo en local, define CGLTF_IMPLEMENTATION dans 1 .c
 *   - utilise cgltf_parse_file + cgltf_load_buffers
 *   - extrait attributes (POSITION, NORMAL, JOINTS_0, WEIGHTS_0)
 *   - extrait animations (channels avec target_path = "translation"
 *     / "rotation" / "scale") et samplers (LINEAR/STEP/CUBICSPLINE)
 *   - skinning : ajoute u_bones[N] uniforme + skin matrices ;
 *     vertex shader fait position = sum_i (weight_i * bone_i * pos)
 *
 * Pour des assets STATIQUES (sans skeleton), tu peux deja exporter
 * en OBJ depuis Blender et utiliser obj_loader.h -- ca couvre 80%
 * des besoins (decorations, props, weapons, structures). */

#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include <stdbool.h>

typedef struct {
    float   *verts;        /* pos 3f + normal 3f + color 3f (interleave) */
    int      vert_count;
    /* TODO : bone_ids (uint8_t * vert_count * 4), bone_weights (float * vert_count * 4) */
    /* TODO : bones (mat4 * bone_count) + animation channels */
} GltfMesh;

/* Renvoie false : non implemente, voir docstring ci-dessus. */
bool gltf_load_file(const char *path, GltfMesh *out);
void gltf_free(GltfMesh *m);

#endif

#ifdef GLTF_LOADER_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>

bool gltf_load_file(const char *path, GltfMesh *out) {
    (void)path; (void)out;
    fprintf(stderr, "[gltf] non implemente. Utilise obj_loader.h pour les "
                    "meshes statiques ou vendor cgltf.h pour le complet.\n");
    return false;
}

void gltf_free(GltfMesh *m) {
    if (!m) return;
    free(m->verts);
    m->verts = NULL;
    m->vert_count = 0;
}

#endif
