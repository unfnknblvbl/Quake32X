// The level blob the cartridge plays: a Quake BSP29 map as a compact,
// fixed-point, big-endian blob for the 32X renderer (the port of
// tools/mkmap.py, with the editor's optimisations).
//
// The layout is described by the header (LVL_* in the game's src/level.h).
// With no edits the output is exactly what tools/mkmap.py wrote. Edits that
// need game 0.7.0 or later (light-to-polygon pieces, door areas, draw
// distance areas, light grids, reduced textures, movers as models, model
// LOD, sound formats and the shared sound bank) go in an extension
// directory the header points to; older levels have none.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "q32x/flatpal.h"
#include "q32x/gamedata.h"
#include "q32x/quake.h"

namespace q32x {

// the order of the header's lumps
extern const std::vector<std::string> LEVEL_LUMPS;
// the extension lumps (game 0.7.0)
extern const std::vector<std::string> LEVEL_EXT_LUMPS;
constexpr int LEVEL_HDR_SIZE = 4 + 4 + 32 * 8 + 64;
constexpr int LEVEL_HDR_EXTOFS = 296;     // i32: the extension directory, 0 = none
constexpr int LEVEL_HDR_FLAGS = 300;      // i32: LF_*
constexpr int LF_FLAT_ONLY = 1;           // no textures or light maps: flat shading only
constexpr int MAX_LEVEL_FACES = 10240;    // MAX_MAP_FACES in src/r_local.h
constexpr int MAX_LEVEL_FLAT_PIECES = 16; // vertices of a flat-mode polygon (room for the clipper)

// ------------------------------------------------------------ edits

struct FaceEdit {
    int colour = -1;       // palette index for flat shading (-1: the texture's average)
    bool exact = false;    // that colour at every light level (no light map shading)
    bool lock = false;     // never merged, split or removed by the automatic tools
    bool remove = false;   // not drawn
    bool no_split = false; // light-to-polygon leaves it whole
    int band = -1;         // light-to-polygon: the whole face in this band (-1: from its light map)
};

// a colour remap for flat shading, for the whole level or some leaves
struct PaletteRemap {
    std::array<uint8_t, 256> map{};
    std::vector<int> leafs;   // empty: the whole level
    static PaletteRemap identity();
};

// the light maps as hard light and shadow regions, made into polygons for
// flat shading
struct LightPolyOptions {
    bool enabled = false;
    int bands = 2;                // light levels (2: light and shadow)
    double min_area = 512;        // smallest region kept, square world units
    int max_pieces = 12;          // pieces per face before it is left whole
    int budget = 0;               // extra polygons for the level (0: no limit)
    double contrast = 1.0;        // spread of the band colours around the average (1: as measured)
    std::vector<int> off_leafs;   // faces in these leaves stay whole
    // plain colours with hard shadows: no light-map shading in flat mode;
    // the brightest band shows each face's colour at lit_row, every darker
    // band shadow_step rows darker (faces not split take their main band)
    bool hard_shadows = false;
    int lit_row = 32;             // colormap row of the lit colour (32: the colour itself)
    int shadow_step = 12;         // rows darker per band
};

// a flat colour for the faces whose texture matches, on some surfaces
struct ColourRule {
    std::vector<std::string> match;   // texture name globs ("*" all)
    int surface = 0;                  // 0 any, 1 floors, 2 walls, 3 ceilings
    int colour = 0;                   // palette index
};

struct ReduceOptions {
    bool merge_textured = false;  // neighbouring faces with the same texture mapping merge (light maps joined)
    int max_ext = 256;            // largest merged face, texels
    double min_area = 0;          // faces smaller than this (square units) are dropped, unless locked
    bool fix_tjunctions = true;   // flat-mode polygons get the vertices their neighbours have on their edges
};

// a reduced texture (the editor's 16x16, 4 bits per texel layer)
struct ReducedTexture {
    int min_mip = 2;              // the finest mip level drawn (2: a 64x64 texture as 16x16)
    bool four_bit = true;         // 16 colours, 4 bits per texel
    std::array<uint8_t, 16> pal{};// palette indices of the 16 colours
    Bytes pixels;                 // at min_mip size: 4-bit indices (one per byte) or palette indices
};

struct DrawArea {
    std::string name;
    std::vector<int> leafs;
    int far = 0;                  // draw distance in world units (0: no limit)
};

struct LevelEdits {
    std::map<int, FaceEdit> faces;           // by BSP face index
    std::vector<PaletteRemap> remaps;
    std::vector<ColourRule> colour_rules;    // the first that matches a face gives its colour
    LightPolyOptions light;
    ReduceOptions reduce;
    bool vertex_order = false;               // vertices in the order the faces use them
    bool flat_only = false;                  // drop textures (but kept-textured ones) and light maps
    int light_grid = 0;                      // model lighting from a grid this many units apart (0: light maps)
    bool doors_block = false;                // closed doors hide what is behind them
    bool simple_collision = false;           // the clip hulls rebuilt balanced (the same solid space)
    std::set<int> door_exclude;              // mover indices that never block
    std::vector<DrawArea> areas;
    std::map<std::string, ReducedTexture> textures;   // by texture name
    // a texture's own pixels replaced (w x h palette indices; its mips are
    // made from them), by texture name
    std::map<std::string, Bytes> texture_pixels;
    std::set<int> mover_models;              // mover indices drawn as models
    // flat mode in a palette of the user's own (colours, rules and face
    // colours are then indices into it), shaded by occlusion and lights
    FlatPalOptions flatpal;
    bool empty() const;
};

struct LevelOptions {
    bool big_endian = true;          // false: the host build's copy
    std::string demo;                // a demo in the pak (e.g. "demo1.dem") for the attract mode
    int flat_tol = 4;                // light steps (of 64) faces may differ by and still merge
    GameDataOptions game;
    LevelEdits edits;
    const Bytes *bsp_data = nullptr; // the map itself, in place of the pak's copy
};

// ------------------------------------------------------------ results

struct LevelInfo {
    std::map<std::string, Lump> lumps;
    std::map<std::string, Lump> ext;
    size_t size = 0;
    int faces = 0, world_faces = 0, fragments = 0, merged = 0, flat_faces = 0;
    int nodes = 0, leafs = 0, verts = 0, movers = 0, triggers = 0, entities = 0;
    int textured_merged = 0;         // BSP faces merged away in textured mode
    int pieces = 0;                  // light-to-polygon pieces
    int removed = 0;
    int areas = 0, door_portals = 0;
    size_t sound_bytes = 0, model_bytes = 0, texture_bytes = 0, light_bytes = 0;
    std::vector<std::string> warnings;
    // per output world face: the BSP faces it came from
    std::vector<std::vector<int>> world_members;
    // per BSP world face: its light-to-polygon band map (smax x tmax), if split
    std::map<int, std::vector<uint8_t>> bands;
    std::vector<int> band_rows;      // the colormap row of each band
};

Bytes build_level(const Pak &pak, const std::string &bsp_name, const LevelOptions &opt,
                  LevelInfo *info = nullptr, std::string *log = nullptr);

// the KEEP_TEXTURE rule: textures drawn textured even in flat-shaded mode
bool keep_textured(const std::string &name);

}  // namespace q32x
