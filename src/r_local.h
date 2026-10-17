/* Renderer shared definitions */
#ifndef R_LOCAL_H
#define R_LOCAL_H

#include "level.h"

#define NOINLINE __attribute__((noinline))

#define NEAR_Z      (4 << 16)
#define MAXGAPS     24
#define MAXCLIPVERTS 24

#define MAXCAND     768         /* candidate faces from the BSP walk */
#define BATCH       32          /* faces per pipeline batch */
#define POLYWORDS   1280        /* screen polygon buffer for one batch */
#ifndef MAXSPANS
#define MAXSPANS    768         /* spans for one batch */
#endif

#define HASHSIZE    512
#define MAX_SURFSIZE (128 * 128)

#define PROJ        80          /* full-size focal length: 90 degree field of view */

#define MAXWFREC    32          /* deferred see-through liquid faces per CPU */
#define MAXWSPANS   512         /* ... and their spans */

#define ANGLE_90    16384
#define ANGLE_180   32768

/* view state: written by the master, read by both CPUs while rendering */
typedef struct {
    fixed_t org[3];
    fixed_t fwd[3], right[3], down[3];
    int32_t r80s5[3], d80s5[3];      /* right/down (16.16) * 2^20 / proj */
    int16_t r16[3][3];               /* rows: right, down, fwd in 2.14 */
    int32_t camr[3];                 /* org . row, in 15.17 */
    fixed_t far;
    int32_t yaw, pitch;
    int     textures, lighting;
    int     cachegen;                /* bumped to flush surface caches */
    int     split;                   /* first row drawn by the slave */
    int     time;                    /* 60 Hz ticks, for sky and water */
    int     mipbias;
    int     flip_fs;                 /* FS value to wait for before drawing */
    /* view window (screen size option): the 3D view is vw x vh rendered
       pixels inside the 160x112 screen, with the same 90 degree field of view */
    int     vw, vh, proj;            /* proj = vw / 2, the focal length */
    int64_t proj40;                  /* proj << 40, dividend for the projection */
    fixed_t vw16;                    /* vw << 16 */
    fixed_t cx16, cy16;              /* projection centre */
    int32_t k24;                     /* 2^24 / proj */
    fixed_t mipscale;                /* PROJ / proj: distance scale for mip choice */
    int     mesh;                    /* draw liquids as a see-through mesh */
    int     underwater;              /* the eye is inside a liquid */
    int     cut_row0, cut_n;         /* rows the view weapon hides (mdlink.h) */
    const uint8_t *cut_x;            /* cut_n pairs x0, x1 */
    /* moving brush models (doors, plats): the keyframe of fragments drawn
       for each, the offset still to apply, and camr for its vertices */
    uint8_t mkey[64];                /* per mover (MAX_MOVERS) */
    fixed_t mres[64][3];
    int32_t mcamr[64][3];
} view_t;

typedef struct { fixed_t x, y, z; } vec3_t;

typedef struct surfcache_s {
    struct surfcache_s **owner;
    uint32_t size;             /* bytes including this header */
    uint16_t key;              /* face */
    uint8_t  mip, pad;
    uint16_t width, height;
    uint16_t batch;            /* last pipeline batch that used it */
    uint16_t pad2;
    uint8_t  data[];
} surfcache_t;

typedef struct { uint8_t n; uint8_t g[MAXGAPS * 2]; } gaprow_t;

#define VCACHE_SIZE 256
typedef struct {
    uint32_t key;              /* vertex index | frame << 16 */
    vec3_t   v;                /* view space */
    fixed_t  sx, sy;           /* projected (valid unless outside near plane) */
    int32_t  oc;               /* outcodes for this CPU's frustum */
    int32_t  pad;
} vcache_t;
typedef struct { uint8_t y, x0, x1, pad; } span_t;

/* a face that produced spans in the current batch, plus its shading setup */
typedef struct {
    const lface_t *f;
    int      fi;
    int      span0, nspan;
    fixed_t  zmin;
    vec3_t   refv;                   /* a polygon vertex (view space) */
    fixed_t  refsx, refsy;           /* ... and its screen position */
    /* filled by face_setup */
    int      mip;
    int32_t  zi0, dzidx, dzidy;
    int32_t  sz0, dszdx, dszdy;
    int32_t  tz0, dtzdx, dtzdy;
    fixed_t  smax, tmax;
    int32_t  soff, toff;             /* turbulent faces: texture offsets */
    surfcache_t *surf;
} frec_t;

/* argument block for the assembly span drawer (offsets fixed in span_asm.s) */
typedef struct {
    int32_t dzidx, dszdx, dtzdx, smax, tmax;
    const uint8_t *src;
    int32_t width;
    const uint16_t *dbl;
    int32_t left, sn, tn;
    const span_t *spanend;
    volatile uint16_t *fb;
    int32_t zi0, sz0, tz0, dzidy, dszdy, dtzdy;
    const span_t *spans;
    int32_t nspans;
} texargs_t;

/* per-CPU renderer state */
typedef struct {
    int      cpu;
    int      row0, row1;
    int      rowsleft;
    fixed_t  ky0, ky1;               /* view-space y/z limits for this CPU */
    fixed_t  fr_n[6][3];             /* world-space frustum planes */
    fixed_t  fr_d[6];
    fixed_t  fr_len[6];              /* |fr_n| (planes are not normalized) */

    /* pipeline buffers */
    int      ncand;
    uint32_t cand[MAXCAND];          /* face | clipflags << 16 */
    int      npolyw;
    int32_t  poly[POLYWORDS];
    int      nfrec;
    frec_t   frec[BATCH + 1];
    frec_t  *cur;                    /* record being filled by geom_raster */
    int      nspans;
    span_t   spans[MAXSPANS];
    uint16_t batch;
    int      fb_ready;

    /* see-through liquids: rasterized against the coverage without filling
       it, then drawn as a mesh after everything else. The buffers are taken
       from the surface cache memory only while the option is on (NULL off). */
    int      nwfrec, nwspans;
    frec_t  *wfrec;                  /* [MAXWFREC] */
    span_t  *wspans;                 /* [MAXWSPANS] */               /* the frame we draw into is no longer displayed */

    vec3_t   clipa[MAXCLIPVERTS], clipb[MAXCLIPVERTS];

    /* vertex cache */
    uint32_t vcframe;
    vcache_t vcache[VCACHE_SIZE];

    /* surface cache */
    uint8_t     *sc_base, *sc_end;
    surfcache_t *sc_rover;
    surfcache_t **sc_hash;
    int          cachegen;

    /* stats */
    int faces, spancount, pixels, surfbuilds, texels, nodes;
    int facecalls, vxforms, clips, leaves;
} rcpu_t;

extern view_t view;
extern uint8_t vis_leafs[], vis_nodes[], vis_faces[];
extern uint8_t colormap[64 * 256];
extern gaprow_t gaps[SCREENH];
extern volatile uint16_t *r_fb;      /* view window origin; 160 words per rendered row */
extern volatile uint16_t *r_screen;  /* top left of the screen */
extern const fixed_t sintab[1024];

static inline fixed_t fsin(int a) { return sintab[(a >> 6) & 1023]; }
static inline fixed_t fcos(int a) { return sintab[((a >> 6) + 256) & 1023]; }

static inline fixed_t dot16(const fixed_t *a, const fixed_t *b)
{
    return (fixed_t)(((int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2]) >> 16);
}

/* v >> mip for mip 0..3 without a variable shift (the SH-2 has no barrel
 * shifter; GCC would call a library routine) */
static inline int shr_mip(int v, int mip)
{
    switch (mip) {
    case 3: v >>= 1; /* fall through */
    case 2: v >>= 1; /* fall through */
    case 1: v >>= 1; /* fall through */
    default: break;
    }
    return v;
}

extern const uint8_t bitmask8[8];
#define BIT_TEST(a, i) ((a)[(i) >> 3] & bitmask8[(i) & 7])
#define BIT_SET(a, i)  ((a)[(i) >> 3] |= bitmask8[(i) & 7])

enum { CP_NEAR, CP_FAR, CP_LEFT, CP_RIGHT, CP_TOP, CP_BOTTOM, CP_NUM };

/* the back buffer becomes writable once the requested flip has happened:
 * geometry for the next frame runs before that, drawing waits here */
static inline void wait_fb(rcpu_t *rc)
{
#ifndef HOST
    if (!rc->fb_ready) {
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) != view.flip_fs) ;
        rc->fb_ready = 1;
    }
#else
    (void)rc;
#endif
}

/* r_main.c */
void r_init(void);
void r_setup_view(fixed_t org[3], int yaw, int pitch);
void r_mover_cam(int nmovers);
void r_markleaves(int leaf);
int  r_pointleaf(const fixed_t p[3]);
void r_render(rcpu_t *rc, int row0, int row1);

/* r_geom.c: candidate faces -> screen polygons -> spans */
int  geom_polys(rcpu_t *rc, int ci);
void geom_raster(rcpu_t *rc);

/* r_setup.c */
int  face_setup(rcpu_t *rc, frec_t *fr);
void face_zsetup(frec_t *fr);

/* r_model.c: models, particles and the view weapon, with depth only in
   the per-row intervals around them */
void r_depth_clear(rcpu_t *rc);
void r_depth_spans(rcpu_t *rc, frec_t *fr, int flags);
void r_draw_models(rcpu_t *rc);
extern uint16_t zbuf_mem[];

/* r_surf.c */
void sc_init(rcpu_t *rc, uint8_t *mem, int size, surfcache_t **hash);
void r_arena(rcpu_t *rc, uint8_t *mem, int size, surfcache_t **hash, int mesh);
void sc_flush(rcpu_t *rc);
surfcache_t *sc_get(rcpu_t *rc, int faceidx, int mip);

/* r_span.c */
void r_span_init(void);
void r_level_init(void);
void r_oram_init(void);
void draw_spans_tex(rcpu_t *rc, const frec_t *fr);
void draw_spans_flat(rcpu_t *rc, const frec_t *fr, int color);
void draw_spans_sky(rcpu_t *rc, const frec_t *fr);
void draw_spans_turb(rcpu_t *rc, const frec_t *fr, int mesh);
void draw_spans_flat_mesh(rcpu_t *rc, const frec_t *fr, int color);
void fill_gaps(rcpu_t *rc);
void draw_batch(rcpu_t *rc);
extern void (*r_poll_hook)(void);   /* called by the master between batches */

#endif
