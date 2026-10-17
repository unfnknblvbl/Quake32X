/* Surface cache: lit, tiled copies of a face's texture (one per CPU) */
#include "r_local.h"

#define HDRSIZE ((int)sizeof(surfcache_t))
#define MINFREE 64


static inline int hash_key(int face, int mip)
{
    return ((face << 2) ^ (face >> 8) ^ mip) & (HASHSIZE - 1);
}

void sc_init(rcpu_t *rc, uint8_t *mem, int size, surfcache_t **hash)
{
    rc->sc_base = mem;
    rc->sc_end = mem + (size & ~7);
    rc->sc_hash = hash;
    sc_flush(rc);
}

/* split a CPU's memory between the see-through liquid buffers (when used)
 * and its surface cache */
void r_arena(rcpu_t *rc, uint8_t *mem, int size, surfcache_t **hash, int mesh)
{
    rc->wfrec = NULL;
    rc->wspans = NULL;
    if (mesh) {
        int need = (MAXWFREC * sizeof(frec_t) + MAXWSPANS * sizeof(span_t) + 15) & ~15;
        rc->wfrec = (frec_t *)mem;
        rc->wspans = (span_t *)(mem + MAXWFREC * sizeof(frec_t));
        mem += need;
        size -= need;
    }
    sc_init(rc, mem, size, hash);
}

void sc_flush(rcpu_t *rc)
{
    surfcache_t *s = (surfcache_t *)rc->sc_base;
    int i;
    for (i = 0; i < HASHSIZE; i++)
        rc->sc_hash[i] = NULL;
    s->owner = NULL;
    s->batch = 0xFFFF;
    s->size = rc->sc_end - rc->sc_base;
    rc->sc_rover = s;
}

#define FREEBATCH 0xFFFF
#define NEXT(t) ((surfcache_t *)((uint8_t *)(t) + (t)->size))

/* allocate `size` bytes (header included) at the rover, evicting old entries.
 * Returns NULL if that would evict a surface used by the current batch. */
static surfcache_t *sc_alloc(rcpu_t *rc, uint32_t size)
{
    surfcache_t *s, *t, *next;
    uint32_t got;
    int wrap;

    size = (size + sizeof(void *) - 1) & ~(uint32_t)(sizeof(void *) - 1);
    s = rc->sc_rover;
    wrap = (uint8_t *)s + size > rc->sc_end;

    /* check first: nothing we would overwrite may still be needed */
    t = s;
    if (wrap) {
        for (; (uint8_t *)t < rc->sc_end; t = NEXT(t))
            if (t->batch == rc->batch) return NULL;
        t = (surfcache_t *)rc->sc_base;
    }
    for (got = 0; got < size; t = NEXT(t)) {
        if (t->batch == rc->batch) return NULL;
        got += t->size;
    }

    if (wrap) {
        /* evict everything up to the end; the tail becomes one free block */
        for (t = s; (uint8_t *)t < rc->sc_end; t = NEXT(t))
            if (t->owner) *t->owner = NULL;
        s->owner = NULL;
        s->batch = FREEBATCH;
        s->size = rc->sc_end - (uint8_t *)s;
        s = (surfcache_t *)rc->sc_base;
    }
    if (s->owner) *s->owner = NULL;
    got = s->size;
    while (got < size) {
        next = (surfcache_t *)((uint8_t *)s + got);
        if (next->owner) *next->owner = NULL;
        got += next->size;
    }
    if (got - size >= MINFREE) {
        next = (surfcache_t *)((uint8_t *)s + size);
        next->owner = NULL;
        next->batch = FREEBATCH;
        next->size = got - size;
    } else {
        size = got;
    }
    s->size = size;
    s->owner = NULL;
    next = (surfcache_t *)((uint8_t *)s + size);
    rc->sc_rover = (uint8_t *)next >= rc->sc_end ? (surfcache_t *)rc->sc_base : next;
    return s;
}

static inline int imod(int a, int m)
{
    a = a - hw_div(a, m) * m;
    return a < 0 ? a + m : a;
}

/* lit blocks for one mip level; `shift` is a compile-time constant in each
 * instantiation (the SH-2 has no barrel shifter) */
static inline __attribute__((always_inline)) void build_lit(
    uint8_t *out, int sw, int sh, const uint8_t *tex, int tw, int th, int s0, int t0,
    const uint8_t *lm, int smax, const int shift)
{
    int bu, bv, u, v, brow = t0;
    for (bv = 0; bv < (sh >> shift); bv++) {
        const uint8_t *l0 = lm + bv * smax, *l1 = l0 + smax;
        int bcol = s0;
        uint8_t *rowout = out + ((bv * sw) << shift);
        for (bu = 0; bu < (sw >> shift); bu++) {
            int ll = l0[bu] << 6, lr = l0[bu + 1] << 6;
            int lls = ((l1[bu] << 6) - ll) >> shift;
            int lrs = ((l1[bu + 1] << 6) - lr) >> shift;
            uint8_t *dst = rowout + (bu << shift);
            int trow = brow;
            for (v = 0; v < (1 << shift); v++) {
                const uint8_t *src = tex + trow * tw + bcol;
                int light = ll, step = (lr - ll) >> shift;
#ifndef HOST
                if (shift >= 2) {
                    /* four texels per long write: SDRAM writes are 2 clocks
                       per word whatever their width */
                    uint32_t *d32 = (uint32_t *)dst;
                    for (u = 0; u < (1 << shift); u += 4) {
                        uint32_t a, b, c, d;
                        a = colormap[(light & 0xFF00) + src[u]];     light += step;
                        b = colormap[(light & 0xFF00) + src[u + 1]]; light += step;
                        c = colormap[(light & 0xFF00) + src[u + 2]]; light += step;
                        d = colormap[(light & 0xFF00) + src[u + 3]]; light += step;
                        *d32++ = (a << 24) | (b << 16) | (c << 8) | d;
                    }
                } else
#endif
                for (u = 0; u < (1 << shift); u++) {
                    dst[u] = colormap[(light & 0xFF00) + src[u]];
                    light += step;
                }
                dst += sw;
                ll += lls;
                lr += lrs;
                if (++trow == th) trow = 0;
            }
            bcol += 1 << shift;
            if (bcol >= tw) bcol -= tw;
        }
        brow += 1 << shift;
        if (brow >= th) brow -= th;
    }
}

static void build_surface(rcpu_t *rc, surfcache_t *sc, const lface_t *f, int mip)
{
    const ltexinfo_t *ti = &lvl.texinfo[f->texinfo];
    const ltexture_t *tx = &lvl.textures[ti->tex];
    const uint8_t *tex = lvl.base + tx->mip[mip];
    int tw = shr_mip(tx->w, mip), th = shr_mip(tx->h, mip);
    int sw = sc->width, sh = sc->height;
    int s0 = imod(shr_mip(f->tmins[0], mip), tw);
    int t0 = imod(shr_mip(f->tmins[1], mip), th);
    int smax = (f->ext[0] >> 4) + 1;
    int u, v;
    uint8_t *out = sc->data;

    rc->surfbuilds++;
    rc->texels += sw * sh;

    if (!view.lighting || f->lightofs < 0) {
        /* plain texture tiling (lighting off, or no lightmap) */
        int row = t0;
        const uint8_t *cmap = (f->lightofs < 0 && view.lighting) ? colormap + 63 * 256 : NULL;
        for (v = 0; v < sh; v++) {
            const uint8_t *src = tex + row * tw;
            int col = s0;
            if (cmap) {
                for (u = 0; u < sw; u++) {
                    out[u] = cmap[src[col]];
                    if (++col == tw) col = 0;
                }
            } else {
                u = 0;
                while (u < sw) {
                    int n = tw - col;
                    if (n > sw - u) n = sw - u;
                    memcpy(out + u, src + col, n);
                    u += n;
                    col = 0;
                }
            }
            out += sw;
            if (++row == th) row = 0;
        }
        return;
    }

    {
        const uint8_t *lm = lvl.light + f->lightofs;
        /* block size is 16 >> mip texels: shift = 4 - mip */
        switch (mip) {
        case 0: build_lit(out, sw, sh, tex, tw, th, s0, t0, lm, smax, 4); break;
        case 1: build_lit(out, sw, sh, tex, tw, th, s0, t0, lm, smax, 3); break;
        case 2: build_lit(out, sw, sh, tex, tw, th, s0, t0, lm, smax, 2); break;
        default: build_lit(out, sw, sh, tex, tw, th, s0, t0, lm, smax, 1); break;
        }
    }
}

surfcache_t *sc_get(rcpu_t *rc, int faceidx, int mip)
{
    int h = hash_key(faceidx, mip);
    surfcache_t *s = rc->sc_hash[h];
    const lface_t *f;
    int w, ht;

    if (s && s->key == faceidx && s->mip == mip) {
        s->batch = rc->batch;
        return s;
    }

    f = &lvl.faces[faceidx];
    w = shr_mip(f->ext[0], mip);
    ht = shr_mip(f->ext[1], mip);
    s = sc_alloc(rc, HDRSIZE + w * ht);
    if (!s)
        return NULL;
    if (rc->sc_hash[h])
        rc->sc_hash[h]->owner = NULL;   /* the colliding entry leaves the table */
    s->key = faceidx;
    s->mip = mip;
    s->batch = rc->batch;
    s->width = w;
    s->height = ht;
    s->owner = &rc->sc_hash[h];
    rc->sc_hash[h] = s;
    build_surface(rc, s, f, mip);
    return s;
}
