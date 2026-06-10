/*
 * scps_save_io.c — crc32 + deflate sur miniz (voir scps_save_io.h).
 * Compilé avec la même config minimale que third_party/miniz.c.
 */
#include "scps_save_io.h"
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#include "miniz.h"
#include <stdlib.h>

uint32_t scps_crc32(const void *p, size_t n){
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, (const unsigned char*)p, n);
}

bool scps_zip(const void *src, size_t n, void **out, size_t *outn){
    if (!out || !outn) return false;
    *out=NULL; *outn=0;
    mz_ulong cap = mz_compressBound((mz_ulong)n);
    unsigned char *buf = (unsigned char*)malloc(cap ? cap : 1);
    if (!buf) return false;
    mz_ulong clen = cap;
    int rc = mz_compress(buf, &clen, (const unsigned char*)src, (mz_ulong)n);
    if (rc != MZ_OK){ free(buf); return false; }
    *out = buf; *outn = (size_t)clen;
    return true;
}

size_t scps_unzip(const void *src, size_t n, void *dst, size_t dstn){
    mz_ulong dlen = (mz_ulong)dstn;
    int rc = mz_uncompress((unsigned char*)dst, &dlen, (const unsigned char*)src, (mz_ulong)n);
    return (rc == MZ_OK) ? (size_t)dlen : 0;
}
