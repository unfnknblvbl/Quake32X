// Game data for a level blob: entities, flat-coloured models, sounds,
// movers and triggers (the port of tools/mkents.py).
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "q32x/movers.h"
#include "q32x/quake.h"

namespace q32x {

extern const std::vector<std::string> CLASSES;
struct ModelName {
    std::string name, path;
};
extern const std::vector<ModelName> MODELS;
extern const std::vector<std::string> SOUNDS;
extern const std::vector<std::string> MOVER_IDS;
extern const std::vector<std::string> TRIGGER_IDS;
extern const std::vector<std::string> FRAME_MODELS;
extern const double ANORMS[162][3];

int model_index(const std::string &name);   // -1 if unknown
int sound_index(const std::string &name);

// the C headers the engine is built with (src/g_ids.h, src/g_frames.h)
std::string ids_header();
std::string frames_header(const Pak &pak);

// targetname/target strings -> small numbers, shared by entities, movers
// and triggers
struct Names {
    std::map<std::string, int> ids;
    int operator()(const OptStr &n);
};

// an alias model or item box as the engine's flat-coloured model
struct FlatModel {
    double scale[3], origin[3];
    double radius;
    int flags;
    int nverts;
    std::vector<std::array<int, 3>> tris;
    std::vector<Bytes> frames;     // nverts x {x, y, z, normal}
    std::vector<Bytes> colours;    // per skin: one palette index per triangle
    // trimmed frames: for each of the model's frames, the stored frame drawn
    // for it (empty: frames are stored one for one); needs game 0.7.0
    std::vector<int> frame_map;
    int logical_frames() const { return frame_map.empty() ? int(frames.size()) : int(frame_map.size()); }
};
constexpr int MF_FRAMEMAP = 0x2000;   // the model has a frame map after its boxes
FlatModel load_mdl_flat(const Bytes &d, const Palette &pal, bool fullbright);
FlatModel load_bsp_model(const Bytes &d, const Palette &pal);
Bytes build_model(const FlatModel &md, bool big);
FlatModel load_model(const Pak &pak, const std::string &name, const Palette &pal);

struct Sound {
    Bytes data;      // 8-bit unsigned PCM
    int rate = 11025;
    int loop = -1;
};
// a .wav as Quake loads it, resampled to 11025 Hz 8-bit
Sound load_wav(const Bytes &d);

struct Lump {
    size_t ofs = 0, count = 0;
};

// changes to a sound; every sound is stored 8-bit at 11025 Hz, the mixer's
// own rate and its fastest format
struct SoundEdit {
    double gain = 1.0;         // loudness
    bool trim = false;         // quiet lead-in and tail cut off
    int threshold = 2;         // what counts as quiet (of 128)
    bool plain() const { return gain == 1.0 && !trim; }
};
Sound apply_sound_edit(const Sound &s, const SoundEdit &e);
// any .wav (8 or 16 bits, mono or stereo, any rate) as 8-bit 11025 Hz
Sound convert_wav(const Bytes &d);

// a sound in the shared bank (offsets from the bank's start)
struct BankEntry {
    uint32_t ofs = 0;
    uint32_t len = 0;
    int loop = -1;
};

// a lower-detail version of a model, used beyond `dist` units
struct ModelLod {
    FlatModel model;
    int dist = 512;
};

struct GameDataOptions {
    // the editor can replace or drop models and sounds; empty = from the pak
    std::map<std::string, FlatModel> models;
    std::map<std::string, Sound> sounds;
    std::map<std::string, SoundEdit> sound_edits; // by sound name
    std::map<std::string, BankEntry> bank;        // sounds in the shared bank
    std::map<std::string, ModelLod> lods;         // by model name
    std::set<std::string> drop_sounds;            // sounds left out entirely
};

// what a level can use: model names and sound names
void level_needs(const std::vector<Entity> &ents, const std::vector<Mover> &movers, const Entity &world,
                 const std::vector<std::string> &extra_models, const std::vector<std::string> &extra_sounds,
                 std::set<std::string> &models, std::set<std::string> &sounds);

// append entities, models, sounds, movers, triggers and strings; returns the
// lumps by name
std::map<std::string, Lump> add_game_data(Buf &blob, const Pak &pak, const Palette &pal,
                                          const std::vector<Entity> &ents, const Bsp &bsp,
                                          const std::vector<Mover> &movers, const Entity &world,
                                          const std::vector<std::string> &extra_models,
                                          const std::vector<std::string> &extra_sounds,
                                          const GameDataOptions &opt, std::string *log);

}  // namespace q32x
