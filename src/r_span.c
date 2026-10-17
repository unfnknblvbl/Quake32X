/* Span drawers. Every rendered pixel is one framebuffer word (2 screen pixels). */
#include "r_local.h"

#define ZI_MIN (1 << 18)

static const int32_t recip[17] = {
    0, 65536, 32768, 21845, 16384, 13107, 10923, 9362, 8192,
    7282, 6554, 5958, 5461, 5041, 4681, 4369, 4096
};

static int32_t turbtab[256];     /* two periods, so turbtab + phase needs no wrap */

/* liquid textures copied from cartridge ROM to SDRAM: the warp reads them
 * in a scattered order, and ROM cache refills cost about 6x as much */
#define NLIQUID 2
static uint8_t liquid_copy[NLIQUID][64 * 64] __attribute__((aligned(16)));
static int liquid_tex[NLIQUID] = { -1, -1 };

static void copy_liquids(void)
{
    int i, k, n = 0;
    for (i = 0; i < lvl.numfaces && n < NLIQUID; i++) {
        const lface_t *f = &lvl.faces[i];
        int tex = lvl.texinfo[f->texinfo].tex;
        if (!(f->flags & FF_LIQUID))
            continue;
        for (k = 0; k < n; k++)
            if (liquid_tex[k] == tex) break;
        if (k < n)
            continue;
        memcpy(liquid_copy[n], lvl.base + lvl.textures[tex].mip[0], 64 * 64);
        liquid_tex[n++] = tex;
    }
}

static inline const uint8_t *liquid_texels(int tex)
{
    int k;
    for (k = 0; k < NLIQUID; k++)
        if (liquid_tex[k] == tex)
            return liquid_copy[k];
    return lvl.base + lvl.textures[tex].mip[0];
}
static uint16_t dbltab[256];      /* index (signed byte + 128) -> doubled pixel */

#ifndef HOST
extern const uint8_t oram_code_start[], oram_code_end[];
extern void span_tex(texargs_t *a);
#define ORAM_BASE 0xC0000000
#define ORAM_FN(f) ((void *)(ORAM_BASE + ((uint32_t)(f) - (uint32_t)oram_code_start)))
static void (*span_tex_p)(texargs_t *);

/* copy the hand-written loops into this CPU's on-chip RAM */
void r_oram_init(void)
{
#if !defined(ONCHIP) || defined(NO_ORAM_CODE)
    span_tex_p = span_tex;          /* compatible build: run the loop from SDRAM */
#else
    memcpy((void *)ORAM_BASE, oram_code_start, oram_code_end - oram_code_start);
    span_tex_p = ORAM_FN(span_tex);
#endif
}
#else
void r_oram_init(void) {}
#endif

void r_span_init(void)
{
    int i;
    for (i = 0; i < 256; i++)
        turbtab[i] = (8 << 16) + (fsin(i << 9) << 3);
    for (i = 0; i < 256; i++) {
        int c = (i - 128) & 0xFF;
        dbltab[i] = (uint16_t)((c << 8) | c);
    }
    copy_liquids();
}

static inline uint16_t dbl(int c) { return (uint16_t)((c << 8) | c); }

/* low clamp 1/256 texel, as in span_asm.s: interpolation between two
 * clamped ends can round a little below the lower one */
static inline fixed_t clampf(fixed_t v, fixed_t hi)
{
    return v < 0x100 ? 0x100 : v > hi ? hi : v;
}

#ifndef HOST
void draw_spans_tex(rcpu_t *rc, const frec_t *fr)
{
    texargs_t a;
    const surfcache_t *sc = fr->surf;
    a.dzidx = fr->dzidx; a.dszdx = fr->dszdx; a.dtzdx = fr->dtzdx;
    a.dzidy = fr->dzidy; a.dszdy = fr->dszdy; a.dtzdy = fr->dtzdy;
    a.zi0 = fr->zi0; a.sz0 = fr->sz0; a.tz0 = fr->tz0;
    a.smax = fr->smax; a.tmax = fr->tmax;
    a.src = sc->data;
    a.width = sc->width;
    a.dbl = dbltab + 128;
    a.fb = r_fb;
    a.spans = rc->spans + fr->span0;
    a.nspans = fr->nspan;
    span_tex_p(&a);
}
#else
void draw_spans_tex(rcpu_t *rc, const frec_t *fr)
{
    const surfcache_t *sc = fr->surf;
    const uint8_t *src = sc->data;
    const int sw = sc->width;
    const fixed_t smax = fr->smax, tmax = fr->tmax;
    const int32_t dzidx = fr->dzidx, dszdx = fr->dszdx, dtzdx = fr->dtzdx;
    int i;

    for (i = 0; i < fr->nspan; i++) {
        const span_t *sp = &rc->spans[fr->span0 + i];
        int x = sp->x0, y = sp->y, count = sp->x1 - sp->x0;
        volatile uint16_t *dst = r_fb + y * SCREENW + x;
        uint32_t zi = (uint32_t)fr->zi0 + x * dzidx + y * fr->dzidy;
        uint32_t sz = (uint32_t)fr->sz0 + x * dszdx + y * fr->dszdy;
        uint32_t tz = (uint32_t)fr->tz0 + x * dtzdx + y * fr->dtzdy;
        int32_t z16, s, t, sn, tn, sstep, tstep;

        rc->pixels += count;
        z16 = hw_div64((int64_t)1 << 48, (int32_t)zi < ZI_MIN ? ZI_MIN : (int32_t)zi);
        s = clampf((int32_t)(((int64_t)(int32_t)sz * z16) >> 24), smax);
        t = clampf((int32_t)(((int64_t)(int32_t)tz * z16) >> 24), tmax);

        while (count > 0) {
            int n;
            if (count > 16) {
                n = 16;
                zi += dzidx << 4; sz += dszdx << 4; tz += dtzdx << 4;
                z16 = hw_div64((int64_t)1 << 48, (int32_t)zi < ZI_MIN ? ZI_MIN : (int32_t)zi);
                sn = clampf((int32_t)(((int64_t)(int32_t)sz * z16) >> 24), smax);
                tn = clampf((int32_t)(((int64_t)(int32_t)tz * z16) >> 24), tmax);
                sstep = (sn - s) >> 4;
                tstep = (tn - t) >> 4;
            } else {
                n = count;
                if (n > 1) {
                    int m = n - 1;
                    zi += dzidx * m; sz += dszdx * m; tz += dtzdx * m;
                    z16 = hw_div64((int64_t)1 << 48, (int32_t)zi < ZI_MIN ? ZI_MIN : (int32_t)zi);
                    sn = clampf((int32_t)(((int64_t)(int32_t)sz * z16) >> 24), smax);
                    tn = clampf((int32_t)(((int64_t)(int32_t)tz * z16) >> 24), tmax);
                    sstep = (int32_t)(((int64_t)(sn - s) * recip[m]) >> 16);
                    tstep = (int32_t)(((int64_t)(tn - t) * recip[m]) >> 16);
                } else {
                    sn = s; tn = t; sstep = tstep = 0;
                }
            }
            count -= n;
            do {
#ifdef HOST_CHECK
                if ((t >> 16) * sw + (s >> 16) < 0 || (t >> 16) * sw + (s >> 16) >= sw * sc->height) {
                    extern void host_bad_texel(const frec_t *fr, int s, int t, int x, int y);
                    host_bad_texel(fr, s, t, x, y);
                }
#endif
                *dst++ = dbl(src[(t >> 16) * sw + (s >> 16)]);
                s += sstep;
                t += tstep;
            } while (--n);
            s = sn;
            t = tn;
        }
    }
}
#endif

void draw_spans_flat(rcpu_t *rc, const frec_t *fr, int color)
{
    uint16_t w = dbl(color);
    uint32_t l = ((uint32_t)w << 16) | w;
    int i;
    for (i = 0; i < fr->nspan; i++) {
        const span_t *sp = &rc->spans[fr->span0 + i];
        int x = sp->x0, n = sp->x1 - sp->x0;
        volatile uint16_t *dst = r_fb + sp->y * SCREENW + x;
        rc->pixels += n;
        if (x & 1) { *dst++ = w; n--; }
        {
            volatile uint32_t *d32 = (volatile uint32_t *)dst;
            while (n >= 2) { *d32++ = l; n -= 2; }
            dst = (volatile uint16_t *)d32;
        }
        if (n) *dst = w;
    }
}

/* every other screen pixel: one byte of each doubled pixel word, the other
 * byte on alternate rows, so what is behind shows through in a fine mesh */
void draw_spans_flat_mesh(rcpu_t *rc, const frec_t *fr, int color)
{
    int i;
    for (i = 0; i < fr->nspan; i++) {
        const span_t *sp = &rc->spans[fr->span0 + i];
        int n = sp->x1 - sp->x0;
        volatile uint8_t *d = (volatile uint8_t *)(r_fb + sp->y * SCREENW + sp->x0) + (sp->y & 1);
        rc->pixels += n;
        while (n--) {
            *d = color;
            d += 2;
        }
    }
}

void draw_spans_sky(rcpu_t *rc, const frec_t *fr)
{
    int u0 = ((view.yaw * 5) >> 9) + (view.time >> 1);
    int v0 = (view.pitch * 5) >> 9;
    int i;
    for (i = 0; i < fr->nspan; i++) {
        const span_t *sp = &rc->spans[fr->span0 + i];
        int x = sp->x0, n = sp->x1 - sp->x0;
        volatile uint16_t *dst = r_fb + sp->y * SCREENW + x;
        const uint8_t *row = lvl.skytex + (((sp->y + v0) & 127) << 7);
        int u = x - u0;
        rc->pixels += n;
        while (n--) {
            *dst++ = dbl(row[u & 127]);
            u++;
        }
    }
}

/* water, slime, lava, teleporters. Like Quake, these always use the 64x64
 * base texture (no mip levels), which keeps the inner loop to constant
 * shifts and masks. mesh: write every other screen pixel (see-through). */
void draw_spans_turb(rcpu_t *rc, const frec_t *fr, int mesh)
{
    const lface_t *f = fr->f;
    const uint8_t *tex = liquid_texels(lvl.texinfo[f->texinfo].tex);
    const int32_t dzidx = fr->dzidx, dszdx = fr->dszdx, dtzdx = fr->dtzdx;
    const int32_t soff = fr->soff << 16, toff = fr->toff << 16;
    const int32_t *turb = turbtab + (view.time & 127);
    int i;

    for (i = 0; i < fr->nspan; i++) {
        const span_t *sp = &rc->spans[fr->span0 + i];
        int x = sp->x0, y = sp->y, count = sp->x1 - sp->x0;
        volatile uint16_t *dst = r_fb + y * SCREENW + x;
        volatile uint8_t *d8 = (volatile uint8_t *)dst + (y & 1);
        uint32_t zi = (uint32_t)fr->zi0 + x * dzidx + y * fr->dzidy;
        uint32_t sz = (uint32_t)fr->sz0 + x * dszdx + y * fr->dszdy;
        uint32_t tz = (uint32_t)fr->tz0 + x * dtzdx + y * fr->dtzdy;
        int32_t z16, s, t, sn, tn, sstep, tstep;

        rc->pixels += count;
        z16 = hw_div64((int64_t)1 << 48, (int32_t)zi < ZI_MIN ? ZI_MIN : (int32_t)zi);
        s = (int32_t)(((int64_t)(int32_t)sz * z16) >> 24) + soff;
        t = (int32_t)(((int64_t)(int32_t)tz * z16) >> 24) + toff;
        while (count > 0) {
            int n = count > 16 ? 16 : count;
            zi += dzidx * n; sz += dszdx * n; tz += dtzdx * n;
            z16 = hw_div64((int64_t)1 << 48, (int32_t)zi < ZI_MIN ? ZI_MIN : (int32_t)zi);
            sn = (int32_t)(((int64_t)(int32_t)sz * z16) >> 24) + soff;
            tn = (int32_t)(((int64_t)(int32_t)tz * z16) >> 24) + toff;
            sstep = (int32_t)(((int64_t)(sn - s) * recip[n]) >> 16);
            tstep = (int32_t)(((int64_t)(tn - t) * recip[n]) >> 16);
            count -= n;
            if (mesh) {
                do {
                    int ss = ((s + turb[(t >> 16) & 127]) >> 16) & 63;
                    int tt = ((t + turb[(s >> 16) & 127]) >> 16) & 63;
                    *d8 = tex[(tt << 6) + ss];
                    d8 += 2;
                    s += sstep;
                    t += tstep;
                } while (--n);
            } else {
                do {
                    int ss = ((s + turb[(t >> 16) & 127]) >> 16) & 63;
                    int tt = ((t + turb[(s >> 16) & 127]) >> 16) & 63;
                    *dst++ = dbl(tex[(tt << 6) + ss]);
                    s += sstep;
                    t += tstep;
                } while (--n);
            }
            s = sn;
            t = tn;
        }
    }
}

void fill_gaps(rcpu_t *rc)
{
    int r;
    for (r = rc->row0; r < rc->row1; r++) {
        gaprow_t *gr = &gaps[r];
        int k;
        for (k = 0; k < gr->n; k++) {
            volatile uint16_t *d = r_fb + r * SCREENW;
            int x;
            for (x = gr->g[k * 2]; x < gr->g[k * 2 + 1]; x++)
                d[x] = 0;
        }
    }
}
