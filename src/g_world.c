/* World collision for the game: point contents and swept traces through
 * the BSP (hull 0) and the clip hulls (1: player-sized, 2: large), plus
 * ray/box tests and Quake's light probe. Fixed point (16.16 units). */
#include "g_local.h"
#include "r_local.h"

#define DIST_EPSILON 2048            /* 1/32 unit, as in Quake */

/* signed distance of p from node/clipnode num's plane in the given hull */
static inline fixed_t hull_dist(int hull, int num, const fixed_t p[3], int *child0, int *child1)
{
    if (hull == 0) {
        const lnode_t *n = &lvl.nodes[num];
        *child0 = n->child[0];
        *child1 = n->child[1];
        if (n->type < 3)
            return p[n->type] - n->dist;
        return (fixed_t)(((int64_t)n->normal[0] * p[0] + (int64_t)n->normal[1] * p[1]
                          + (int64_t)n->normal[2] * p[2]) >> 14) - n->dist;
    } else {
        const lclipnode_t *c = &lvl.clipnodes[num];
        const lplane_t *pl = &lvl.planes[c->plane];
        *child0 = c->child[0];
        *child1 = c->child[1];
        return dot16(pl->n, p) - pl->dist;
    }
}

static inline int hull_head(int hull)
{
    return hull == 0 ? lvl.hdr->headnode : hull == 1 ? lvl.hdr->headnode1 : lvl.hdr->headnode2;
}

/* hull 0 children are nodes or leaves (-1 - leaf); clip hulls hold contents */
static inline int leaf_contents(int hull, int num)
{
    return hull == 0 ? lvl.leafs[-1 - num].contents : num;
}

static int contents_from(int hull, int num, const fixed_t p[3])
{
    while (num >= 0) {
        int c0, c1;
        fixed_t d = hull_dist(hull, num, p, &c0, &c1);
        num = d < 0 ? c1 : c0;
    }
    return leaf_contents(hull, num);
}

int g_contents(int hull, const fixed_t p[3])
{
    return contents_from(hull, hull_head(hull), p);
}

static void plane_normal(int hull, int num, fixed_t out[3])
{
    if (hull == 0) {
        const lnode_t *n = &lvl.nodes[num];
        out[0] = n->normal[0] << 2;
        out[1] = n->normal[1] << 2;
        out[2] = n->normal[2] << 2;
    } else {
        const lplane_t *pl = &lvl.planes[lvl.clipnodes[num].plane];
        out[0] = pl->n[0];
        out[1] = pl->n[1];
        out[2] = pl->n[2];
    }
}

/* SV_RecursiveHullCheck; returns 0 once the impact is found */
static int rhc(int hull, int num, fixed_t p1f, fixed_t p2f, const fixed_t p1[3], const fixed_t p2[3],
               trace_t *tr)
{
    int c0, c1, side, k;
    fixed_t t1, t2, frac, midf, mid[3];

    if (num < 0) {
        int c = leaf_contents(hull, num);
        if (c != CONTENTS_SOLID) {
            tr->allsolid = 0;
            if (c == CONTENTS_EMPTY) tr->inopen = 1;
            else tr->inwater = 1;
        } else
            tr->startsolid = 1;
        return 1;
    }
    t1 = hull_dist(hull, num, p1, &c0, &c1);
    t2 = hull_dist(hull, num, p2, &c0, &c1);
    if (t1 >= 0 && t2 >= 0)
        return rhc(hull, c0, p1f, p2f, p1, p2, tr);
    if (t1 < 0 && t2 < 0)
        return rhc(hull, c1, p1f, p2f, p1, p2, tr);

    /* the crossing point, put DIST_EPSILON on the near side */
    if (t1 < 0)
        frac = hw_fixdiv(t1 + DIST_EPSILON, t1 - t2);
    else
        frac = hw_fixdiv(t1 - DIST_EPSILON, t1 - t2);
    if (frac < 0) frac = 0;
    if (frac > 0x10000) frac = 0x10000;
    midf = p1f + fixmul(p2f - p1f, frac);
    for (k = 0; k < 3; k++)
        mid[k] = p1[k] + fixmul(p2[k] - p1[k], frac);
    side = t1 < 0;

    if (!rhc(hull, side ? c1 : c0, p1f, midf, p1, mid, tr))
        return 0;
    if (contents_from(hull, side ? c0 : c1, mid) != CONTENTS_SOLID)
        return rhc(hull, side ? c0 : c1, midf, p2f, mid, p2, tr);
    if (tr->allsolid)
        return 0;

    /* the other side of the node is solid: this is the impact point */
    plane_normal(hull, num, tr->normal);
    if (side) {
        tr->normal[0] = -tr->normal[0];
        tr->normal[1] = -tr->normal[1];
        tr->normal[2] = -tr->normal[2];
    }
    while (g_contents(hull, mid) == CONTENTS_SOLID) {
        frac -= 0x1999;             /* back off by 0.1 */
        if (frac < 0) {
            tr->frac = midf;
            tr->end[0] = mid[0]; tr->end[1] = mid[1]; tr->end[2] = mid[2];
            return 0;
        }
        midf = p1f + fixmul(p2f - p1f, frac);
        for (k = 0; k < 3; k++)
            mid[k] = p1[k] + fixmul(p2[k] - p1[k], frac);
    }
    tr->frac = midf;
    tr->end[0] = mid[0]; tr->end[1] = mid[1]; tr->end[2] = mid[2];
    return 0;
}

void g_trace(int hull, const fixed_t start[3], const fixed_t end[3], trace_t *tr)
{
    memset(tr, 0, sizeof(*tr));
    tr->frac = 0x10000;
    tr->allsolid = 1;
    tr->end[0] = end[0]; tr->end[1] = end[1]; tr->end[2] = end[2];
    rhc(hull, hull_head(hull), 0, 0x10000, start, end, tr);
    if (tr->allsolid)
        tr->startsolid = 1;
    if (tr->frac == 0x10000) {
        tr->end[0] = end[0]; tr->end[1] = end[1]; tr->end[2] = end[2];
    }
}

/* ray (dir normalized, 16.16) against an axis-aligned box: 1 and the hit
 * distance if it hits within maxlen */
int g_ray_box(const fixed_t s[3], const fixed_t d[3], fixed_t maxlen,
                    const fixed_t mins[3], const fixed_t maxs[3], fixed_t *tout)
{
    fixed_t tmin = 0, tmax = maxlen;
    int k;
    for (k = 0; k < 3; k++) {
        if (d[k] > -64 && d[k] < 64) {
            if (s[k] < mins[k] || s[k] > maxs[k])
                return 0;
        } else {
            fixed_t a = hw_fixdiv(mins[k] - s[k], d[k]);
            fixed_t b = hw_fixdiv(maxs[k] - s[k], d[k]);
            if (a > b) { fixed_t t = a; a = b; b = t; }
            if (a > tmin) tmin = a;
            if (b < tmax) tmax = b;
            if (tmin > tmax)
                return 0;
        }
    }
    *tout = tmin;
    return 1;
}

/* ------------------------------------------------------------ light probe */

/* R_RecursiveLightPoint: the lightmap value under p (inverted, 0 = bright) */
static int light_rec(int num, const fixed_t start[3], const fixed_t end[3])
{
    const lnode_t *n;
    fixed_t front, back, frac, mid[3];
    int side, r, i, k;

    if (num < 0)
        return -1;
    n = &lvl.nodes[num];
    if (n->type < 3) {
        front = start[n->type] - n->dist;
        back = end[n->type] - n->dist;
    } else {
        front = (fixed_t)(((int64_t)n->normal[0] * start[0] + (int64_t)n->normal[1] * start[1]
                           + (int64_t)n->normal[2] * start[2]) >> 14) - n->dist;
        back = (fixed_t)(((int64_t)n->normal[0] * end[0] + (int64_t)n->normal[1] * end[1]
                          + (int64_t)n->normal[2] * end[2]) >> 14) - n->dist;
    }
    side = front < 0;
    if ((back < 0) == side)
        return light_rec(n->child[side], start, end);
    frac = hw_fixdiv(front, front - back);
    for (k = 0; k < 3; k++)
        mid[k] = start[k] + fixmul(end[k] - start[k], frac);
    r = light_rec(n->child[side], start, mid);
    if (r >= 0)
        return r;
    for (i = 0; i < n->numfaces; i++) {
        const lface_t *f = &lvl.faces[n->firstface + i];
        const ltexinfo_t *ti = &lvl.texinfo[f->texinfo];
        fixed_t s, t;
        int ds, dt;
        if (f->flags & (FF_SKY | FF_TURB))
            continue;
        s = dot16(ti->s, mid) + ti->s[3];
        t = dot16(ti->t, mid) + ti->t[3];
        ds = (s >> 16) - f->tmins[0];
        dt = (t >> 16) - f->tmins[1];
        if (ds < 0 || dt < 0 || ds > f->ext[0] || dt > f->ext[1])
            continue;
        if (f->lightofs < 0)
            return 128;
        return lvl.light[f->lightofs + (dt >> 4) * ((f->ext[0] >> 4) + 1) + (ds >> 4)];
    }
    return light_rec(n->child[!side], mid, end);
}

int g_light_point(const fixed_t p[3])
{
    fixed_t end[3];
    int r;
    end[0] = p[0];
    end[1] = p[1];
    end[2] = p[2] - FX(2048);
    r = light_rec(lvl.hdr->headnode, p, end);
    return r < 0 ? 128 : r;
}

/* after an entity moves: its leaf now, its light when next drawn */
void g_relink(ent_t *e)
{
    e->leaf = r_pointleaf(e->org);
    e->lightdirty = 1;
}

int g_leaf_visible(const fixed_t p[3])
{
    int leaf = r_pointleaf(p);
    return leaf > 0 && BIT_TEST(vis_leafs, leaf);
}
