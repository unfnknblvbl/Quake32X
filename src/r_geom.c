/* Geometry stages of the face pipeline (compiled for size: this code is
 * instruction-cache bound on the SH-2).
 *
 *   geom_polys:  candidate faces -> clipped, projected screen polygons
 *   geom_raster: screen polygons -> spans against the per-row coverage gaps
 *
 * Both run over a whole batch of faces so that each stage's code stays in
 * the 2 KB cache instead of being refetched for every face.
 */
#include "r_local.h"

/* ------------------------------------------------------------ clipping */

static inline fixed_t clipdist(const rcpu_t *rc, int k, const vec3_t *v)
{
    switch (k) {
    case CP_NEAR:  return v->z - NEAR_Z;
    case CP_FAR:   return view.far - v->z;
    case CP_LEFT:  return v->x + v->z;
    case CP_RIGHT: return v->z - v->x;
    case CP_TOP:   return v->y - fixmul(rc->ky0, v->z);
    default:       return fixmul(rc->ky1, v->z) - v->y;
    }
}

/* clip against plane k; tags follow the vertices: the original vertex
 * index (its projection is already known) or -1 for a new one */
static int clip_poly(const rcpu_t *rc, int k, const vec3_t *in, const int8_t *tin, int n,
                     vec3_t *out, int8_t *tout)
{
    const vec3_t *p0 = &in[n - 1];
    fixed_t d0 = clipdist(rc, k, p0);
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const vec3_t *p1 = &in[i];
        fixed_t d1 = clipdist(rc, k, p1);
        if ((d0 >= 0) != (d1 >= 0)) {
            /* always interpolate from the inside point for watertight edges */
            const vec3_t *a = d0 >= 0 ? p0 : p1, *b = d0 >= 0 ? p1 : p0;
            fixed_t da = d0 >= 0 ? d0 : d1, db = d0 >= 0 ? d1 : d0;
            fixed_t t = hw_fixdiv(da, da - db);
            out[m].x = a->x + fixmul(b->x - a->x, t);
            out[m].y = a->y + fixmul(b->y - a->y, t);
            out[m].z = a->z + fixmul(b->z - a->z, t);
            tout[m] = -1;
            m++;
        }
        if (d1 >= 0) {
            tout[m] = tin[i];
            out[m++] = *p1;
        }
        p0 = p1;
        d0 = d1;
    }
    return m;
}

static void project(const rcpu_t *rc, const vec3_t *v, fixed_t *sx, fixed_t *sy)
{
    fixed_t z = v->z < NEAR_Z ? NEAR_Z : v->z;
    int32_t inv = hw_div64(view.proj40, z);                 /* proj * 2^40 / z */
    fixed_t x = view.cx16 + (fixed_t)(((int64_t)v->x * inv) >> 24);
    fixed_t y = view.cy16 + (fixed_t)(((int64_t)v->y * inv) >> 24);
    if (x < 0) x = 0;
    if (x > view.vw16) x = view.vw16;
    if (y < (rc->row0 << 16)) y = rc->row0 << 16;
    if (y > (rc->row1 << 16)) y = rc->row1 << 16;
    *sx = x;
    *sy = y;
}

/* ------------------------------------------------------------ vertices */

/* transform (16-bit multiplies), outcode and project one vertex into its cache slot */
static NOINLINE void xform_vert(rcpu_t *rc, vcache_t *e, int vi)
{
    const lvert_t *v = &lvl.verts[vi];
    int32_t vx = v->x, vy = v->y, vz = v->z;
    const int32_t *camr = v->pad ? view.mcamr[v->pad - 1] : view.camr;   /* movers: shifted */
    fixed_t x, y, z;
    int oc = 0;

    rc->vxforms++;
    x = ((int16_t)vx * view.r16[0][0] + (int16_t)vy * view.r16[0][1] + (int16_t)vz * view.r16[0][2]
         - camr[0]) >> 1;
    y = ((int16_t)vx * view.r16[1][0] + (int16_t)vy * view.r16[1][1] + (int16_t)vz * view.r16[1][2]
         - camr[1]) >> 1;
    z = ((int16_t)vx * view.r16[2][0] + (int16_t)vy * view.r16[2][1] + (int16_t)vz * view.r16[2][2]
         - camr[2]) >> 1;
    e->v.x = x;
    e->v.y = y;
    e->v.z = z;
    if (z < NEAR_Z) {
        oc = 1 << CP_NEAR;
    } else {
        int32_t inv;
        fixed_t sx, sy;
        hw_div64_start(view.proj40, z);     /* divide runs while we compute outcodes */
        if (z > view.far) oc = 1 << CP_FAR;
        if (y < fixmul(rc->ky0, z)) oc |= 1 << CP_TOP;
        if (y > fixmul(rc->ky1, z)) oc |= 1 << CP_BOTTOM;
        inv = hw_div64_result();
        sx = view.cx16 + (fixed_t)(((int64_t)x * inv) >> 24);
        sy = view.cy16 + (fixed_t)(((int64_t)y * inv) >> 24);
        if (sx < 0) sx = 0;
        if (sx > view.vw16) sx = view.vw16;
        if (sy < (rc->row0 << 16)) sy = rc->row0 << 16;
        if (sy > (rc->row1 << 16)) sy = rc->row1 << 16;
        e->sx = sx;
        e->sy = sy;
        goto sides;
    }
    if (y < fixmul(rc->ky0, z)) oc |= 1 << CP_TOP;
    if (y > fixmul(rc->ky1, z)) oc |= 1 << CP_BOTTOM;
sides:
    if (x < -z) oc |= 1 << CP_LEFT;
    if (x > z) oc |= 1 << CP_RIGHT;
    e->oc = oc;
    e->key = (uint32_t)vi | rc->vcframe;
}

static inline const vcache_t *get_vert(rcpu_t *rc, int vi)
{
    vcache_t *e = &rc->vcache[vi & (VCACHE_SIZE - 1)];
    if (e->key != ((uint32_t)vi | rc->vcframe))
        xform_vert(rc, e, vi);
    return e;
}

/* face bounding sphere against the frustum: -1 = outside, else the clip
 * planes the face can still cross */
static inline int sphere_clip(const rcpu_t *rc, const lfgeom_t *sp, int clip)
{
    int k, m;
    for (k = 0, m = 1; k < CP_NUM; k++, m <<= 1) {
        const fixed_t *n = rc->fr_n[k];
        int32_t d, rr;
        if (!(clip & m))
            continue;
        d = n[0] * sp->x + n[1] * sp->y + n[2] * sp->z - rc->fr_d[k];
        rr = rc->fr_len[k] * sp->r;
        if (d < -rr)
            return -1;
        if (d >= rr)
            clip &= ~m;
    }
    return clip;
}


/* one candidate -> polygon record in rc->poly; returns words used (0 = culled).
 * Vertex cache entries are copied out immediately: a later vertex of the
 * same face may map to the same cache slot. (On the 32X, geom_asm.S does
 * this; the C version is for the host and -DC_GEOM.) */
#if defined(HOST) || defined(C_GEOM)
int make_poly_c(rcpu_t *rc, int fi, int clip, int32_t *out)
{
    const lfgeom_t *g = &lvl.fgeom[fi];
    int i, n = g->numverts, ocand = 0xFF, ocor = 0;
    const uint16_t *fv;
    fixed_t zmin = 0x7FFFFFFF, *sx = out + 5, *sy;

    rc->facecalls++;
    if (n < 3 || n > MAXCLIPVERTS - CP_NUM)
        return 0;
    if (clip) {
        clip = sphere_clip(rc, g, clip);
        if (clip < 0)
            return 0;
    }
    fv = &lvl.fverts[g->firstvert];
    sy = sx + n;
    for (i = 0; i < n; i++) {
        const vcache_t *e = get_vert(rc, fv[i]);
        ocand &= e->oc;
        ocor |= e->oc;
        sx[i] = e->sx;
        sy[i] = e->sy;
        if (e->v.z < zmin) zmin = e->v.z;
        if (i == 0) {
            out[2] = e->v.x;
            out[3] = e->v.y;
            out[4] = e->v.z;
        }
    }
    if (ocand & clip)
        return 0;                       /* all vertices outside one plane */
    ocor &= clip;
    if (ocor)
        return make_poly_clip(rc, fi, ocor, out);
    if (zmin < NEAR_Z) zmin = NEAR_Z;
    if (out[4] < NEAR_Z) out[4] = NEAR_Z;
    out[0] = fi | (n << 16);
    out[1] = zmin;
    return 5 + 2 * n;
}
#endif

/* the rest of make_poly for a face that crosses the planes in ocor: out[5..]
 * holds its vertices' projections (x, then y), which the vertices that stay
 * keep; only the new ones are projected (geom_asm.S comes here too) */
int make_poly_clip(rcpu_t *rc, int fi, int ocor, int32_t *out)
{
    const lfgeom_t *g = &lvl.fgeom[fi];
    const uint16_t *fv = &lvl.fverts[g->firstvert];
    int i, k, m, n = g->numverts;
    vec3_t *in = rc->clipa, *o = rc->clipb, *t;
    int8_t tags[2][MAXCLIPVERTS], *tin = tags[0], *to = tags[1], *tt;
    fixed_t sx0[MAXCLIPVERTS - CP_NUM], sy0[MAXCLIPVERTS - CP_NUM], zmin, *sx = out + 5, *sy;

    rc->clips++;
    for (i = 0; i < n; i++) {
        in[i] = get_vert(rc, fv[i])->v;
        tin[i] = i;
        sx0[i] = sx[i];
        sy0[i] = sx[n + i];
    }
    for (k = 0, m = 1; k < CP_NUM; k++, m <<= 1) {
        if (!(ocor & m))
            continue;
        n = clip_poly(rc, k, in, tin, n, o, to);
        if (n < 3)
            return 0;
        t = in; in = o; o = t;
        tt = tin; tin = to; to = tt;
    }
    sy = sx + n;
    zmin = 0x7FFFFFFF;
    for (i = 0; i < n; i++) {
        if (tin[i] >= 0) {
            sx[i] = sx0[tin[i]];
            sy[i] = sy0[tin[i]];
        } else
            project(rc, &in[i], &sx[i], &sy[i]);
        if (in[i].z < zmin) zmin = in[i].z;
    }
    out[2] = in[0].x;
    out[3] = in[0].y;
    out[4] = in[0].z;
    if (zmin < NEAR_Z) zmin = NEAR_Z;
    if (out[4] < NEAR_Z) out[4] = NEAR_Z;
    out[0] = fi | (n << 16);
    out[1] = zmin;
    return 5 + 2 * n;
}

/* stage B: fill the polygon buffer from candidates ci.. ; returns next ci */
int geom_polys(rcpu_t *rc, const candlist_t *cl, int ci)
{
    int npoly = 0, w = 0, n = cl->n;
    while (ci < n && npoly < BATCH && w <= POLYWORDS - (5 + 2 * MAXCLIPVERTS)) {
        /* the walk culled against the whole window: this CPU's band
           (top and bottom planes) is tested here */
#if defined(HOST) || defined(C_GEOM)
        int used = make_poly_c(rc, cl->face[ci], cl->clip[ci] | (1 << CP_TOP) | (1 << CP_BOTTOM),
                               rc->poly + w);
#else
        int used = make_poly_asm(rc, cl->face[ci], cl->clip[ci] | (1 << CP_TOP) | (1 << CP_BOTTOM),
                                 rc->poly + w);     /* geom_asm.S */
#endif
        ci++;
        if (used) {
            w += used;
            npoly++;
        }
    }
    rc->npolyw = w;
    return ci;
}

/* ------------------------------------------------------------ coverage */

NOINLINE void span_overflow(rcpu_t *rc)
{
    /* span buffer full: draw what we have (including the current face's
       spans so far) and continue with a fresh copy of its record */
    frec_t keep, *cur = rc->cur;
    cur->nspan = rc->nspans - cur->span0;
    keep = *cur;
    if (cur->nspan)
        rc->nfrec++;
    draw_batch(rc);
    rc->frec[0] = keep;
    rc->frec[0].span0 = 0;
    rc->frec[0].nspan = 0;
    rc->nfrec = 0;
    rc->cur = &rc->frec[0];
}

static inline void emit_span(rcpu_t *rc, int y, int x0, int x1)
{
    span_t *s;
    if (rc->nspans == MAXSPANS)
        span_overflow(rc);
    s = &rc->spans[rc->nspans++];
#ifdef HOST
    s->y = y; s->x0 = x0; s->x1 = x1;
#else
    *(uint32_t *)s = ((uint32_t)y << 24) | ((uint32_t)x0 << 16) | ((uint32_t)x1 << 8);
#endif
}

/* out of gap slots: paint [x0, x1) of row y black and drop it */
NOINLINE void r_cover_black(rcpu_t *rc, int y, int x0, int x1)
{
    volatile uint16_t *d = r_fb + y * SCREENW;
    wait_fb(rc);
    for (; x0 < x1; x0++) d[x0] = 0;
}

/* clip [xs,xe) on row y against the uncovered gaps of that row */
static void cover_row(rcpu_t *rc, int y, int xs, int xe)
{
    gaprow_t *gr = &gaps[y];
    uint8_t *g = gr->g;
    int n = gr->n, i = 0;

    while (i < n) {
        int gs = g[i * 2], ge = g[i * 2 + 1];
        if (ge <= xs) { i++; continue; }
        if (gs >= xe) break;
        emit_span(rc, y, gs > xs ? gs : xs, ge < xe ? ge : xe);
        if (xs <= gs) {
            if (xe >= ge) {                 /* whole gap covered */
                int j;
                for (j = i; j < n - 1; j++) {
                    g[j * 2] = g[j * 2 + 2];
                    g[j * 2 + 1] = g[j * 2 + 3];
                }
                n--;
                continue;
            }
            g[i * 2] = xe;                  /* left part covered */
            break;
        }
        if (xe >= ge) {                     /* right part covered */
            g[i * 2 + 1] = xs;
            i++;
            continue;
        }
        /* middle covered: split the gap */
        if (n < MAXGAPS) {
            int j;
            for (j = n; j > i + 1; j--) {
                g[j * 2] = g[j * 2 - 2];
                g[j * 2 + 1] = g[j * 2 - 1];
            }
            g[i * 2 + 2] = xe;
            g[i * 2 + 3] = ge;
            n++;
        } else
            r_cover_black(rc, y, xe, ge);   /* out of gap slots */
        g[i * 2 + 1] = xs;
        break;
    }
    gr->n = n;
    if (n == 0)
        rc->rowsleft--;
}

/* see-through liquid: spans for the uncovered parts of [xs,xe), without
 * covering them (what is behind gets drawn too) */
static NOINLINE void mesh_row(rcpu_t *rc, int y, int xs, int xe)
{
    const gaprow_t *gr = &gaps[y];
    int i;
    for (i = 0; i < gr->n; i++) {
        int gs = gr->g[i * 2], ge = gr->g[i * 2 + 1];
        span_t *s;
        if (ge <= xs) continue;
        if (gs >= xe) break;
        if (rc->nwspans == MAXWSPANS) return;
        s = &rc->wspans[rc->nwspans++];
        s->y = y;
        s->x0 = gs > xs ? gs : xs;
        s->x1 = ge < xe ? ge : xe;
    }
}

/* does row y have an uncovered gap overlapping [xs,xe)? */
static inline int row_open(int y, int xs, int xe)
{
    const gaprow_t *gr = &gaps[y];
    int k;
    for (k = 0; k < gr->n; k++)
        if (gr->g[k * 2] < xe && gr->g[k * 2 + 1] > xs)
            return 1;
    return 0;
}

typedef struct { int i; fixed_t x, dxdy; int rend; } chain_t;

/* advance a polygon side to the next edge that reaches below row r */
static int chain_next(chain_t *c, int n, int dir, const fixed_t *sx, const fixed_t *sy, int r)
{
    int guard = n;
    while (guard--) {
        int i0 = c->i, i1 = i0 + dir, r1;
        fixed_t x0, x1, y0, y1, x, lo, hi;
        if (i1 < 0) i1 = n - 1;
        else if (i1 >= n) i1 = 0;
        y0 = sy[i0];
        y1 = sy[i1];
        c->i = i1;
        if (y1 < y0)
            return 0;
        r1 = (y1 + 0x7FFF) >> 16;
        if (r1 <= r || y1 == y0)
            continue;
        x0 = sx[i0];
        x1 = sx[i1];
        c->dxdy = hw_fixdiv(x1 - x0, y1 - y0);
        x = x0 + fixmul((r << 16) + 0x8000 - y0, c->dxdy);
        lo = x0 < x1 ? x0 : x1;
        hi = x0 < x1 ? x1 : x0;
        c->x = x < lo ? lo : x > hi ? hi : x;
        c->rend = r1;
        return 1;
    }
    return 0;
}

/* scan-convert a convex polygon by walking its two sides from the top vertex.
 * Instantiated twice with a constant `mesh`, so the usual copy has no extra
 * branch in its row loop. */
static inline __attribute__((always_inline)) void raster_poly(rcpu_t *rc, int n, const fixed_t *sx,
                                                             const fixed_t *sy, const int mesh)
{
    fixed_t ymin = sy[0], ymax = sy[0], xmin = sx[0], xmax = sx[0];
    int i, itop = 0, r, rtop, rbot, xs, xe;
    const int vw = view.vw;             /* local: stores below may alias view */
    chain_t a, b;

    for (i = 1; i < n; i++) {
        if (sy[i] < ymin) { ymin = sy[i]; itop = i; }
        if (sy[i] > ymax) ymax = sy[i];
        if (sx[i] < xmin) xmin = sx[i];
        if (sx[i] > xmax) xmax = sx[i];
    }
    rtop = (ymin + 0x7FFF) >> 16;
    rbot = (ymax + 0x7FFF) >> 16;
    if (rtop < rc->row0) rtop = rc->row0;
    if (rbot > rc->row1) rbot = rc->row1;
    /* occlusion pre-test: trim rows whose gaps miss the polygon's x range */
    xs = (xmin + 0x7FFF) >> 16;
    xe = (xmax + 0x7FFF) >> 16;
    while (rtop < rbot && !row_open(rtop, xs, xe))
        rtop++;
    while (rbot > rtop && !row_open(rbot - 1, xs, xe))
        rbot--;
    if (rtop >= rbot)
        return;

    r = rtop;
    a.i = b.i = itop;
    if (!chain_next(&a, n, -1, sx, sy, r) || !chain_next(&b, n, 1, sx, sy, r))
        return;
    for (;;) {
        int rend = a.rend < b.rend ? a.rend : b.rend;
        if (rend > rbot) rend = rbot;
        for (; r < rend; r++) {
            if (gaps[r].n) {
                fixed_t xl = a.x, xr = b.x;
                if (xl > xr) { fixed_t t = xl; xl = xr; xr = t; }
                xs = (xl + 0x7FFF) >> 16;
                xe = (xr + 0x7FFF) >> 16;
                if (xs < 0) xs = 0;
                if (xe > vw) xe = vw;
                if (xs < xe) {
                    if (mesh) mesh_row(rc, r, xs, xe);
                    else cover_row(rc, r, xs, xe);
                }
            }
            a.x += a.dxdy;
            b.x += b.dxdy;
        }
        if (r >= rbot)
            break;
        if (a.rend <= r && !chain_next(&a, n, -1, sx, sy, r))
            break;
        if (b.rend <= r && !chain_next(&b, n, 1, sx, sy, r))
            break;
    }
}

static NOINLINE void raster_mesh(rcpu_t *rc, int n, const fixed_t *sx, const fixed_t *sy)
{
    raster_poly(rc, n, sx, sy, 1);
}

/* stage C: rasterize the batch's polygons into spans and face records */
void geom_raster(rcpu_t *rc)
{
    int w = 0;
    while (w < rc->npolyw && rc->rowsleft > 0) {
        int32_t *p = rc->poly + w;
        int fi = p[0] & 0xFFFF, n = p[0] >> 16;
        frec_t *fr = &rc->frec[rc->nfrec];
        int mesh = 0;
        if (view.mesh && rc->wfrec && (lvl.fgeom[fi].flags & FF_LIQUID) && rc->nwfrec < MAXWFREC
            && rc->nwspans < MAXWSPANS - 64) {
            mesh = 1;                   /* deferred: see draw_liquids */
            fr = &rc->wfrec[rc->nwfrec];
        }
        fr->f = &lvl.faces[fi];
        fr->fi = fi;
        fr->span0 = mesh ? rc->nwspans : rc->nspans;
        fr->zmin = p[1];
        fr->refv.x = p[2];
        fr->refv.y = p[3];
        fr->refv.z = p[4];
        fr->refsx = p[5];
        fr->refsy = p[5 + n];
        if (mesh) {
            raster_mesh(rc, n, p + 5, p + 5 + n);
            fr->nspan = rc->nwspans - fr->span0;
            if (fr->nspan)
                rc->nwfrec++;
        } else {
            rc->cur = fr;
#if defined(HOST) || defined(C_RASTER)
            raster_poly(rc, n, p + 5, p + 5 + n, 0);
#else
            raster_solid(rc, n, p + 5, p + 5 + n);  /* raster_asm.S */
#endif
            fr = rc->cur;               /* may have moved on span overflow */
            fr->nspan = rc->nspans - fr->span0;
            if (fr->nspan)
                rc->nfrec++;
        }
        w += 5 + 2 * n;
    }
}
