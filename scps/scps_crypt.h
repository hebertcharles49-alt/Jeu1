#ifndef SCPS_CRYPT_H
#define SCPS_CRYPT_H
/*
 * scps_crypt.h — CHIFFREMENT DES SAUVEGARDES (flux ChaCha20 + empreinte FNV-1a).
 *
 * Cadrage HONNÊTE : la clé vit dans le binaire (pas de mot de passe joueur) —
 * c'est une protection contre l'ÉDITION SAUVAGE et le save-scumming, pas un
 * secret cryptographique face à qui possède le code. Dans ce périmètre, on fait
 * les choses proprement : un vrai chiffre de flux (ChaCha20, nonce UNIQUE par
 * sauvegarde → deux sauvegardes identiques ne se ressemblent pas), et une
 * EMPREINTE du clair vérifiée au chargement — un octet altéré = refus net.
 */
#include <stdint.h>
#include <stddef.h>

/* XOR le tampon avec le flux ChaCha20 (clé interne du jeu, nonce par sauvegarde).
 * Symétrique : le même appel chiffre et déchiffre. */
void     scrypt_stream(uint64_t nonce, uint8_t *buf, size_t n);
/* Empreinte FNV-1a 64 du tampon (l'intégrité du CLAIR, stockée dans l'en-tête). */
uint64_t scrypt_fnv1a(const void *p, size_t n);

#endif /* SCPS_CRYPT_H */
