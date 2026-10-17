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
    LVL_TRIGGERS, LVL_STRINGS, LVL_DEMO,
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
} lheader_t;

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
    int numfaces, numleafs, numnodes, numplanes, visleafs;
} level_t;

extern level_t lvl;

void level_init(const void *blob);

#endif
