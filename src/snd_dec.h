/* The smaller sound formats, decoded as they play (src/snd.c; the
 * converter's encoder is editor/q32x/lib/src/sound.cpp, and its tests run
 * this same code against it).
 *
 * A sound's format is in the top four bits of its table length (the length
 * is always in 11 kHz samples, as played):
 *   0  8-bit unsigned at 11 kHz: the mixer reads it as it is
 *   1  8-bit at half rate (5.5 kHz): every other sample; each plays twice
 *   2  4 bits a sample, in blocks of 256: each block its first sample's
 *      value before it (a byte), a table of 16 signed steps, then 128 bytes
 *      of nibbles (the first in the high half), each sample the one before
 *      plus its step (the converter keeps every sample in 0..255)
 *   3  both: the half-rate samples in 4 bits */
#ifndef SND_DEC_H
#define SND_DEC_H

/* (included after the integer types: the game's own, or <stdint.h>) */

#define SF_HALF  1
#define SF_PACK  2
#define SF_LEN(v) ((v) & 0x0FFFFFFF)
#define SF_FMT(v) ((uint32_t)(v) >> 28)
#define PK_BLOCK 256
#define PK_BYTES (1 + 16 + PK_BLOCK / 2)
#define SDEC_MAX 128                    /* the most samples one call decodes */

typedef struct {
    const uint8_t *data;
    int32_t  fmt, srclen;               /* srclen in stored samples */
    int32_t  ak;                        /* 4 bits: the last stored sample decoded (-1: none) */
    int32_t  av;                        /* its value */
} sdec_t;

static inline void sdec_start(sdec_t *d, const uint8_t *data, int fmt, int32_t len)
{
    d->data = data;
    d->fmt = fmt;
    d->srclen = fmt & SF_HALF ? (len + 1) >> 1 : len;
    d->ak = -1;
    d->av = 128;
}

/* n stored samples of a sound in 4 bits from sample i (the one after the
   last decoded) into dst; past its end, its last sample again. A byte at a
   time: two samples, each the one before plus its step */
static void pk_run(sdec_t *d, int32_t i, int32_t n, uint8_t *dst)
{
    int32_t last = d->srclen - 1;
    int v = d->av;
    while (n > 0) {
        const uint8_t *blk, *nb;
        const int8_t *tab;
        int32_t m;
        int j;
        if (i > last) {
            while (n-- > 0) *dst++ = v;
            break;
        }
        blk = d->data + (i / PK_BLOCK) * PK_BYTES;
        tab = (const int8_t *)blk + 1;
        j = i & (PK_BLOCK - 1);
        if (j == 0)
            v = blk[0];
        m = PK_BLOCK - j;
        if (m > n) m = n;
        if (m > last + 1 - i) m = last + 1 - i;
        i += m;
        n -= m;
        nb = blk + 17 + (j >> 1);
        if (j & 1) {
            v += tab[*nb++ & 15];
            *dst++ = v;
            m--;
        }
        for (; m >= 2; m -= 2) {
            int b = *nb++;
            v += tab[b >> 4];
            dst[0] = v;
            v += tab[b & 15];
            dst[1] = v;
            dst += 2;
        }
        if (m) {
            v += tab[*nb >> 4];
            *dst++ = v;
        }
    }
    d->ak = (i > last ? last : i - 1);
    d->av = v;
}

/* stored samples k .. k + cnt - 1 into dst: mostly from where the last call
   stopped, or its last sample again (half rate starts on it) */
static void pk_decode(sdec_t *d, int32_t k, int cnt, uint8_t *dst)
{
    static uint8_t skip[PK_BLOCK];
    if (k == d->ak && d->ak >= 0) {
        *dst++ = d->av;
        if (--cnt == 0) return;
        k++;
    } else if (k != d->ak + 1) {
        /* from the start of its block */
        int32_t b0 = k & ~(PK_BLOCK - 1);
        d->ak = b0 - 1;
        if (k > b0) pk_run(d, b0, k - b0, skip);
    }
    pk_run(d, k, cnt, dst);
}

/* (for the converter's tests and previews; the mixer plays formats 1 and 3
   from their stored samples) cnt samples at 11 kHz from pos into dst: half
   rate plays each stored sample twice */
static void sdec_run(sdec_t *d, int32_t pos, int cnt, uint8_t *dst)
{
    uint8_t tmp[PK_BLOCK];
    int j;
    int32_t k0;
    if (d->fmt == SF_PACK) {
        pk_decode(d, pos, cnt, dst);
        return;
    }
    if (!(d->fmt & SF_HALF)) {
        for (j = 0; j < cnt; j++) dst[j] = d->data[pos + j];
        return;
    }
    k0 = pos >> 1;
    {
        int need = ((pos + cnt - 1) >> 1) - k0 + 1;
        if (d->fmt & SF_PACK)
            pk_decode(d, k0, need, tmp);
        else
            for (j = 0; j < need; j++) tmp[j] = d->data[k0 + j];
    }
    for (j = 0; j < cnt; j++) dst[j] = tmp[((pos + j) >> 1) - k0];
}

#endif
