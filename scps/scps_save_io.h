#ifndef SCPS_SAVE_IO_H
#define SCPS_SAVE_IO_H
/*
 * scps_save_io.h — FONDATION D'E/S DE SAUVEGARDE (brief build §3)
 *
 * Compression de blocs d'état (deflate) + CRC32 d'intégrité, sur miniz vendoré.
 * Ce module NE FAIT PAS la sauvegarde — il pose les briques que le format
 * (magic+version+graine+jour+params+CRC, sections taguées, ChaCha20) emploie :
 * la sauvegarde chiffrée a déjà atterri ; ce socle l'OUTILLE pour compresser
 * les grosses sections (World, WorldEconomy) et vérifier l'en-tête.
 *
 * Aucune dépendance de plus : miniz est vendoré, compilé crc32+deflate seuls.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* CRC32 (miniz) d'un bloc — l'empreinte d'en-tête. */
uint32_t scps_crc32(const void *p, size_t n);

/* Compresse `n` octets de `src` → buffer ALLOUÉ (*out, à libérer par l'appelant ;
 * *outn = sa taille). false si allocation/compression échoue. */
bool scps_zip(const void *src, size_t n, void **out, size_t *outn);

/* Décompresse `n` octets de `src` vers `dst` (capacité `dstn` — la taille claire
 * est connue de l'appelant, stockée dans l'en-tête de section). Renvoie la
 * taille décompressée, ou 0 si échec / dépassement de capacité. */
size_t scps_unzip(const void *src, size_t n, void *dst, size_t dstn);

#endif /* SCPS_SAVE_IO_H */
