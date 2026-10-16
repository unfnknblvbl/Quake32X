/* Quake 32X renderer: view setup, PVS, BSP walk, clipping, span coverage */
#include "r_local.h"

level_t lvl;
view_t  view;

#define MAX_MAP_LEAFS 4096
#define MAX_MAP_NODES 4096
#define MAX_MAP_FACES 8192

uint8_t vis_leafs[MAX_MAP_LEAFS / 8];
uint8_t vis_nodes[MAX_MAP_NODES / 8];
uint8_t vis_faces[MAX_MAP_FACES / 8];
uint8_t colormap[64 * 256];
const uint8_t bitmask8[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
gaprow_t gaps[SCREENH];

void level_init(const void *blob)
{
    const uint8_t *b = blob;
    const lheader_t *h = blob;
    lvl.base = b;
    lvl.hdr = h;
#define L(n) (b + h->lump[n].ofs)
    lvl.planes    = (const lplane_t *)L(LVL_PLANES);
    lvl.nodes     = (const lnode_t *)L(LVL_NODES);
    lvl.leafs     = (const lleaf_t *)L(LVL_LEAFS);
    lvl.marksurf  = (const uint16_t *)L(LVL_MARKSURF);
    lvl.faces     = (const lface_t *)L(LVL_FACES);
    lvl.fverts    = (const uint16_t *)L(LVL_FVERTS);
    lvl.verts     = (const lvert_t *)L(LVL_VERTS);
    lvl.texinfo   = (const ltexinfo_t *)L(LVL_TEXINFO);
    lvl.textures  = (const ltexture_t *)L(LVL_TEXTURES);
    lvl.light     = L(LVL_LIGHT);
    lvl.vis       = L(LVL_VIS);
    lvl.leaffrags = (const uint16_t *)L(LVL_LEAFFRAGS);
    lvl.clipnodes = (const lclipnode_t *)L(LVL_CLIPNODES);
    lvl.palette   = (const uint16_t *)L(LVL_PALETTE);
    lvl.colormap  = L(LVL_COLORMAP);
    lvl.conchars  = L(LVL_CONCHARS);
    lvl.skytex    = L(LVL_SKYTEX);
    lvl.facesph   = (const lsphere_t *)L(LVL_FACESPH);
    lvl.nodeparent = (const int16_t *)L(LVL_NODEPARENT);
#undef L
    lvl.numfaces  = h->lump[LVL_FACES].count;
    lvl.numleafs  = h->lump[LVL_LEAFS].count;
    lvl.numnodes  = h->lump[LVL_NODES].count;
    lvl.numplanes = h->lump[LVL_PLANES].count;
    lvl.visleafs  = h->visleafs;
}

void r_init(void)
{
    memcpy(colormap, lvl.colormap, sizeof(colormap));
}

/* ------------------------------------------------------------------ view */

void r_setup_view(fixed_t org[3], int yaw, int pitch)
{
    fixed_t sy = fsin(yaw), cy = fcos(yaw);
    fixed_t sp = fsin(pitch), cp = fcos(pitch);
    int i;

    view.org[0] = org[0]; view.org[1] = org[1]; view.org[2] = org[2];
    view.yaw = yaw; view.pitch = pitch;
    view.fwd[0] = fixmul(cy, cp);
    view.fwd[1] = fixmul(sy, cp);
    view.fwd[2] = -sp;
    view.right[0] = sy;
    view.right[1] = -cy;
    view.right[2] = 0;
    view.down[0] = -fixmul(cy, sp);
    view.down[1] = -fixmul(sy, sp);
    view.down[2] = -cp;
    for (i = 0; i < 3; i++) {
        /* x * 2^21 / 80 == x * (2^32 / 40) >> 12, without a 64-bit divide */
        view.r80s5[i] = (int32_t)(((int64_t)view.right[i] * 107374182) >> 12);
        view.d80s5[i] = (int32_t)(((int64_t)view.down[i] * 107374182) >> 12);
        view.r16[0][i] = view.right[i] >> 2;
        view.r16[1][i] = view.down[i] >> 2;
        view.r16[2][i] = view.fwd[i] >> 2;
    }
    /* camera . basis row in 15.17 (same scale as 13.3 vertex x 2.14 basis) */
    for (i = 0; i < 3; i++)
        view.camr[i] = (int32_t)(((int64_t)org[0] * view.r16[i][0] + (int64_t)org[1] * view.r16[i][1]
                                  + (int64_t)org[2] * view.r16[i][2]) >> 13);
}

/* which side of a node's plane is p on (1 = back) */
static inline int node_side(const lnode_t *node, const fixed_t *p)
{
    if (node->type < 3)
        return p[node->type] < node->dist;
    return (fixed_t)(((int64_t)node->normal[0] * p[0] + (int64_t)node->normal[1] * p[1]
                      + (int64_t)node->normal[2] * p[2]) >> 14) < node->dist;
}

int r_pointleaf(const fixed_t p[3])
{
    int n = lvl.hdr->headnode;
    while (n >= 0) {
        const lnode_t *node = &lvl.nodes[n];
        n = node->child[node_side(node, p)];
    }
    return -1 - n;
}

/* ------------------------------------------------------------------ PVS */

static void mark_leaf(int leaf)
{
    const lleaf_t *lf = &lvl.leafs[leaf];
    int i, n;
    BIT_SET(vis_leafs, leaf);
    for (i = 0; i < lf->nummark; i++) {
        int f = lvl.marksurf[lf->firstmark + i];
        if (f != 0xFFFF)
            BIT_SET(vis_faces, f);
    }
    n = lf->parent;
    while (n >= 0 && !BIT_TEST(vis_nodes, n)) {
        BIT_SET(vis_nodes, n);
        n = lvl.nodeparent[n];
    }
}

void r_markleaves(int leaf)
{
    const lleaf_t *lf = &lvl.leafs[leaf];
    int i;

    memset(vis_leafs, 0, sizeof(vis_leafs));
    memset(vis_nodes, 0, sizeof(vis_nodes));
    memset(vis_faces, 0, sizeof(vis_faces));

    if (leaf == 0 || lf->visofs < 0) {
        for (i = 1; i <= lvl.visleafs; i++)
            mark_leaf(i);
        return;
    }
    {
        const uint8_t *in = lvl.vis + lf->visofs;
        int row = (lvl.visleafs + 7) >> 3;
        int byte = 0;
        while (byte < row) {
            int c = *in++;
            if (c == 0) {
                byte += *in++;
                continue;
            }
            for (i = 0; i < 8; i++)
                if (c & bitmask8[i]) {
                    int l = byte * 8 + i + 1;
                    if (l <= lvl.visleafs)
                        mark_leaf(l);
                }
            byte++;
        }
    }
    mark_leaf(leaf);
}

/* ------------------------------------------------------------------ BSP walk */

/* returns -1 if the box is outside, else the remaining clip flags */
static int cull_box(const rcpu_t *rc, const int16_t *mins, const int16_t *maxs, int clipflags)
{
    int k, m;
    for (k = 0, m = 1; k < CP_NUM; k++, m <<= 1) {
        const fixed_t *n = rc->fr_n[k];
        int32_t dmax, dmin;
        if (!(clipflags & m))
            continue;
        if (n[0] >= 0) { dmax = n[0] * maxs[0]; dmin = n[0] * mins[0]; }
        else           { dmax = n[0] * mins[0]; dmin = n[0] * maxs[0]; }
        if (n[1] >= 0) { dmax += n[1] * maxs[1]; dmin += n[1] * mins[1]; }
        else           { dmax += n[1] * mins[1]; dmin += n[1] * maxs[1]; }
        if (n[2] >= 0) { dmax += n[2] * maxs[2]; dmin += n[2] * mins[2]; }
        else           { dmax += n[2] * mins[2]; dmin += n[2] * maxs[2]; }
        if (dmax < rc->fr_d[k])
            return -1;
        if (dmin >= rc->fr_d[k])
            clipflags &= ~m;
    }
    return clipflags;
}

static inline void add_cand(rcpu_t *rc, int fi, int clip)
{
    if (rc->ncand < MAXCAND)
        rc->cand[rc->ncand++] = (uint32_t)fi | ((uint32_t)clip << 16);
}

static void walk_leaf(rcpu_t *rc, int leaf, int clipflags)
{
    const lleaf_t *lf = &lvl.leafs[leaf];
    int i;
    if (!lf->numfrag || !BIT_TEST(vis_leafs, leaf))
        return;
    clipflags = cull_box(rc, lf->mins, lf->maxs, clipflags);
    if (clipflags < 0)
        return;
    for (i = 0; i < lf->numfrag; i++) {
        int fi = lvl.leaffrags[lf->firstfrag + i];
        const lface_t *f = &lvl.faces[fi];
        const lplane_t *pl = &lvl.planes[f->plane];
        fixed_t d = dot16(pl->n, view.org) - pl->dist;
        if ((d < 0) != ((f->flags & FF_PLANEBACK) != 0))
            continue;
        add_cand(rc, fi, clipflags);
    }
}

/* stage A: front-to-back BSP walk collecting candidate faces, with an
 * explicit stack (the CPU stack lives in 2 KB of on-chip RAM) */
#define WALK_STACK 72

static void walk_bsp(rcpu_t *rc)
{
    uint32_t stack[WALK_STACK];
    int sp = 0, num = lvl.hdr->headnode, clip = (1 << CP_NUM) - 1;

    rc->ncand = 0;
    for (;;) {
        while (num >= 0) {
            const lnode_t *node;
            int side;
            if (!BIT_TEST(vis_nodes, num))
                goto pop;
            node = &lvl.nodes[num];
            if (clip) {
                clip = cull_box(rc, node->mins, node->maxs, clip);
                if (clip < 0)
                    goto pop;
            }
            rc->nodes++;
            side = node_side(node, view.org);
            if (sp == WALK_STACK)
                goto pop;           /* cannot happen with E1M1 (depth 63) */
            stack[sp++] = (uint32_t)num | ((uint32_t)side << 16) | ((uint32_t)clip << 17);
            num = node->child[side];
        }
        walk_leaf(rc, -1 - num, clip);
    pop:
        if (sp == 0)
            return;
        {
            uint32_t e = stack[--sp];
            const lnode_t *node;
            int side, i, nf, ff;
            num = e & 0xFFFF;
            side = (e >> 16) & 1;
            clip = e >> 17;
            node = &lvl.nodes[num];
            nf = node->numfaces;
            ff = node->firstface;
            for (i = 0; i < nf; i++) {
                int fi = ff + i;
                if (!BIT_TEST(vis_faces, fi))
                    continue;
                if ((lvl.faces[fi].flags & FF_PLANEBACK) != side)
                    continue;
                add_cand(rc, fi, clip);
            }
            num = node->child[!side];
        }
    }
}

/* ------------------------------------------------------------------ stage D */

static void draw_range(rcpu_t *rc, int a, int b)
{
    int i;
    for (i = a; i < b; i++) {
        const frec_t *fr = &rc->frec[i];
        const lface_t *f = fr->f;
        rc->faces++;
        rc->spancount += fr->nspan;
        if (!view.textures)
            draw_spans_flat(rc, fr, view.lighting ? f->lit : f->unlit);
        else if (f->flags & FF_SKY)
            draw_spans_sky(rc, fr);
        else if (f->flags & FF_TURB)
            draw_spans_turb(rc, fr);
        else
            draw_spans_tex(rc, fr);
    }
}

/* shade and draw all face records of the batch */
void draw_batch(rcpu_t *rc)
{
    int i, start = 0, n = rc->nfrec;
    wait_fb(rc);
    if (++rc->batch == 0xFFFF) rc->batch = 1;
    for (i = 0; i < n; i++) {
        if (!face_setup(rc, &rc->frec[i])) {
            /* surface cache full of this batch's surfaces: draw those first */
            draw_range(rc, start, i);
            start = i;
            if (++rc->batch == 0xFFFF) rc->batch = 1;
            face_setup(rc, &rc->frec[i]);
        }
    }
    draw_range(rc, start, n);
    rc->nfrec = 0;
    rc->nspans = 0;
}

/* ------------------------------------------------------------------ frame */

static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static void set_plane(rcpu_t *rc, int k, fixed_t a, fixed_t b, fixed_t c, fixed_t e)
{
    int i;
    for (i = 0; i < 3; i++)
        rc->fr_n[k][i] = fixmul(a, view.right[i]) + fixmul(b, view.down[i]) + fixmul(c, view.fwd[i]);
    rc->fr_d[k] = dot16(rc->fr_n[k], view.org) + e;
}

void r_render(rcpu_t *rc, int row0, int row1)
{
    int r, k, ci;

    rc->row0 = row0;
    rc->row1 = row1;
    rc->vcframe += 0x10000;
    if (rc->vcframe == 0) {
        memset(rc->vcache, 0xFF, sizeof(rc->vcache));
        rc->vcframe = 0x10000;
    }
    rc->faces = rc->spancount = rc->pixels = rc->surfbuilds = rc->texels = rc->nodes = 0;
    rc->facecalls = rc->vxforms = rc->clips = rc->leaves = 0;
    if (rc->cachegen != view.cachegen) {
        sc_flush(rc);
        rc->cachegen = view.cachegen;
    }
    if (row0 >= row1)
        return;

    rc->ky0 = ((row0 - CENTERY) << 16) / PROJ;
    rc->ky1 = ((row1 - CENTERY) << 16) / PROJ;
    set_plane(rc, CP_NEAR, 0, 0, 0x10000, NEAR_Z);
    set_plane(rc, CP_FAR, 0, 0, -0x10000, -view.far);
    set_plane(rc, CP_LEFT, 0x10000, 0, 0x10000, 0);
    set_plane(rc, CP_RIGHT, -0x10000, 0, 0x10000, 0);
    set_plane(rc, CP_TOP, 0, 0x10000, -rc->ky0, 0);
    set_plane(rc, CP_BOTTOM, 0, -0x10000, rc->ky1, 0);
    for (k = 0; k < CP_NUM; k++) {
        const fixed_t *n = rc->fr_n[k];
        uint32_t sq = (uint32_t)((((int64_t)n[0] * n[0] + (int64_t)n[1] * n[1] + (int64_t)n[2] * n[2]) >> 16));
        rc->fr_len[k] = (fixed_t)(isqrt32(sq) << 8) + 1;    /* 16.16 */
    }

    for (r = row0; r < row1; r++) {
        gaps[r].n = 1;
        gaps[r].g[0] = 0;
        gaps[r].g[1] = SCREENW;
    }
    rc->rowsleft = row1 - row0;
    rc->nfrec = 0;
    rc->nspans = 0;
    rc->fb_ready = 0;

    walk_bsp(rc);
    ci = 0;
    while (ci < rc->ncand && rc->rowsleft > 0) {
        ci = geom_polys(rc, ci);
        geom_raster(rc);
        draw_batch(rc);
    }
    wait_fb(rc);
    fill_gaps(rc);
}
