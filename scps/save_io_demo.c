/*
 * save_io_demo.c — banc auto-vérifiant : compresse/décompresse un bloc test et
 * vérifie le CRC32 round-trip (brief build §9.5). Sortie ≠ 0 si échec.
 */
#include "scps_save_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass=0, fail=0;
static void check(const char *what, int ok){
    printf("   %s %s\n", ok?"✓":"✗", what);
    if (ok) pass++; else fail++;
}

int main(void){
    printf("══ save_io : compression de bloc + CRC32 round-trip ══\n");

    /* Un bloc « état » plausible : motif répétitif (comme une struct pleine de
     * zéros et de petits entiers) → la compression doit MORDRE. */
    enum { N = 64*1024 };
    unsigned char *src = (unsigned char*)malloc(N);
    if (!src){ fprintf(stderr,"OOM\n"); return 2; }
    for (int i=0;i<N;i++) src[i] = (unsigned char)((i*7+ (i/137)) & 0x1f);   /* peu d'entropie */

    uint32_t crc_src = scps_crc32(src, N);

    void *zip=NULL; size_t zn=0;
    int zok = scps_zip(src, N, &zip, &zn);
    check("la compression réussit", zok && zip && zn>0);
    check("le bloc compressé est PLUS PETIT que le clair", zok && zn < (size_t)N);

    unsigned char *dst = (unsigned char*)malloc(N);
    if (!dst){ free(src); free(zip); return 2; }
    size_t dn = zok ? scps_unzip(zip, zn, dst, N) : 0;
    check("la décompression rend la taille d'origine", dn == (size_t)N);
    check("le clair est IDENTIQUE après round-trip", dn==(size_t)N && !memcmp(src,dst,N));
    check("le CRC32 du round-trip ÉGALE celui du clair", dn==(size_t)N && scps_crc32(dst,dn)==crc_src);

    /* Garde-fou : une capacité de destination INSUFFISANTE → refus net (0). */
    unsigned char tiny[16];
    size_t too = zok ? scps_unzip(zip, zn, tiny, sizeof tiny) : 1;
    check("une destination trop petite est REFUSÉE (0)", too==0);

    /* CRC32 : déterministe et SENSIBLE — un bit retourné change l'empreinte. */
    { uint32_t a=scps_crc32("SCPS",4), a2=scps_crc32("SCPS",4), b=scps_crc32("SCQS",4);
      check("crc32 est déterministe (même entrée → même empreinte)", a==a2 && a!=0);
      check("crc32 est sensible (un octet diffère → empreinte différente)", a!=b); }

    free(src); free(dst); free(zip);
    printf("══ BILAN : %d réussis, %d échoués ══\n", pass, fail);
    return fail ? 1 : 0;
}
