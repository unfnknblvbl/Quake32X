/* Level blob layout produced by tools/mkmap.py */
#ifndef LEVEL_H
#define LEVEL_H

#include "hw.h"

enum {
    LVL_PLANES, LVL_NODES, LVL_LEAFS, LVL_MARKSURF, LVL_FACES, LVL_FVERTS,
    LVL_VERTS, LVL_TEXINFO, LVL_TEXTURES, LVL_LIGHT, LVL_VIS, LVL_LEAFFRAGS,
    LVL_CLIPNODES, LVL_PALETTE, LVL_COLORMAP, LVL_CONCHARS, LVL_SKYTEX,
    LVL_FACESPH, LVL_NODEPARENT, LVL_BACKTILE,
    LVL_ENTS, LVL_MODELS, LVL_ANORMS, LVL_SOUNDS, LVL_MOVERS, LVL_MKEYS,
    LVL_TRIGGERS, LVL_STRINGS, LVL_DEMO, LVL_FLATOF, LVL_FLATNODE, LVL_FLATLIST,
    LVL_NUMLUMPS
};

typedef struct {
    char    magic[4];
    int32_t numlumps;
    struct { int32_t ofs, count; } lump[LVL_NUMLUMPS];
    fixed_t spawn[3];
    int32_t yaw;
    int16_t headnode, headnode1, numnodes, nworld, visleafs, headnode2;
    int16_t worldtype, pad;              /* 0 medieval, 1 metal (runic), 2 base */
    int32_t  extofs;                     /* the extension directory (game 0.7.0), 0: none */
    int32_t  flags;                      /* LF_ */
} lheader_t;

#define LF_FLAT_ONLY 1                   /* no textures or light maps: flat shading only */

/* extension lumps (the editor's optimisations), in the directory at extofs */
enum {
    LX_FLATREF, LX_FLATMARK, LX_LIGHTGRID, LX_LEAFAREA, LX_DOORS, LX_LEAFFAR,
    LX_MVMODELS, LX_MDLLOD, LX_FLATPAL, LX_NUMLUMPS
};

typedef struct {
    char    magic[4];                    /* "Q32X" */
    int32_t numlumps;
    struct { int32_t ofs, count; } lump[LX_NUMLUMPS];
} lextdir_t;

typedef struct { uint16_t first; uint8_t count, pad; } lflatref_t;   /* into flatmark */
typedef struct { uint8_t mover, nareas, area[4]; uint16_t pad; } ldoor_t;
typedef struct { int16_t org[3]; uint16_t step, dims[3], pad; uint8_t v[]; } lgrid_t;
typedef struct { uint32_t ofs; uint16_t dist, pad; } lmdllod_t;
/* flat mode in the user's palette: its 32X colours, Quake's colours as its
   (particles), and the colormap from Quake's colours to its (models, the
   textures kept in flat mode); the faces' flat colours are its already */
typedef struct { uint16_t cram[256]; uint8_t remap[256]; uint8_t cmap[64 * 256]; } lflatpal_t;

typedef struct { fixed_t n[3]; fixed_t dist; } lplane_t;            /* 16 */

typedef struct {
    int16_t  child[2];                   /* child < 0: leaf -1-child */
    uint16_t firstface, numfaces;
    int16_t  mins[3], maxs[3];
    uint8_t  type;                       /* 0-2: axial plane on x/y/z */
    uint8_t  numfront;                   /* faces [firstface, +numfront) face the front */
    int16_t  normal[3];                  /* 2.14 */
    fixed_t  dist;
} lnode_t;                                                          /* 32 */

/* compact per-face record for the geometry stages (one cache line) */
typedef struct {
    uint16_t firstvert;
    uint8_t  numverts, flags, lit, unlit;
    uint16_t pad;
    int16_t  x, y, z, r;                 /* bounding sphere, world units */
} lfgeom_t;

typedef struct {
    int16_t  contents, parent;
    int32_t  visofs;
    uint16_t firstmark, nummark, firstfrag, numfrag;
    int16_t  mins[3], maxs[3];
} lleaf_t;                                                          /* 28 */

#define FF_PLANEBACK 1
#define FF_SKY       2
#define FF_TURB      4
#define FF_DARK      8
#define FF_LIQUID   16                   /* water, slime, lava (not teleporters) */
#define FF_UNDERWATER 32                 /* only visible from inside a liquid */
#define FF_KEEPTEX  64                   /* textured in flat-shaded mode too: switches, exits */
#define FF_FLATTEX 128                   /* flat with textures on too (liquids, sky, glass): no
                                            texels; lightofs holds its colours in Quake's palette
                                            (lit << 8 | unlit) */

typedef struct {
    uint16_t firstvert;
    uint8_t  numverts, flags;
    uint16_t plane, texinfo;
    int16_t  tmins[2];
    uint16_t ext[2];
    int32_t  lightofs;
    uint8_t  lit, unlit;
    uint16_t pad;
} lface_t;                                                          /* 24 */

typedef struct { int16_t x, y, z, pad; } lvert_t;                   /* 13.3 */

typedef struct {
    fixed_t  s[4], t[4];
    uint16_t tex, flags;
} ltexinfo_t;                                                       /* 36 */

typedef struct {
    uint16_t w, h;
    uint8_t  avg, flags;
    uint16_t pad;
    int32_t  mip[4];     /* offsets from the start of the blob */
} ltexture_t;                                                       /* 24 */

typedef struct { int16_t plane, child[2], pad; } lclipnode_t;

#define CONTENTS_EMPTY  -1
#define CONTENTS_SOLID  -2
#define CONTENTS_WATER  -3
#define CONTENTS_SLIME  -4
#define CONTENTS_LAVA   -5
#define CONTENTS_SKY    -6

typedef struct {
    uint16_t first;                      /* into flatlist: front faces, then back */
    uint8_t  nfront, nback;
} lflatnode_t;

typedef struct {
    const uint8_t     *base;
    const lheader_t   *hdr;
    const lplane_t    *planes;
    const lnode_t     *nodes;
    const lleaf_t     *leafs;
    const uint16_t    *marksurf;
    const lface_t     *faces;
    const uint16_t    *fverts;
    const lvert_t     *verts;
    const ltexinfo_t  *texinfo;
    const ltexture_t  *textures;
    const uint8_t     *light;
    const uint8_t     *vis;
    const uint16_t    *leaffrags;
    const lclipnode_t *clipnodes;
    const uint16_t    *palette;
    const uint8_t     *colormap;
    const uint8_t     *conchars;
    const uint8_t     *skytex;
    const lfgeom_t    *fgeom;
    const int16_t     *nodeparent;
    const uint8_t     *backtile;         /* 64x64 border tile */
    /* the editor's extensions (NULL when the level has none) */
    const lflatref_t  *flatref;          /* per face: the flat pieces it shows as */
    const uint16_t    *flatmark;
    const lgrid_t     *grid;             /* model lighting */
    const uint8_t     *leafarea;         /* per leaf: its area (doors divide them) */
    const ldoor_t     *doors;
    int                ndoors;
    const uint16_t    *leaffar;          /* per leaf: draw distance in units, 0 none */
    const uint32_t    *mvmodels;         /* per mover: a model drawn in its place */
    const lmdllod_t   *mdllod;           /* per model id: a simpler model far away */
    const lflatpal_t  *flatpal;          /* flat mode's own palette */
    int                flags;            /* LF_ */
    /* flat shading: faces merged where their colours match (tools/mkmap.py) */
    const uint16_t    *flatof;           /* per face: the merged face it is part of, or 0xFFFF */
    const lflatnode_t *flatnode;         /* per node: its faces in flat mode */
    const uint16_t    *flatlist;
    int numfaces, numleafs, numnodes, numplanes, visleafs;
} level_t;

extern level_t lvl;

void level_init(const void *blob);

#endif
