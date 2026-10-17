/* Models (monsters, items, the view weapon) and particles. Each CPU draws
 * them into its own rows after the world.
 *
 * There is no full depth buffer. The master reserves depth only inside a
 * per-row interval around this frame's drawables (scene.zrow); world spans
 * write 1/z there as they are drawn, and models and particles test and
 * write it. Model triangles are flat shaded, one colour per triangle (the
 * average of the skin texels it covers), lit per triangle the way Quake
 * lights alias vertices, at a constant depth per triangle (its nearest
 * vertex). The view weapon has no depth test: its triangles are drawn back
 * to front. */
#include "g_local.h"
#ifdef HOST_CHECK
#include <stdio.h>
#include <stdlib.h>
#endif

uint16_t zbuf_mem[ZBUF_MAX];

#define MAXMV 256

typedef struct {
    int32_t  x, y;                   /* screen, 16.16 */
    uint16_t depth;                  /* 2^18 / z (z in units): depth buffer
                                        values are 2^20 / z, clamped */
    uint8_t  row;                    /* colormap row: 0 bright .. 63 dark */
    uint8_t  near;                   /* in front of the near plane: not projected */
} mvert_t;

static mvert_t mverts[2][MAXMV];

/* back-to-front order for sorted models: triangles bucketed by 1/z */
#define NBUCKET 64
#define MAXSORT 448
static uint16_t sortnext[2][MAXSORT];
static uint16_t sorthead[2][NBUCKET];

typedef struct {
    int32_t a[3][3];                 /* view = a * byte vertex + c (16.16) */
    fixed_t c[3];
    int32_t lv[3];                   /* light vector, model space, 2.14 */
    int32_t ambinv, shade;
} mxform_t;

typedef struct { int x0, x1, y0, y1; } mclip_t;

static inline uint16_t dbl(int c) { return (uint16_t)((c << 8) | c); }

static inline uint16_t clamp16(uint32_t d) { return d > 0xFFFF ? 0xFFFF : (uint16_t)d; }

/* ------------------------------------------------------------ depth */

/* zero the depth intervals of this CPU's rows (sky and gaps stay 0) */
void r_depth_clear(rcpu_t *rc)
{
    int y;
    if (!scene.zused)
        return;
    for (y = rc->row0; y < rc->row1; y++) {
        const zrow_t *z = &scene.zrow[y];
        if (z->x1 > z->x0)
            memset(scene.zbuf + z->ofs, 0, (z->x1 - z->x0) * 2);
    }
}

/* depth of a world face's spans, where they cross the depth intervals */
void r_depth_spans(rcpu_t *rc, frec_t *fr, int flags)
{
    int i, ready = view.textures;
    if (flags & FF_SKY)
        return;                     /* infinitely far: stays 0 */
    for (i = 0; i < fr->nspan; i++) {
        const span_t *sp = &rc->spans[fr->span0 + i];
        const zrow_t *z = &scene.zrow[sp->y];
        int a = sp->x0, b = sp->x1;
        uint16_t *d;
        int32_t zi, dz;
        if (z->x1 <= z->x0)
            continue;
        if (a < z->x0) a = z->x0;
        if (b > z->x1) b = z->x1;
        if (a >= b)
            continue;
        if (!ready) {
            face_zsetup(fr);        /* flat shading skips the gradients */
            ready = 1;
        }
        d = scene.zbuf + z->ofs + (a - z->x0);
        dz = fr->dzidx;
        zi = fr->zi0 + a * dz + sp->y * fr->dzidy;
        do {
            int32_t v = zi >> 12;
            *d++ = v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v;
            zi += dz;
        } while (++a < b);
    }
}

/* ------------------------------------------------------------ setup */

static const int16_t *anorms(void)
{
    return (const int16_t *)(lvl.base + lvl.hdr->lump[LVL_ANORMS].ofs);
}

static void setup_light(mxform_t *m, const drawent_t *d)
{
    int j = view.lighting ? 255 - d->light : 160;
    int amb, shade;
    uint16_t yaw = d->yaw;
    if (d->mode == DM_GUN && j < 24)
        j = 24;                     /* always give some light on the gun */
    amb = j;
    shade = j;
    if (amb > 128) amb = 128;
    if (amb + shade > 192) shade = 192 - amb;
    m->ambinv = 255 - amb;
    if (m->ambinv < 5) m->ambinv = 5;
    m->shade = shade;
    /* Quake's light vector (-1, 0, 0) turned into the model's frame */
    m->lv[0] = -fcos(yaw) >> 2;
    m->lv[1] = fsin(yaw) >> 2;
    m->lv[2] = 0;
}

static NOINLINE void setup_xform(mxform_t *m, const drawent_t *d)
{
    const lmodel_t *md = d->mdl;
    int r, k;
    if (d->mode == DM_GUN) {
        /* the weapon follows the view: model forward is view z, left is
           -x, up is -y; raised a little like Quake's viewsize fudge */
        for (r = 0; r < 3; r++)
            for (k = 0; k < 3; k++)
                m->a[r][k] = 0;
        m->a[0][1] = -md->scale[1];
        m->a[1][2] = -md->scale[2];
        m->a[2][0] = md->scale[0];
        m->c[0] = -md->origin[1];
        m->c[1] = -md->origin[2] - FX(1);
        m->c[2] = md->origin[0];
    } else {
        fixed_t cy = fcos(d->yaw), sy = fsin(d->yaw);
        fixed_t col[3][3], o[3];
        const fixed_t *rows[3] = { view.right, view.down, view.fwd };
        /* model axes in the world, scaled */
        col[0][0] = fixmul(cy, md->scale[0]); col[0][1] = fixmul(sy, md->scale[0]); col[0][2] = 0;
        col[1][0] = -fixmul(sy, md->scale[1]); col[1][1] = fixmul(cy, md->scale[1]); col[1][2] = 0;
        col[2][0] = 0; col[2][1] = 0; col[2][2] = md->scale[2];
        o[0] = d->org[0] + fixmul(cy, md->origin[0]) - fixmul(sy, md->origin[1]) - view.org[0];
        o[1] = d->org[1] + fixmul(sy, md->origin[0]) + fixmul(cy, md->origin[1]) - view.org[1];
        o[2] = d->org[2] + md->origin[2] - view.org[2];
        for (r = 0; r < 3; r++) {
            for (k = 0; k < 3; k++)
                m->a[r][k] = dot16(rows[r], col[k]);
            m->c[r] = dot16(rows[r], o);
        }
    }
    setup_light(m, d);
}

static inline void xform_vert(const mxform_t *m, const uint8_t *v, fixed_t out[3])
{
    int r;
    for (r = 0; r < 3; r++)
        out[r] = m->a[r][0] * v[0] + m->a[r][1] * v[1] + m->a[r][2] * v[2] + m->c[r];
}

static inline int32_t clampscreen(int64_t v)
{
    return v > ((int64_t)8192 << 16) ? (8192 << 16) : v < -((int64_t)8192 << 16) ? -(8192 << 16) : (int32_t)v;
}

/* project a view-space point: 1 if in front of the near plane */
static inline int project(const fixed_t v[3], mvert_t *o)
{
    int32_t r, k;
    if (v[2] < NEAR_Z) {
        o->near = 1;
        return 0;
    }
    r = hw_div(1 << 28, v[2] >> 8);              /* 2^20 / z */
    k = r * view.proj;
    o->x = clampscreen((int64_t)view.cx16 + (((int64_t)v[0] * k) >> 20));
    o->y = clampscreen((int64_t)view.cy16 + (((int64_t)v[1] * k) >> 20));
    o->depth = r >> 2 > 0xFFFF ? 0xFFFF : r >> 2;
    o->near = 0;
    return 1;
}

static NOINLINE void xform_verts(const mxform_t *m, const drawent_t *d, mvert_t *mv)
{
    const lmodel_t *md = d->mdl;
    const uint8_t *fv = (const uint8_t *)md + md->frames_ofs + d->frame * md->numverts * 4;
    const int16_t *an = anorms();
    const int32_t a00 = m->a[0][0], a01 = m->a[0][1], a02 = m->a[0][2], c0 = m->c[0];
    const int32_t a10 = m->a[1][0], a11 = m->a[1][1], a12 = m->a[1][2], c1 = m->c[1];
    const int32_t a20 = m->a[2][0], a21 = m->a[2][1], a22 = m->a[2][2], c2 = m->c[2];
    const int32_t l0 = m->lv[0], l1 = m->lv[1], amb = m->ambinv << 14, shade = m->shade;
    const int32_t proj = view.proj;
    int i, n = md->numverts;
    for (i = 0; i < n; i++, fv += 4, mv++) {
        int b0 = fv[0], b1 = fv[1], b2 = fv[2];
        const int16_t *nn = an + fv[3] * 3;
        fixed_t x, y, z = a20 * b0 + a21 * b1 + a22 * b2 + c2;
        int32_t lc, val, r;
        if (z >= NEAR_Z) {
            /* start 2^28 / (z >> 8) on the divider; it runs while the
               rest of the vertex is worked out */
#ifndef HOST
            SH2_DIVU_DVSR = z >> 8;
            SH2_DIVU_DVDNT = 1 << 28;
#endif
        }
        x = a00 * b0 + a01 * b1 + a02 * b2 + c0;
        y = a10 * b0 + a11 * b1 + a12 * b2 + c1;
        lc = (nn[0] * l0 + nn[1] * l1) >> 14;       /* the light vector is level */
        val = amb;
        if (lc < 0)
            val += shade * lc;
        if (val < 0) val = 0;
        mv->row = val >> 16;
        if (z < NEAR_Z) {
            mv->near = 1;
            continue;
        }
#ifndef HOST
        r = SH2_DIVU_DVDNT;
#else
        r = (1 << 28) / (z >> 8);
#endif
        {
            int32_t k = r * proj;
            mv->x = clampscreen((int64_t)view.cx16 + (((int64_t)x * k) >> 20));
            mv->y = clampscreen((int64_t)view.cy16 + (((int64_t)y * k) >> 20));
        }
        mv->depth = r >> 2 > 0xFFFF ? 0xFFFF : r >> 2;
        mv->near = 0;
    }
}

/* ------------------------------------------------------------ raster */

/* dx/dy for an edge that crosses at least one pixel centre row; nearly
 * flat edges get a bounded slope (they cover a row or two at most) */
static inline int32_t slope(int32_t dx, int32_t dy)
{
    int32_t ax = dx < 0 ? -dx : dx;
    if (dy <= 0)
        return 0;
    if ((int64_t)ax >= ((int64_t)dy << 13))
        return dx < 0 ? -(8192 << 16) : (8192 << 16);
    return hw_fixdiv(dx, dy);
}

/* fill a triangle (screen 16.16, any winding), pixel centres inside;
 * zt: depth-test against the frame's depth intervals */
static void tri_fill(const int32_t *pa, const int32_t *pb, const int32_t *pc,
                     uint16_t w, uint16_t depth, const mclip_t *cl, int zt)
{
    const int32_t *t;
    int32_t xl, xr, sl, sr, s02, s01, s12;
    int y, ys, ym, ye;
    /* sort by y */
    if (pb[1] < pa[1]) { t = pa; pa = pb; pb = t; }
    if (pc[1] < pb[1]) { t = pb; pb = pc; pc = t; }
    if (pb[1] < pa[1]) { t = pa; pa = pb; pb = t; }
    ys = (pa[1] + 0x7FFF) >> 16;
    ye = (pc[1] + 0x7FFF) >> 16;
    if (ys < cl->y0) ys = cl->y0;
    if (ye > cl->y1) ye = cl->y1;
    if (ys >= ye)
        return;
    ym = (pb[1] + 0x7FFF) >> 16;
    s02 = slope(pc[0] - pa[0], pc[1] - pa[1]);
    s01 = ys < ym ? slope(pb[0] - pa[0], pb[1] - pa[1]) : 0;
    s12 = ym < ye ? slope(pc[0] - pb[0], pc[1] - pb[1]) : 0;
    {
        fixed_t yc = (ys << 16) + 0x8000;
        xl = pa[0] + fixmul(yc - pa[1], s02);
        sl = s02;
        if (ys < ym) {
            xr = pa[0] + fixmul(yc - pa[1], s01);
            sr = s01;
        } else {
            xr = pb[0] + fixmul(yc - pb[1], s12);
            sr = s12;
        }
    }
    for (y = ys; y < ye; y++) {
        int x0, x1;
        if (y == ym) {
            xr = pb[0] + fixmul((y << 16) + 0x8000 - pb[1], s12);
            sr = s12;
        }
        if (xl < xr) {
            x0 = (xl + 0x7FFF) >> 16;
            x1 = (xr + 0x7FFF) >> 16;
        } else {
            x0 = (xr + 0x7FFF) >> 16;
            x1 = (xl + 0x7FFF) >> 16;
        }
        if (x0 < cl->x0) x0 = cl->x0;
        if (x1 > cl->x1) x1 = cl->x1;
        if (x0 < x1) {
            volatile uint16_t *fb = r_fb + y * SCREENW;
            if (zt) {
                const zrow_t *zr = &scene.zrow[y];
                uint16_t *zb = scene.zbuf + zr->ofs - zr->x0;
                do {
                    if (depth >= zb[x0]) {
                        zb[x0] = depth;
                        fb[x0] = w;
                    }
                } while (++x0 < x1);
            } else {
                do fb[x0] = w; while (++x0 < x1);
            }
        }
        xl += sl;
        xr += sr;
    }
}

/* a triangle with a vertex behind the near plane: clip it in view space */
static void tri_clipped(const mxform_t *m, const lmodel_t *md, const uint8_t *fv,
                        const uint8_t *tri, uint16_t w, const mclip_t *cl, int zt)
{
    fixed_t in[3][3], out[4][3];
    mvert_t pv[4];
    int i, n = 0;
    for (i = 0; i < 3; i++)
        xform_vert(m, fv + tri[i] * 4, in[i]);
    for (i = 0; i < 3; i++) {
        const fixed_t *a = in[i], *b = in[i == 2 ? 0 : i + 1];
        int ina = a[2] >= NEAR_Z, inb = b[2] >= NEAR_Z;
        if (ina) {
            out[n][0] = a[0]; out[n][1] = a[1]; out[n][2] = a[2];
            n++;
        }
        if (ina != inb) {
            fixed_t f = hw_fixdiv(NEAR_Z - a[2], b[2] - a[2]);
            out[n][0] = a[0] + fixmul(b[0] - a[0], f);
            out[n][1] = a[1] + fixmul(b[1] - a[1], f);
            out[n][2] = NEAR_Z;
            n++;
        }
    }
    if (n < 3)
        return;
    for (i = 0; i < n; i++)
        project(out[i], &pv[i]);
    {
        int64_t dn = (int64_t)(pv[0].y - pv[1].y) * (pv[0].x - pv[2].x)
                   - (int64_t)(pv[0].x - pv[1].x) * (pv[0].y - pv[2].y);
        if (dn >= 0)
            return;
    }
    (void)md;
    for (i = 1; i + 1 < n; i++) {
        int32_t a[2] = { pv[0].x, pv[0].y }, b[2] = { pv[i].x, pv[i].y }, c[2] = { pv[i + 1].x, pv[i + 1].y };
        tri_fill(a, b, c, w, 0xFFFF, cl, zt);
    }
}

static inline int backfacing(const mvert_t *a, const mvert_t *b, const mvert_t *c)
{
    /* Quake's test (d_polyse.c): drawn when the cross product is negative */
    int64_t dn = (int64_t)(a->y - b->y) * (a->x - c->x) - (int64_t)(a->x - b->x) * (a->y - c->y);
    return dn >= 0;
}

static NOINLINE void draw_model(rcpu_t *rc, const drawent_t *d)
{
    const lmodel_t *md = d->mdl;
    const uint8_t *tris = (const uint8_t *)md + md->tris_ofs;
    const uint8_t *cols = (const uint8_t *)md + md->colors_ofs + d->skin * md->numtris;
    const uint8_t *fv = (const uint8_t *)md + md->frames_ofs + d->frame * md->numverts * 4;
    mvert_t *mv = mverts[rc->cpu];
    mxform_t m;
    mclip_t cl;
    int i, n = md->numtris;

    cl.x0 = d->rect[0];
    cl.x1 = d->rect[2];
    cl.y0 = d->rect[1] > rc->row0 ? d->rect[1] : rc->row0;
    cl.y1 = d->rect[3] < rc->row1 ? d->rect[3] : rc->row1;
    if (cl.y0 >= cl.y1)
        return;
    setup_xform(&m, d);
    xform_verts(&m, d, mv);
#ifdef HOST_CHECK
    if (getenv("MDEBUG") && d->mode != DM_GUN) {
        int q;
        fprintf(stderr, "cpu %d model %d verts tris %d mode %d rect %d %d %d %d dist %d light %d\n", rc->cpu,
                md->numverts, n, d->mode, d->rect[0], d->rect[1], d->rect[2], d->rect[3], d->dist, d->light);
        fprintf(stderr, "  a %d %d %d / %d %d %d / %d %d %d c %d %d %d\n", m.a[0][0], m.a[0][1], m.a[0][2], m.a[1][0],
                m.a[1][1], m.a[1][2], m.a[2][0], m.a[2][1], m.a[2][2], m.c[0] >> 16, m.c[1] >> 16, m.c[2] >> 16);
        for (q = 0; q < 5; q++)
            fprintf(stderr, "  v%d x %.1f y %.1f depth %u row %d near %d\n", q, mv[q].x / 65536.0, mv[q].y / 65536.0,
                    mv[q].depth, mv[q].row, mv[q].near);
    }
#endif

    if (d->mode != DM_DEPTH) {
        /* back to front, no depth: bucket the front-facing triangles by
           their summed 1/z (near-clipped ones count as nearest) */
        uint16_t *next = sortnext[rc->cpu], *head = sorthead[rc->cpu];
        uint32_t kmin = 0xFFFFFFFF, kmax = 0, span;
        int b;
        if (n > MAXSORT)
            n = MAXSORT;
        for (i = 0; i < n; i++) {
            const uint8_t *t = tris + i * 4;
            const mvert_t *a = &mv[t[0]], *bb = &mv[t[1]], *c = &mv[t[2]];
            if (!(a->near | bb->near | c->near)) {
                uint32_t k = (uint32_t)a->depth + bb->depth + c->depth;
                if (k < kmin) kmin = k;
                if (k > kmax) kmax = k;
            }
        }
        span = kmax > kmin ? (kmax - kmin) / NBUCKET + 1 : 1;
        for (b = 0; b < NBUCKET; b++)
            head[b] = 0xFFFF;
        for (i = 0; i < n; i++) {
            const uint8_t *t = tris + i * 4;
            const mvert_t *a = &mv[t[0]], *bb = &mv[t[1]], *c = &mv[t[2]];
            if (a->near & bb->near & c->near)
                continue;                   /* all behind the eye */
            if (a->near | bb->near | c->near)
                b = NBUCKET - 1;
            else {
                if (backfacing(a, bb, c))
                    continue;
                b = hw_div((uint32_t)a->depth + bb->depth + c->depth - kmin, span);
                if (b >= NBUCKET) b = NBUCKET - 1;
            }
            next[i] = head[b];
            head[b] = i;
        }
        for (b = 0; b < NBUCKET; b++)
            for (i = head[b]; i != 0xFFFF; i = next[i]) {
                const uint8_t *t = tris + i * 4;
                const mvert_t *a = &mv[t[0]], *bb = &mv[t[1]], *c = &mv[t[2]];
                uint16_t w = dbl(colormap[(a->row << 8) + cols[i]]);
                if (a->near | bb->near | c->near)
                    tri_clipped(&m, md, fv, t, w, &cl, 0);
                else
                    tri_fill(&a->x, &bb->x, &c->x, w, 0, &cl, 0);
            }
        return;
    }

    for (i = 0; i < n; i++) {
        const uint8_t *t = tris + i * 4;
        const mvert_t *a = &mv[t[0]], *b = &mv[t[1]], *c = &mv[t[2]];
        uint16_t w;
        if (a->near | b->near | c->near) {
            if (a->near & b->near & c->near)
                continue;
            w = dbl(colormap[(a->row << 8) + cols[i]]);
            tri_clipped(&m, md, fv, t, w, &cl, 1);
            continue;
        }
        /* cheap rejects first: no pixel row, or outside the clip box */
        {
            int32_t y0 = a->y, y1 = a->y, x0 = a->x, x1 = a->x;
            y0 = y0 < b->y ? y0 : b->y;
            y1 = y1 > b->y ? y1 : b->y;
            y0 = y0 < c->y ? y0 : c->y;
            y1 = y1 > c->y ? y1 : c->y;
            if (((y0 + 0x7FFF) >> 16) >= ((y1 + 0x7FFF) >> 16))
                continue;
            if (((y1 + 0x7FFF) >> 16) <= cl.y0 || ((y0 + 0x7FFF) >> 16) >= cl.y1)
                continue;
            x0 = x0 < b->x ? x0 : b->x;
            x1 = x1 > b->x ? x1 : b->x;
            x0 = x0 < c->x ? x0 : c->x;
            x1 = x1 > c->x ? x1 : c->x;
            if (((x0 + 0x7FFF) >> 16) >= ((x1 + 0x7FFF) >> 16))
                continue;
            if (((x1 + 0x7FFF) >> 16) <= cl.x0 || ((x0 + 0x7FFF) >> 16) >= cl.x1)
                continue;
        }
        if (backfacing(a, b, c))
            continue;
        {
            uint32_t dd = a->depth;
            if (b->depth > dd) dd = b->depth;
            if (c->depth > dd) dd = c->depth;
            w = dbl(colormap[(a->row << 8) + cols[i]]);
            tri_fill(&a->x, &b->x, &c->x, w, clamp16(dd << 2), &cl, 1);
        }
    }
}

/* ------------------------------------------------------------ particles */

static NOINLINE void draw_particles(rcpu_t *rc)
{
    int i;
    uint16_t now = (uint16_t)level_time;
    for (i = 0; i < MAX_PARTICLES; i++) {
        const particle_t *p = &particles[i];
        fixed_t d[3], v[3];
        int32_t r, k, sx, sy, size, yy, xx;
        uint16_t depth, w;
        if (!(p->type & PT_VISIBLE) || (int16_t)(p->die - now) <= 0)
            continue;
        d[0] = p->org[0] - view.org[0];
        d[1] = p->org[1] - view.org[1];
        d[2] = p->org[2] - view.org[2];
        v[2] = dot16(d, view.fwd);
        if (v[2] < FX(8) || v[2] > view.far)
            continue;
        v[0] = dot16(d, view.right);
        v[1] = dot16(d, view.down);
        r = hw_div(1 << 28, v[2] >> 8);
        k = r * view.proj;
        sx = (view.cx16 + (int32_t)(((int64_t)v[0] * k) >> 20)) >> 16;
        sy = (view.cy16 + (int32_t)(((int64_t)v[1] * k) >> 20)) >> 16;
        size = v[2] < FX(48) ? 2 : 1;
        depth = clamp16(r);
        w = dbl(p->color);
        for (yy = sy; yy < sy + size; yy++) {
            const zrow_t *zr;
            uint16_t *zb;
            if (yy < rc->row0 || yy >= rc->row1)
                continue;
            zr = &scene.zrow[yy];
            zb = scene.zbuf + zr->ofs - zr->x0;
            for (xx = sx; xx < sx + size; xx++) {
                if (xx < 0 || xx >= view.vw)
                    continue;
                /* depth where there is some; elsewhere the leaf test */
                if (xx >= zr->x0 && xx < zr->x1) {
                    if (depth < zb[xx])
                        continue;
                    zb[xx] = depth;
                }
                r_fb[yy * SCREENW + xx] = w;
            }
        }
    }
}

/* ------------------------------------------------------------ entry */

/* depth-tested models, particles, then the sorted ones far to near (the
 * list is in that order), then the weapon */
void r_draw_models(rcpu_t *rc)
{
    int i, m;
    for (m = DM_DEPTH; m <= DM_GUN; m++) {
        for (i = 0; i < scene.ndraw; i++)
            if (scene.draw[i].mode == m)
                draw_model(rc, &scene.draw[i]);
        if (m == DM_DEPTH && scene.nparts)
            draw_particles(rc);
    }
}
