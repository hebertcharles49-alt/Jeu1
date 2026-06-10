/*
 * scps_crypt.c — ChaCha20 (D. J. Bernstein, domaine public) + FNV-1a 64.
 * Implémentation de référence compacte, sans dépendance.
 */
#include "scps_crypt.h"

/* La clé du jeu (32 octets) — assumée DANS le binaire : obfuscation, pas secret. */
static const uint8_t K[32] = {
    0x53,0x43,0x50,0x53, 0x9e,0x37,0x79,0xb9, 0x7f,0x4a,0x7c,0x15, 0xf3,0x9c,0xc0,0x60,
    0x6c,0x62,0x72,0x65, 0x85,0xae,0x67,0xbb, 0x67,0xe6,0x09,0x6a, 0x3b,0xa7,0xca,0x84 };

static inline uint32_t rotl(uint32_t x,int n){ return (x<<n)|(x>>(32-n)); }
static inline uint32_t ld32(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
#define QR(a,b,c,d) \
    a+=b; d^=a; d=rotl(d,16); c+=d; b^=c; b=rotl(b,12); \
    a+=b; d^=a; d=rotl(d, 8); c+=d; b^=c; b=rotl(b, 7);

static void chacha_block(uint32_t out[16], const uint32_t in[16]){
    uint32_t x[16];
    for (int i=0;i<16;i++) x[i]=in[i];
    for (int r=0;r<10;r++){                      /* 20 tours (10 doubles) */
        QR(x[0],x[4],x[ 8],x[12]) QR(x[1],x[5],x[ 9],x[13])
        QR(x[2],x[6],x[10],x[14]) QR(x[3],x[7],x[11],x[15])
        QR(x[0],x[5],x[10],x[15]) QR(x[1],x[6],x[11],x[12])
        QR(x[2],x[7],x[ 8],x[13]) QR(x[3],x[4],x[ 9],x[14])
    }
    for (int i=0;i<16;i++) out[i]=x[i]+in[i];
}

void scrypt_stream(uint64_t nonce, uint8_t *buf, size_t n){
    uint32_t st[16];
    st[0]=0x61707865u; st[1]=0x3320646eu; st[2]=0x79622d32u; st[3]=0x6b206574u;  /* "expand 32-byte k" */
    for (int i=0;i<8;i++) st[4+i]=ld32(K+4*i);
    st[12]=0; st[13]=0;                            /* compteur 64 bits */
    st[14]=(uint32_t)(nonce&0xffffffffu); st[15]=(uint32_t)(nonce>>32);
    size_t off=0;
    while (off<n){
        uint32_t ks[16]; chacha_block(ks,st);
        if (++st[12]==0) ++st[13];
        size_t m = (n-off<64)? n-off : 64;
        for (size_t i=0;i<m;i++)
            buf[off+i] ^= (uint8_t)(ks[i>>2] >> (8*(i&3)));
        off+=m;
    }
}

uint64_t scrypt_fnv1a(const void *p, size_t n){
    const uint8_t *b=(const uint8_t*)p;
    uint64_t h=0xcbf29ce484222325ull;
    for (size_t i=0;i<n;i++){ h^=b[i]; h*=0x100000001b3ull; }
    return h;
}
