/* Quake 32X renderer: view setup, PVS, BSP walk, clipping, span coverage */
#include "g_local.h"

level_t lvl;
view_t  view, view_next;

visset_t vis;
candlist_t cands[2];                /* the BSP walk's output (r_walk) */
int cand_cur;
static int cand_ready;              /* cands[cand_cur ^ 1] holds the next frame's */
uint8_t colormap[64 * 256];
const uint16_t *r_palette;          /* the palette drawn with: the level's, or flat mode's own */
const uint8_t *r_pmap;              /* Quake colours into flat mode's own palette (particles), or NULL */
static int flatpal_on;
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
    lvl.fgeom     = (const lfgeom_t *)L(LVL_FACESPH);
    lvl.nodeparent = (const int16_t *)L(LVL_NODEPARENT);
    lvl.backtile  = L(LVL_BACKTILE);
    lvl.flatof    = (const uint16_t *)L(LVL_FLATOF);
    lvl.flatnode  = (const lflatnode_t *)L(LVL_FLATNODE);
    lvl.flatlist  = (const uint16_t *)L(LVL_FLATLIST);
#undef L
    lvl.numfaces  = h->lump[LVL_FACES].count;
    lvl.numleafs  = h->lump[LVL_LEAFS].count;
    lvl.numnodes  = h->lump[LVL_NODES].count;
    lvl.numplanes = h->lump[LVL_PLANES].count;
    lvl.visleafs  = h->visleafs;
    /* the editor's extensions */
    lvl.flatref = NULL; lvl.flatmark = NULL; lvl.grid = NULL; lvl.leafarea = NULL;
    lvl.doors = NULL; lvl.ndoors = 0; lvl.leaffar = NULL; lvl.mvmodels = NULL; lvl.mdllod = NULL;
    lvl.flatpal = NULL;
    lvl.flags = h->flags;
    if (h->extofs > 0) {
        const lextdir_t *x = (const lextdir_t *)(b + h->extofs);
        int n = x->numlumps < LX_NUMLUMPS ? x->numlumps : LX_NUMLUMPS;
#define X(i) (i < n && x->lump[i].ofs ? b + x->lump[i].ofs : NULL)
        lvl.flatref  = (const lflatref_t *)X(LX_FLATREF);
        lvl.flatmark = (const uint16_t *)X(LX_FLATMARK);
        lvl.grid     = (const lgrid_t *)X(LX_LIGHTGRID);
        lvl.leafarea = X(LX_LEAFAREA);
        lvl.doors    = (const ldoor_t *)X(LX_DOORS);
        lvl.ndoors   = lvl.doors ? x->lump[LX_DOORS].count : 0;
        lvl.leaffar  = (const uint16_t *)X(LX_LEAFFAR);
        lvl.mvmodels = (const uint32_t *)X(LX_MVMODELS);
        lvl.mdllod   = (const lmdllod_t *)X(LX_MDLLOD);
        lvl.flatpal  = (const lflatpal_t *)X(LX_FLATPAL);
#undef X
        if (!lvl.flatmark) lvl.flatref = NULL;
        if (!lvl.leafarea || !lvl.doors) { lvl.leafarea = NULL; lvl.doors = NULL; lvl.ndoors = 0; }
    }
}

void r_init(void)
{
    memcpy(colormap, lvl.colormap, sizeof(colormap));
    r_palette = lvl.palette;
    r_pmap = NULL;
    flatpal_on = 0;
}

/* flat mode's own palette in or out with the textures setting: the
   colormap the models and kept textures are drawn through, and the
   palette (update_palette writes it); cached surfaces are in the old one */
static void r_flatpal(void)
{
    int on = !view.textures && lvl.flatpal;
    if (on == flatpal_on)
        return;
    flatpal_on = on;
    memcpy(colormap, on ? lvl.flatpal->cmap : lvl.colormap, sizeof(colormap));
    r_palette = on ? lvl.flatpal->cram : lvl.palette;
    r_pmap = on ? lvl.flatpal->remap : NULL;
    view.cachegen++;
    view_next.cachegen = view.cachegen;
}

/* ------------------------------------------------------------------ view */

void r_setup_view(fixed_t org[3], int yaw, int pitch)
{
    fixed_t sy = fsin(yaw), cy = fcos(yaw);
    fixed_t sp = fsin(pitch), cp = fcos(pitch);
    int i;

    int32_t k32;

    if (view_next.vw <= 0) { view_next.vw = SCREENW; view_next.vh = SCREENH; }
    view_next.proj = view_next.vw >> 1;
    view_next.proj40 = (int64_t)view_next.proj << 40;
    view_next.vw16 = view_next.vw << 16;
    view_next.cx16 = view_next.vw << 15;
    view_next.cy16 = view_next.vh << 15;
    view_next.k24 = hw_div(1 << 24, view_next.proj);
    view_next.mipscale = hw_div(PROJ << 16, view_next.proj);
    k32 = hw_div64((int64_t)1 << 32, view_next.proj);       /* 2^32 / proj */

    view_next.org[0] = org[0]; view_next.org[1] = org[1]; view_next.org[2] = org[2];
    view_next.yaw = yaw; view_next.pitch = pitch;
    view_next.fwd[0] = fixmul(cy, cp);
    view_next.fwd[1] = fixmul(sy, cp);
    view_next.fwd[2] = -sp;
    view_next.right[0] = sy;
    view_next.right[1] = -cy;
    view_next.right[2] = 0;
    view_next.down[0] = -fixmul(cy, sp);
    view_next.down[1] = -fixmul(sy, sp);
    view_next.down[2] = -cp;
    for (i = 0; i < 3; i++) {
        /* x * 2^20 / proj == x * (2^32 / proj) >> 12, without a 64-bit divide
           (2^20, not 2^21: the smallest window, proj 40, would overflow) */
        view_next.r80s5[i] = (int32_t)(((int64_t)view_next.right[i] * k32) >> 12);
        view_next.d80s5[i] = (int32_t)(((int64_t)view_next.down[i] * k32) >> 12);
        view_next.r16[0][i] = view_next.right[i] >> 2;
        view_next.r16[1][i] = view_next.down[i] >> 2;
        view_next.r16[2][i] = view_next.fwd[i] >> 2;
    }
    /* camera . basis row in 15.17 (same scale as 13.3 vertex x 2.14 basis) */
    for (i = 0; i < 3; i++)
        view_next.camr[i] = (int32_t)(((int64_t)org[0] * view_next.r16[i][0] + (int64_t)org[1] * view_next.r16[i][1]
                                  + (int64_t)org[2] * view_next.r16[i][2]) >> 13);
}

/* camr for each mover's vertices: the camera seen from the mover's frame */
void r_mover_cam(int n)
{
    int m, i;
    for (m = 0; m < n; m++) {
        fixed_t o[3];
        o[0] = view_next.org[0] - view_next.mres[m][0];
        o[1] = view_next.org[1] - view_next.mres[m][1];
        o[2] = view_next.org[2] - view_next.mres[m][2];
        for (i = 0; i < 3; i++)
            view_next.mcamr[m][i] = (int32_t)(((int64_t)o[0] * view_next.r16[i][0] + (int64_t)o[1] * view_next.r16[i][1]
                                          + (int64_t)o[2] * view_next.r16[i][2]) >> 13);
    }
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

/* doors that hide what is behind them: the areas the view can reach
 * through open doors (bit per area) */
static uint8_t area_vis[32];
static uint32_t door_state;                 /* the doors open when last marked */

static int area_visible(int a)
{
    if (!a)
        return 1;
    if (a < 128)
        return area_vis[a >> 3] & bitmask8[a & 7];
    {   /* a doorway: seen from either side */
        const ldoor_t *d = &lvl.doors[a - 128];
        int k;
        for (k = 0; k < d->nareas && k < 4; k++)
            if (area_vis[d->area[k] >> 3] & bitmask8[d->area[k] & 7])
                return 1;
        return 0;
    }
}

/* which doors are open now (a bit each, the first 32) */
uint32_t r_door_state(void)
{
    uint32_t s = 0;
    int i;
    for (i = 0; i < lvl.ndoors && i < 32; i++) {
        const fixed_t *o = mover_off[lvl.doors[i].mover];
        if (o[0] | o[1] | o[2])
            s |= 1u << i;
    }
    return s;
}

static void flood_areas(int leaf)
{
    int a = lvl.leafarea[leaf], changed = 1, i, k;
    memset(area_vis, 0, sizeof(area_vis));
    if (a >= 128) {
        const ldoor_t *d = &lvl.doors[a - 128];
        for (k = 0; k < d->nareas && k < 4; k++)
            area_vis[d->area[k] >> 3] |= bitmask8[d->area[k] & 7];
    } else
        area_vis[a >> 3] |= bitmask8[a & 7];
    door_state = r_door_state();
    while (changed) {
        changed = 0;
        for (i = 0; i < lvl.ndoors; i++) {
            const ldoor_t *d = &lvl.doors[i];
            int any = 0, all = 1;
            if (i < 32 && !(door_state & (1u << i)))
                continue;           /* closed */
            for (k = 0; k < d->nareas && k < 4; k++) {
                if (area_vis[d->area[k] >> 3] & bitmask8[d->area[k] & 7])
                    any = 1;
                else
                    all = 0;
            }
            if (any && !all) {
                for (k = 0; k < d->nareas && k < 4; k++)
                    area_vis[d->area[k] >> 3] |= bitmask8[d->area[k] & 7];
                changed = 1;
            }
        }
    }
}

static void mark_leaf(int leaf)
{
    const lleaf_t *lf = &lvl.leafs[leaf];
    int i, n;
    if (lvl.leafarea && !area_visible(lvl.leafarea[leaf]))
        return;                     /* behind a closed door */
    BIT_SET(vis.leafs, leaf);
    for (i = 0; i < lf->nummark; i++) {
        int f = lvl.marksurf[lf->firstmark + i];
        if (f != 0xFFFF) {
            BIT_SET(vis.faces, f);
            if (lvl.flatref) {
                /* the flat pieces it shows as (light to polygons) */
                const lflatref_t *r = &lvl.flatref[f];
                int k;
                for (k = 0; k < r->count; k++)
                    BIT_SET(vis.faces, lvl.flatmark[r->first + k]);
            } else {
                int g = lvl.flatof[f];
                if (g != 0xFFFF)
                    BIT_SET(vis.faces, g);  /* the merged face it is part of */
            }
        }
    }
    n = lf->parent;
    while (n >= 0 && !BIT_TEST(vis.nodes, n)) {
        BIT_SET(vis.nodes, n);
        n = lvl.nodeparent[n];
    }
}

void r_markleaves(int leaf)
{
    const lleaf_t *lf = &lvl.leafs[leaf];
    int i;

    memset(&vis, 0, sizeof(vis));
    if (lvl.leafarea)
        flood_areas(leaf);

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

/* have doors opened or closed since the leaves were last marked? */
int r_doors_changed(void)
{
    return lvl.leafarea && r_door_state() != door_state;
}

/* the prepared view and BSP walk become the ones drawn (both CPUs idle) */
void r_commit(void)
{
    memcpy(&view, &view_next, sizeof(view));
    r_flatpal();
    if (cand_ready) {
        cand_cur ^= 1;
        cand_ready = 0;
    }
}

/* ------------------------------------------------------------------ BSP walk */

/* Stage A, the BSP walk, runs once per frame for the whole window: the
 * master walks the next frame's tree (view_next, vis) into the spare
 * candidate list while both CPUs draw from the current one, and each CPU
 * then culls the candidates against its own band in stage B. */
typedef struct { fixed_t n[CP_NUM][3]; fixed_t d[CP_NUM]; } frustum_t;
static frustum_t wfr;               /* the next frame's whole-window frustum */

/* returns -1 if the box is outside, else the remaining clip flags */
static int cull_box(const int16_t *mins, const int16_t *maxs, int clipflags)
{
    int k, m;
    for (k = 0, m = 1; k < CP_NUM; k++, m <<= 1) {
        const fixed_t *n = wfr.n[k];
        int32_t dmax, dmin;
        if (!(clipflags & m))
            continue;
        if (n[0] >= 0) { dmax = n[0] * maxs[0]; dmin = n[0] * mins[0]; }
        else           { dmax = n[0] * mins[0]; dmin = n[0] * maxs[0]; }
        if (n[1] >= 0) { dmax += n[1] * maxs[1]; dmin += n[1] * mins[1]; }
        else           { dmax += n[1] * mins[1]; dmin += n[1] * maxs[1]; }
        if (n[2] >= 0) { dmax += n[2] * maxs[2]; dmin += n[2] * mins[2]; }
        else           { dmax += n[2] * mins[2]; dmin += n[2] * maxs[2]; }
        if (dmax < wfr.d[k])
            return -1;
        if (dmin >= wfr.d[k])
            clipflags &= ~m;
    }
    return clipflags;
}

static inline void add_cand(candlist_t *cl, int fi, int clip)
{
    int n = cl->n;
    if (n < MAXCAND) {
        cl->face[n] = fi;
        cl->clip[n] = clip;
        cl->n = n + 1;
    }
}

static void walk_leaf(candlist_t *cl, const visset_t *vs, int leaf, int clipflags)
{
    const lleaf_t *lf = &lvl.leafs[leaf];
    int i;
    if (!lf->numfrag || !BIT_TEST(vs->leafs, leaf))
        return;
    clipflags = cull_box(lf->mins, lf->maxs, clipflags);
    if (clipflags < 0)
        return;
    for (i = 0; i < lf->numfrag; i++) {
        int fi = lvl.leaffrags[lf->firstfrag + i];
        const lface_t *f = &lvl.faces[fi];
        const lplane_t *pl = &lvl.planes[f->plane];
        fixed_t d = dot16(pl->n, view_next.org) - pl->dist;
        if (f->pad) {
            /* a moving model's fragment: only its current keyframe */
            int m = (f->pad & 0xFF) - 1;
            if ((f->pad >> 8) != view_next.mkey[m])
                continue;
            d -= dot16(pl->n, view_next.mres[m]);
        }
        if ((d < 0) != ((f->flags & FF_PLANEBACK) != 0))
            continue;
        add_cand(cl, fi, clipflags);
    }
}

/* front-to-back walk with an explicit stack (the CPU stack lives in 2 KB of
 * on-chip RAM in the FAST build) */
#define WALK_STACK 72

static void walk_bsp(candlist_t *cl, const visset_t *vs)
{
    uint32_t stack[WALK_STACK];
    int sp = 0, num = lvl.hdr->headnode, clip = (1 << CP_NUM) - 1;
    const int flat = !view_next.textures;

    cl->n = 0;
    for (;;) {
        while (num >= 0) {
            const lnode_t *node;
            int side;
            if (!BIT_TEST(vs->nodes, num))
                goto pop;
            node = &lvl.nodes[num];
            if (clip) {
                clip = cull_box(node->mins, node->maxs, clip);
                if (clip < 0)
                    goto pop;
            }
            side = node_side(node, view_next.org);
            if (sp == WALK_STACK)
                goto pop;           /* cannot happen with E1M1 (depth 63) */
            stack[sp++] = (uint32_t)num | ((uint32_t)side << 16) | ((uint32_t)clip << 17);
            num = node->child[side];
        }
        walk_leaf(cl, vs, -1 - num, clip);
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
            if (flat) {
                /* flat shading: the node's faces with same-coloured
                   neighbours merged (front-facing first, then back) */
                const lflatnode_t *fn = &lvl.flatnode[num];
                const uint16_t *fl = lvl.flatlist + fn->first;
                if (side) {
                    fl += fn->nfront;
                    nf = fn->nback;
                } else
                    nf = fn->nfront;
                for (i = 0; i < nf; i++) {
                    int fi = fl[i];
                    if (BIT_TEST(vs->faces, fi))
                        add_cand(cl, fi, clip);
                }
            } else {
                /* faces are stored front-facing first, then back-facing */
                if (side) {
                    ff = node->firstface + node->numfront;
                    nf = node->numfaces - node->numfront;
                } else {
                    ff = node->firstface;
                    nf = node->numfront;
                }
                for (i = 0; i < nf; i++) {
                    int fi = ff + i;
                    if (BIT_TEST(vs->faces, fi))
                        add_cand(cl, fi, clip);
                }
            }
            num = node->child[!side];
        }
    }
}

static void wfr_plane(int k, fixed_t a, fixed_t b, fixed_t c, fixed_t e)
{
    int i;
    for (i = 0; i < 3; i++)
        wfr.n[k][i] = fixmul(a, view_next.right[i]) + fixmul(b, view_next.down[i]) + fixmul(c, view_next.fwd[i]);
    wfr.d[k] = dot16(wfr.n[k], view_next.org) + e;
}

/* the next frame's candidates: after r_setup_view, r_markleaves and the
 * movers' state (g_build_scene) */
void r_walk(void)
{
    fixed_t ky0 = hw_div(-view_next.cy16, view_next.proj);
    fixed_t ky1 = hw_div((view_next.vh << 16) - view_next.cy16, view_next.proj);
    wfr_plane(CP_NEAR, 0, 0, 0x10000, NEAR_Z);
    wfr_plane(CP_FAR, 0, 0, -0x10000, -view_next.far);
    wfr_plane(CP_LEFT, 0x10000, 0, 0x10000, 0);
    wfr_plane(CP_RIGHT, -0x10000, 0, 0x10000, 0);
    wfr_plane(CP_TOP, 0, 0x10000, -ky0, 0);
    wfr_plane(CP_BOTTOM, 0, -0x10000, ky1, 0);
    walk_bsp(&cands[cand_cur ^ 1], &vis);
    cand_ready = 1;
}

/* ------------------------------------------------------------------ stage D */

static void draw_range(rcpu_t *rc, int a, int b)
{
    int i, depth = scene.zused;
    for (i = a; i < b; i++) {
        frec_t *fr = &rc->frec[i];
        const lfgeom_t *g = &lvl.fgeom[fr->fi];
        rc->faces++;
        rc->spancount += fr->nspan;
        if (!view.textures && !(g->flags & FF_KEEPTEX))
            draw_spans_flat(rc, fr, view.lighting ? g->lit : g->unlit);
        else if (g->flags & FF_FLATTEX)
            draw_spans_flat(rc, fr, (uint8_t)(view.lighting ? fr->f->lightofs >> 8 : fr->f->lightofs));
        else if (g->flags & FF_SKY)
            draw_spans_sky(rc, fr);
        else if (g->flags & FF_TURB)
            draw_spans_turb(rc, fr, 0);
        else
            draw_spans_tex(rc, fr);
        if (depth)
            r_depth_spans(rc, fr, g->flags);
    }
}

/* shade and draw all face records of the batch */
void (*r_poll_hook)(void);

void draw_batch(rcpu_t *rc)
{
    int i, start = 0, n = rc->nfrec;
    if (rc->cpu == 0 && r_poll_hook)
        r_poll_hook();
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

/* see-through liquids, after everything behind them is drawn: back to
 * front, as a mesh that leaves every other pixel of what is behind */
static void draw_liquids(rcpu_t *rc)
{
    int i;
    memcpy(rc->spans, rc->wspans, rc->nwspans * sizeof(span_t));
    for (i = rc->nwfrec - 1; i >= 0; i--) {
        frec_t *fr = &rc->wfrec[i];
        const lfgeom_t *g = &lvl.fgeom[fr->fi];
        rc->faces++;
        rc->spancount += fr->nspan;
        if (!view.textures) {
            draw_spans_flat_mesh(rc, fr, view.lighting ? g->lit : g->unlit);
        } else {
            face_setup(rc, fr);         /* liquids need no surface cache */
            draw_spans_turb(rc, fr, 1);
        }
    }
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

    rc->ky0 = hw_div((row0 << 16) - view.cy16, view.proj);
    rc->ky1 = hw_div((row1 << 16) - view.cy16, view.proj);
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

    rc->rowsleft = row1 - row0;
    for (r = row0; r < row1; r++) {
        gaprow_t *gr = &gaps[r];
        int c = r - view.cut_row0;
        gr->n = 1;
        gr->g[0] = 0;
        gr->g[1] = view.vw;
        if ((unsigned)c < (unsigned)view.cut_n) {
            /* the view weapon (a Mega Drive sprite) hides [x0, x1) */
            int x0 = view.cut_x[c * 2], x1 = view.cut_x[c * 2 + 1];
            if (x1 > view.vw) x1 = view.vw;
            if (x0 >= x1)
                continue;
            if (x0 == 0 && x1 == view.vw) {
                gr->n = 0;
                rc->rowsleft--;
            } else if (x0 == 0)
                gr->g[0] = x1;
            else if (x1 == view.vw)
                gr->g[1] = x0;
            else {
                gr->n = 2;
                gr->g[1] = x0;
                gr->g[2] = x1;
                gr->g[3] = view.vw;
            }
        }
    }
    rc->nfrec = 0;
    rc->nspans = 0;
    rc->nwfrec = 0;
    rc->nwspans = 0;
    rc->fb_ready = 0;
    r_depth_clear(rc);

    ci = 0;
    while (ci < cands[cand_cur].n && rc->rowsleft > 0) {
        ci = geom_polys(rc, &cands[cand_cur], ci);
        geom_raster(rc);
        draw_batch(rc);
    }
    wait_fb(rc);
    fill_gaps(rc);
    if (rc->nwfrec)
        draw_liquids(rc);
    if (scene.ndraw || scene.nparts)
        r_draw_models(rc);
}
