// The Mega Drive side graphics (build/md_data.bin): fixed tiles (font,
// border), palettes, and the view weapons pre-rendered as Mega Drive
// sprites (the port of tools/mkmd.py), plus the editor's controls over them.
//
// Layout (big-endian, offsets from the start of the file):
//   header   'MDQ1', static tile ofs, static tile count, static palette ofs,
//            set count, set table ofs, size count, weapon count
//   static   tiles 0..STATIC_TILES-1 (32 bytes each, 4 bits per pixel)
//   palettes line 0 (the font and the border share it) and line 1 (unused)
//   sets     one per (weapon, screen size), 32 bytes each:
//              tiles ofs, tile count (u16), frame count (u16),
//              frame table ofs, palette ofs, cutout table ofs, pad
//              (with compressed tiles: bit 31 of the tiles ofs is set and
//              the tiles are one packed stream per frame, see md_pack)
//   frame    u16 sprite count, then per sprite four words, ready for the
//            sprite table: y + 128, size << 8, tile word, x + 128
//   cutout   u8 first row, u8 row count, then (x0, x1) per row (x1 = 0: none)
//   palette  48 words: palette lines 1, 2 and 3
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "q32x/quake.h"

namespace q32x {

// screen sizes: view width in 32X pixels (2 screen pixels), height in 32X
// rows (2 lines); must match view_sizes in src/game.c
extern const std::array<std::array<int, 2>, 6> MD_SIZES;
extern const std::vector<std::string> MD_WEAPONS;   // in the order of MDW_ in src/mdlink.h
constexpr int MD_VIEW_ROWS = 104;
constexpr int MD_FONT_TILE = 16;
constexpr int MD_BORDER_TILE = MD_FONT_TILE + 256;
constexpr int MD_SHADE_TILE = MD_BORDER_TILE + 64;
constexpr int MD_STATIC_TILES = MD_SHADE_TILE + 64;   // 400
constexpr int MD_GUN_TILE = MD_STATIC_TILES;
constexpr int MD_GUN_TILES_MAX = 1536 - MD_GUN_TILE;  // VRAM 0x0000-0xBFFF holds tiles
constexpr int MD_GUN_NPAL = 3;

// one weapon at one render size: every frame as 4-bit colour indices on the
// set's three palettes, and the sprite blocks
struct MdBlock {
    int y, x, rows, cols, pal;
};
struct MdWeaponSet {
    int weapon = 0, size = 0;        // the render size
    int vw = 0, vh = 0;
    std::vector<std::vector<uint8_t>> frames;   // (2vh) x (2vw) indices 0..15 (0 transparent)
    std::vector<std::vector<MdBlock>> blocks;
    std::array<uint16_t, 48> pal{};  // CRAM words, palette lines 1..3
};

struct MdGfxOptions {
    std::string preview_dir;        // PNG previews of every set
    // the render size used at each screen size (default: its own). A size
    // may use a smaller render (a larger index): one or two renders for all
    // six screen sizes cut the weapon data to a fraction.
    std::array<int, 6> weapon_size = {0, 1, 2, 3, 4, 5};
    bool compress = false;          // packed weapon tiles (needs game 0.7.0)
    // flat-shaded renders too, shown when textures are off (needs game 0.7.0;
    // their sets follow the textured ones)
    bool flat_weapons = false;
    std::optional<Bytes> border;    // 64x64 palette indices in place of gfx.wad's backtile
    std::optional<Bytes> font;      // 128x128 palette indices in place of conchars
    // hand-edited sets, by (weapon, render size): used instead of a render
    std::map<std::pair<int, int>, MdWeaponSet> edited;
    // brightness of the weapon renders (Quake's brightest light x this)
    double light_scale = 1.3;
    double dither = 0.6;
};

struct MdGfx {
    Bytes data;
    std::vector<MdWeaponSet> sets;    // the renders used (one per weapon and used render size)
    Bytes static_tiles;               // 400 tiles
    std::array<uint16_t, 16> font_pal{};
    size_t weapon_bytes = 0;
    int largest_set = 0;
};

MdGfx build_mdgfx(const Pak &pak, const MdGfxOptions &opt, std::string *log = nullptr);
// a weapon at a render size, as the default pipeline makes it
MdWeaponSet render_weapon_set(const Pak &pak, int weapon, int size, double light_scale = 1.3, double dither = 0.6,
                              bool flat = false);

// the run-length + back-reference packing of weapon tiles (md_pack in the
// game's src-md/md.s unpacks it)
Bytes md_pack(const Bytes &tiles);
Bytes md_unpack(const Bytes &packed, size_t size);

}  // namespace q32x
