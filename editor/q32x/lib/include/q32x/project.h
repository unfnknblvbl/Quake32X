// A Quake 32X project: the cartridge's levels and everything the editor
// changes about them, saved as JSON (.q32x). A project refers to the
// player's own pak0.pak and never copies Quake's data into itself; what it
// holds is the user's own work (colours, painted textures, settings).
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "q32x/json.h"
#include "q32x/level.h"
#include "q32x/mdgfx.h"
#include "q32x/rom.h"

namespace q32x {

struct LevelProject {
    std::string name;          // e1m1 (the cartridge's level name, up to 11 characters)
    std::string bsp;           // the map in the pak (maps/e1m1.bsp) ...
    std::string file;          // ... or a .bsp file of its own (relative to the project)
    std::string demo;          // a demo in the pak for the attract mode (demo1.dem)
    int flat_tol = 4;
    LevelEdits edits;
};

// a rule giving textures new pixels: the first rule a texture's name
// matches is used
struct TextureRule {
    std::vector<std::string> match;   // glob patterns ('*', '?'), e.g. "+*button"
    std::string image;                // a PNG (relative to the project), or ...
    Bytes pixels;                     // ... palette indices (pixels_w x pixels_h)
    int pixels_w = 0, pixels_h = 0;
    bool reduce = true;               // drawn from a coarse mip level at 16 colours (smaller, faster)
    int min_size = 8;                 // the reduced texture's smallest side
    bool tile = false;                // the image repeats at its own size instead of covering the texture
    std::vector<std::string> levels;  // the levels it applies to (empty: all)
};

struct SoundProject {
    SoundEdit edit;
    std::string file;          // a .wav of the user's own in its place
    int bank = -1;             // -1: automatic (sounds every level has), 0: in each level, 1: in the bank
    bool drop = false;         // left out
};

struct ModelProject {
    int frame_step = 1;        // keep every nth frame of each animation
    double simplify = 1.0;     // fraction of the triangles kept
    double lod_ratio = 0;      // a distance version with this fraction (0: none)
    int lod_dist = 512;
};

struct BudgetReport {
    std::string level;
    size_t bytes = 0, window = 0;
    struct Item {
        std::string what;
        long value = 0, limit = 0;
        bool error = false;
    };
    std::vector<Item> items;
    std::vector<std::string> warnings;
    bool ok() const
    {
        for (auto &i : items)
            if (i.error) return false;
        return true;
    }
};

struct ExportResult {
    bool ok = false;
    std::vector<std::string> files;
    std::string error;
};

struct SoundBank {
    Bytes data;
    std::map<std::string, BankEntry> entries;
};

class Project {
public:
    std::string path;          // the .q32x file (empty: not saved yet)
    std::string pak_path;
    std::string program_path;  // the game program (q32x.bin) for the cartridge
    std::string version = "0.7.0";
    std::string rom_name = "quake32x";
    std::vector<LevelProject> levels;
    std::vector<TextureRule> texture_rules;
    std::map<std::string, ReducedTexture> reduced;   // per texture name, by hand
    std::map<std::string, SoundProject> sounds;
    bool shared_bank = false;
    int sound_format = 0;      // every sound: SF_HALF (5.5 kHz) | SF_PACK (4-bit), gamedata.h
    // every texture without a rule of its own at half size (its finest level
    // left out: a quarter of the texels), liquids and skies apart
    bool half_textures = false;
    // textures drawn in their flat colour with textures on too (globs): no
    // texels stored (LevelEdits::flat_textures)
    std::vector<std::string> flat_textures;
    std::map<std::string, ModelProject> models;
    MdGfxOptions md;

    static Project load(const std::string &path);
    static Project from_json(const Json &j, const std::string &path);
    static Project quake_default(const std::string &pak_path);   // E1M1-E1M3 as the cartridge has them
    Json to_json() const;
    void save(const std::string &path);

    // the project's files are relative to it
    std::string resolve(const std::string &p) const;
    void attach_pak(const Pak *p) { pak_ = p; }
    const Pak &pak() const;
    Palette palette() const;

    LevelOptions level_options(const LevelProject &lp, bool big_endian = true) const;
    Bytes level_bsp(const LevelProject &lp) const;
    Bytes build_level(const std::string &name, const LevelOptions &opt, LevelInfo *info, std::string *log) const;
    Bytes build_level(const LevelProject &lp, bool big_endian, LevelInfo *info, std::string *log) const;
    MdGfxOptions mdgfx_options() const;
    SoundBank build_bank(std::string *log) const;
    GameDataOptions game_options(const SoundBank *bank) const;
    // the textures each rule gives a level (by texture name)
    void texture_edits(const Bsp &bsp, const std::string &level, LevelEdits &e) const;

    std::vector<BudgetReport> check_all(std::string *log) const;
    BudgetReport check_level(const LevelProject &lp, const LevelInfo &info, size_t window) const;
    ExportResult export_all(const std::string &dir, std::string *log) const;
    // turn on the tools that keep the picture as it is, then tighten the
    // others step by step until the level fits its room in the cartridge
    // (and the engine's limits); returns what was done
    std::vector<std::string> auto_optimise(size_t level, std::string *log);

private:
    const Pak *pak_ = nullptr;
    mutable std::shared_ptr<Pak> own_pak_;
};

bool glob_match(const std::string &pattern, const std::string &s);
std::string to_hex(const Bytes &b);
Bytes from_hex(const std::string &s);

}  // namespace q32x
