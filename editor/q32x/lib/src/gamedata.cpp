#include "q32x/gamedata.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace q32x {

const std::vector<std::string> CLASSES = {
    "none",
    "monster_army", "monster_dog", "monster_ogre", "monster_knight",
    "monster_demon1", "monster_wizard", "monster_zombie",
    "item_health", "item_shells", "item_spikes", "item_rockets",
    "item_armor1", "item_armor2", "item_armorInv",
    "weapon_supershotgun", "weapon_nailgun", "weapon_grenadelauncher",
    "item_artifact_super_damage", "item_artifact_envirosuit", "item_artifact_invisibility",
    "item_key1", "item_key2",
    "trigger_changelevel", "backpack", "info_teleport_destination",
    "misc_explobox", "trap_spikeshooter",
    "light_torch_small_walltorch", "light_flame_large_yellow", "light_flame_small_yellow",
    "ambient_comp_hum", "ambient_drone", "ambient_drip", "ambient_swamp1", "ambient_swamp2",
    "light_fluoro", "light_fluorospark",
    "info_intermission",
};

const std::vector<ModelName> MODELS = {
    {"soldier", "progs/soldier.mdl"}, {"dog", "progs/dog.mdl"}, {"ogre", "progs/ogre.mdl"},
    {"knight", "progs/knight.mdl"}, {"demon", "progs/demon.mdl"}, {"wizard", "progs/wizard.mdl"},
    {"zombie", "progs/zombie.mdl"}, {"h_guard", "progs/h_guard.mdl"}, {"h_dog", "progs/h_dog.mdl"},
    {"h_ogre", "progs/h_ogre.mdl"}, {"h_knight", "progs/h_knight.mdl"}, {"h_demon", "progs/h_demon.mdl"},
    {"h_wizard", "progs/h_wizard.mdl"}, {"h_zombie", "progs/h_zombie.mdl"}, {"gib1", "progs/gib1.mdl"},
    {"gib2", "progs/gib2.mdl"}, {"gib3", "progs/gib3.mdl"}, {"spike", "progs/spike.mdl"},
    {"grenade", "progs/grenade.mdl"}, {"zom_gib", "progs/zom_gib.mdl"}, {"w_spike", "progs/w_spike.mdl"},
    {"armor", "progs/armor.mdl"}, {"g_shot", "progs/g_shot.mdl"}, {"g_nail", "progs/g_nail.mdl"},
    {"g_rock", "progs/g_rock.mdl"}, {"backpack", "progs/backpack.mdl"}, {"quaddama", "progs/quaddama.mdl"},
    {"suit", "progs/suit.mdl"}, {"invisibl", "progs/invisibl.mdl"}, {"w_s_key", "progs/w_s_key.mdl"},
    {"w_g_key", "progs/w_g_key.mdl"}, {"flame", "progs/flame.mdl"}, {"flame2", "progs/flame2.mdl"},
    {"b_bh10", "maps/b_bh10.bsp"}, {"b_bh25", "maps/b_bh25.bsp"}, {"b_bh100", "maps/b_bh100.bsp"},
    {"b_shell0", "maps/b_shell0.bsp"}, {"b_shell1", "maps/b_shell1.bsp"}, {"b_nail0", "maps/b_nail0.bsp"},
    {"b_nail1", "maps/b_nail1.bsp"}, {"b_rock0", "maps/b_rock0.bsp"}, {"b_rock1", "maps/b_rock1.bsp"},
    {"b_explob", "maps/b_explob.bsp"},
};

static const std::vector<std::string> FULLBRIGHT = {"flame", "flame2"};
const std::vector<std::string> FRAME_MODELS = {"soldier", "dog", "ogre", "knight", "demon", "wizard", "zombie"};

const std::vector<std::string> SOUNDS = {
    // teleports, the player's weapons and items
    "misc/r_tele1", "misc/r_tele2", "misc/r_tele3", "misc/r_tele4", "misc/r_tele5",
    "weapons/guncock", "weapons/shotgn2", "weapons/rocket1i", "weapons/spike2",
    "weapons/grenade", "weapons/bounce", "weapons/r_exp3", "weapons/ax1",
    "weapons/pkup", "weapons/lock4",
    "weapons/tink1", "weapons/ric1", "weapons/ric2", "weapons/ric3",
    "items/health1", "items/r_item1", "items/r_item2", "items/armor1",
    "items/damage", "items/damage2", "items/damage3", "items/suit", "items/suit2",
    "items/inv1", "items/inv2", "items/inv3",
    "misc/medkey", "misc/runekey",
    // the player
    "player/pain1", "player/pain2", "player/pain3", "player/death1", "player/death2",
    "player/gib", "player/udeath", "player/plyrjmp8", "player/land",
    "player/axhit2", "player/h2ojump", "player/inh2o", "misc/outwater",
    "player/gasp2", "player/drown1", "player/slimbrn2", "player/lburn1",
    // monsters
    "soldier/sight1", "soldier/sattck1", "soldier/pain1", "soldier/pain2",
    "soldier/death1", "soldier/idle",
    "dog/dsight", "dog/dattack1", "dog/dpain1", "dog/ddeath", "dog/idle",
    "ogre/ogwake", "ogre/ogidle", "ogre/ogidle2", "ogre/ogdrag", "ogre/ogpain1",
    "ogre/ogdth", "ogre/ogsawatk",
    "knight/ksight", "knight/idle", "knight/khurt", "knight/kdeath", "knight/sword1",
    "knight/sword2",
    "demon/sight2", "demon/idle1", "demon/dpain1", "demon/ddeath", "demon/djump",
    "demon/dland2", "demon/dhit2",
    "wizard/wsight", "wizard/widle1", "wizard/widle2", "wizard/wpain", "wizard/wdeath",
    "wizard/wattack", "wizard/hit",
    "zombie/z_idle", "zombie/z_idle1", "zombie/z_pain", "zombie/z_pain1", "zombie/z_fall",
    "zombie/z_gib", "zombie/z_shot1", "zombie/z_hit", "zombie/z_miss",
    // doors, secret doors, plats, trains, buttons, triggers
    "doors/drclos4", "doors/doormv1", "doors/hydro1", "doors/hydro2",
    "doors/stndr1", "doors/stndr2", "doors/ddoor1", "doors/ddoor2",
    "doors/basesec1", "doors/basesec2", "doors/latch2", "doors/winch2",
    "doors/airdoor1", "doors/airdoor2", "doors/medtry", "doors/meduse",
    "doors/runetry", "doors/runeuse", "doors/basetry", "doors/baseuse",
    "plats/plat1", "plats/plat2", "plats/medplat1", "plats/medplat2",
    "plats/train1", "plats/train2",
    "buttons/airbut1", "buttons/switch21", "buttons/switch02", "buttons/switch04",
    "misc/talk", "misc/secret", "misc/trigger1",
    // static sounds
    "ambience/fire1", "ambience/comp1", "ambience/drone6", "ambience/drip1",
    "ambience/swamp1", "ambience/swamp2", "ambience/fl_hum1", "ambience/buzz1",
};

static const std::vector<std::string> CREATURES = {"soldier", "dog", "player", "ogre",
                                                   "knight", "demon", "wizard", "zombie"};
const std::vector<std::string> MOVER_IDS = {"func_door", "func_door_secret", "func_plat", "func_button",
                                            "func_train"};
const std::vector<std::string> TRIGGER_IDS = {"trigger_once", "trigger_multiple", "trigger_secret",
                                              "trigger_counter", "trigger_teleport", "door_field", "plat_field"};

constexpr int SPAWNFLAG_NOT_MEDIUM = 512;

template <class T>
static int index_of(const std::vector<T> &v, const T &x)
{
    auto it = std::find(v.begin(), v.end(), x);
    return it == v.end() ? -1 : int(it - v.begin());
}
static bool contains(const std::vector<std::string> &v, const std::string &x) { return index_of(v, x) >= 0; }

int model_index(const std::string &name)
{
    for (size_t i = 0; i < MODELS.size(); i++)
        if (MODELS[i].name == name) return int(i);
    return -1;
}
int sound_index(const std::string &name) { return index_of(SOUNDS, name); }

static std::string model_path(const std::string &name)
{
    int i = model_index(name);
    check(i >= 0, "unknown model " + name);
    return MODELS[size_t(i)].path;
}

// ------------------------------------------------------------ headers

static std::string cname(std::string s)
{
    for (auto &c : s) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (c == '/' || c == '.') c = '_';
    }
    return s;
}

static std::string class_cname(std::string c)
{
    c = replace_all(c, "monster_", "");
    c = replace_all(c, "item_", "i_");
    c = replace_all(c, "weapon_", "w_");
    c = replace_all(c, "trigger_", "t_");
    c = replace_all(c, "info_", "");
    c = replace_all(c, "light_", "l_");
    c = replace_all(c, "ambient_", "amb_");
    return cname(c);
}

static std::string sound_cname(const std::string &s)
{
    size_t p = s.find('/');
    std::string folder = s.substr(0, p), name = s.substr(p + 1);
    if (contains(CREATURES, folder)) name = folder + "_" + name;
    return cname(name);
}

std::string ids_header()
{
    std::string f = "/* generated by tools/mkents.py --header: do not edit */\n";
    f += "#ifndef G_IDS_H\n#define G_IDS_H\n\n";
    f += "enum {\n";
    for (size_t i = 0; i < CLASSES.size(); i++)
        f += "    CL_" + class_cname(CLASSES[i]) + " = " + std::to_string(i) + ",\n";
    f += "    CL_NUM\n};\n\nenum {\n";
    for (size_t i = 0; i < MODELS.size(); i++)
        f += "    MDL_" + cname(MODELS[i].name) + " = " + std::to_string(i) + ",\n";
    f += "    MDL_NUM\n};\n\nenum {\n";
    for (size_t i = 0; i < SOUNDS.size(); i++)
        f += "    SND_" + sound_cname(SOUNDS[i]) + " = " + std::to_string(i) + ",\n";
    f += "    SND_NUM\n};\n\nenum {\n";
    for (size_t i = 0; i < MOVER_IDS.size(); i++)
        f += "    MV_" + cname(replace_all(MOVER_IDS[i], "func_", "")) + " = " + std::to_string(i) + ",\n";
    f += "    MV_NUM\n};\n\nenum {\n";
    for (size_t i = 0; i < TRIGGER_IDS.size(); i++)
        f += "    TR_" + cname(replace_all(TRIGGER_IDS[i], "trigger_", "")) + " = " + std::to_string(i) + ",\n";
    f += "    TR_NUM\n};\n\n#endif\n";
    return f;
}

static std::vector<std::string> mdl_frame_names(const Bytes &d)
{
    Reader r(d);
    int nskins = r.i32(48), sw = r.i32(52), sh = r.i32(56), nverts = r.i32(60), ntris = r.i32(64),
        nframes = r.i32(68);
    size_t o = 84;
    for (int i = 0; i < nskins; i++) {
        int t = r.i32(o);
        o += 4;
        if (t == 0)
            o += size_t(sw) * size_t(sh);
        else {
            int n = r.i32(o);
            o += 4 + 4 * size_t(n) + size_t(n) * size_t(sw) * size_t(sh);
        }
    }
    o += 12 * size_t(nverts) + 16 * size_t(ntris);
    std::vector<std::string> names;
    for (int i = 0; i < nframes; i++) {
        int t = r.i32(o);
        o += 4;
        if (t == 0) {
            names.push_back(r.name(o + 8, 16));
            o += 24 + 4 * size_t(nverts);
        } else {
            int n = r.i32(o);
            o += 4 + 8 + 4 * size_t(n);
            names.push_back(r.name(o + 8, 16));
            o += size_t(n) * (24 + 4 * size_t(nverts));
        }
    }
    return names;
}

std::string frames_header(const Pak &pak)
{
    std::string f = "/* generated by tools/mkents.py --header: the monsters' animations\n";
    f += " * (first frame and length, from the frame names in the .mdl files) */\n";
    f += "#ifndef G_FRAMES_H\n#define G_FRAMES_H\n\n";
    for (const auto &name : FRAME_MODELS) {
        auto names = mdl_frame_names(pak.get(model_path(name)));
        struct G {
            std::string base;
            int first, count;
        };
        std::vector<G> groups;
        for (size_t i = 0; i < names.size(); i++) {
            std::string base = names[i];
            while (!base.empty() && base.back() >= '0' && base.back() <= '9') base.pop_back();
            if (!groups.empty() && groups.back().base == base && groups.back().first + groups.back().count == int(i))
                groups.back().count++;
            else
                groups.push_back({base, int(i), 1});
        }
        std::set<std::string> seen;
        for (auto &g : groups) {
            std::string key = cname(name + "_" + g.base);
            if (seen.count(key)) continue;
            seen.insert(key);
            f += "#define FR_" + key + " " + std::to_string(g.first) + "\n#define FN_" + key + " " +
                 std::to_string(g.count) + "\n";
        }
        f += "\n";
    }
    f += "#endif\n";
    return f;
}

// ------------------------------------------------------------ names

int Names::operator()(const OptStr &n)
{
    if (!n || n->empty()) return 0;
    auto it = ids.find(*n);
    if (it == ids.end()) it = ids.emplace(*n, int(ids.size()) + 1).first;
    check(it->second < 256, "more than 255 target names");
    return it->second;
}

// ------------------------------------------------------------ what a level needs

struct ItemDeps {
    std::vector<std::string> models, sounds;
};
static const std::map<std::string, ItemDeps> &item_deps()
{
    static const std::map<std::string, ItemDeps> m = {
        {"item_health", {{"b_bh10", "b_bh25", "b_bh100"}, {"items/health1", "items/r_item1", "items/r_item2"}}},
        {"item_shells", {{"b_shell0", "b_shell1"}, {}}},
        {"item_spikes", {{"b_nail0", "b_nail1"}, {}}},
        {"item_rockets", {{"b_rock0", "b_rock1"}, {}}},
        {"item_armor1", {{"armor"}, {"items/armor1"}}},
        {"item_armor2", {{"armor"}, {"items/armor1"}}},
        {"item_armorInv", {{"armor"}, {"items/armor1"}}},
        {"weapon_supershotgun", {{"g_shot"}, {}}},
        {"weapon_nailgun", {{"g_nail"}, {}}},
        {"weapon_grenadelauncher", {{"g_rock"}, {}}},
        {"item_artifact_super_damage", {{"quaddama"}, {"items/damage", "items/damage2", "items/damage3"}}},
        {"item_artifact_envirosuit", {{"suit"}, {"items/suit", "items/suit2"}}},
        {"item_artifact_invisibility", {{"invisibl"}, {"items/inv1", "items/inv2", "items/inv3"}}},
        {"misc_explobox", {{"b_explob"}, {"weapons/r_exp3"}}},
        {"trap_spikeshooter", {{"spike"}, {"weapons/spike2"}}},
        {"light_torch_small_walltorch", {{"flame"}, {"ambience/fire1"}}},
        {"light_flame_large_yellow", {{"flame2"}, {"ambience/fire1"}}},
        {"light_flame_small_yellow", {{"flame2"}, {"ambience/fire1"}}},
        {"ambient_comp_hum", {{}, {"ambience/comp1"}}},
        {"ambient_drone", {{}, {"ambience/drone6"}}},
        {"ambient_drip", {{}, {"ambience/drip1"}}},
        {"ambient_swamp1", {{}, {"ambience/swamp1"}}},
        {"ambient_swamp2", {{}, {"ambience/swamp2"}}},
        {"light_fluoro", {{}, {"ambience/fl_hum1"}}},
        {"light_fluorospark", {{}, {"ambience/buzz1"}}},
        {"trigger_teleport", {{}, {}}},
    };
    return m;
}

struct MonsterDeps {
    std::vector<std::string> models;
    std::string folder;
};
static const std::map<std::string, MonsterDeps> &monster_deps()
{
    static const std::map<std::string, MonsterDeps> m = {
        {"monster_army", {{"soldier", "h_guard", "backpack"}, "soldier"}},
        {"monster_dog", {{"dog", "h_dog"}, "dog"}},
        {"monster_ogre", {{"ogre", "h_ogre", "grenade", "backpack"}, "ogre"}},
        {"monster_knight", {{"knight", "h_knight"}, "knight"}},
        {"monster_demon1", {{"demon", "h_demon"}, "demon"}},
        {"monster_wizard", {{"wizard", "h_wizard", "w_spike"}, "wizard"}},
        {"monster_zombie", {{"zombie", "h_zombie", "zom_gib"}, "zombie"}},
    };
    return m;
}

static int worldtype(const Entity &world) { return pyintf(world.get("worldtype", "0")); }

static std::vector<std::string> key_names(const Entity &world)
{
    switch (worldtype(world)) {
    case 0: return {"misc/medkey", "doors/medtry", "doors/meduse"};
    case 1: return {"misc/runekey", "doors/runetry", "doors/runeuse"};
    case 2: return {"misc/medkey", "doors/basetry", "doors/baseuse"};
    }
    fail("unknown worldtype " + std::to_string(worldtype(world)));
}

static std::vector<std::string> mover_sounds(const Mover &m, const Entity &world)
{
    int s = m.sounds;
    if (m.cls == "func_door") {
        std::vector<std::string> out;
        if (s == 1) out = {"doors/drclos4", "doors/doormv1"};
        if (s == 2) out = {"doors/hydro1", "doors/hydro2"};
        if (s == 3) out = {"doors/stndr1", "doors/stndr2"};
        if (s == 4) out = {"doors/ddoor1", "doors/ddoor2"};
        if (m.items) {
            auto k = key_names(world);
            out.push_back(k[1]);
            out.push_back(k[2]);
        }
        return out;
    }
    if (m.cls == "func_door_secret") {
        if (s == 1) return {"doors/latch2", "doors/winch2", "doors/drclos4"};
        if (s == 2) return {"doors/airdoor1", "doors/airdoor2"};
        if (s == 3) return {"doors/basesec1", "doors/basesec2"};
        return {};
    }
    if (m.cls == "func_plat")
        return s == 1 ? std::vector<std::string>{"plats/plat1", "plats/plat2"}
                      : std::vector<std::string>{"plats/medplat1", "plats/medplat2"};
    if (m.cls == "func_train")
        return s == 1 ? std::vector<std::string>{"plats/train1", "plats/train2"} : std::vector<std::string>{};
    static const char *btn[4] = {"buttons/airbut1", "buttons/switch21", "buttons/switch02", "buttons/switch04"};
    return {btn[s & 3]};
}

void level_needs(const std::vector<Entity> &ents, const std::vector<Mover> &movers, const Entity &world,
                 const std::vector<std::string> &extra_models, const std::vector<std::string> &extra_sounds,
                 std::set<std::string> &models, std::set<std::string> &sounds)
{
    models = {"gib1", "gib2", "gib3", "spike", "grenade", "backpack"};
    sounds.clear();
    for (auto &s : SOUNDS) {
        std::string folder = s.substr(0, s.find('/'));
        if (folder == "player" || folder == "weapons" || s == "misc/r_tele1" || s == "misc/r_tele2" ||
            s == "misc/r_tele3" || s == "misc/r_tele4" || s == "misc/r_tele5" || s == "misc/outwater" ||
            s == "misc/h2ohit1" || s == "misc/talk" || s == "misc/secret" || s == "misc/trigger1")
            sounds.insert(s);
    }
    for (auto &e : ents) {
        std::string cn = e.get("classname");
        if (!present(e)) continue;
        auto md = monster_deps().find(cn);
        auto id = item_deps().find(cn);
        if (md != monster_deps().end()) {
            models.insert(md->second.models.begin(), md->second.models.end());
            for (auto &s : SOUNDS)
                if (starts_with(s, md->second.folder + "/")) sounds.insert(s);
            if (cn == "monster_ogre") sounds.insert({"weapons/grenade", "weapons/bounce", "weapons/r_exp3"});
        } else if (id != item_deps().end()) {
            models.insert(id->second.models.begin(), id->second.models.end());
            sounds.insert(id->second.sounds.begin(), id->second.sounds.end());
        } else if (cn == "item_key1" || cn == "item_key2") {
            models.insert(cn == "item_key1" ? "w_s_key" : "w_g_key");
            sounds.insert(key_names(world)[0]);
        }
    }
    for (auto &m : movers)
        for (auto &s : mover_sounds(m, world)) sounds.insert(s);
    sounds.insert({"misc/talk", "misc/secret", "misc/trigger1"});
    models.insert(extra_models.begin(), extra_models.end());
    sounds.insert(extra_sounds.begin(), extra_sounds.end());
}

// ------------------------------------------------------------ entities

static Bytes entity_records(const std::vector<Entity> &ents, const Bsp &bsp, bool big, Names &names,
                            const Entity &world, int &count)
{
    Buf out(big);
    count = 0;
    for (auto &e : ents) {
        std::string cn = e.get("classname");
        int sf = pyintf(e.get("spawnflags", "0"));
        if (sf & SPAWNFLAG_NOT_MEDIUM) continue;
        int ci = index_of(CLASSES, cn);
        if (ci < 0) continue;
        int variant = 0;
        if (cn == "item_health" || cn == "item_shells" || cn == "item_spikes" || cn == "item_rockets")
            variant = sf & 3;           // health: 1 rotten, 2 mega; ammo: 1 big
        else if (cn == "item_key1" || cn == "item_key2")
            variant = worldtype(world);
        else if (cn == "trap_spikeshooter")
            variant = sf & 3;           // 1 super spike, 2 laser
        else if (cn == "monster_zombie")
            variant = sf & 1;           // crucified
        std::vector<int64_t> org = {0, 0, 0}, half = {0, 0, 0};
        if (e.has("origin")) {
            org.clear();
            for (auto &v : split_ws(e.get("origin"))) org.push_back(pyround(pyfloat(v)));
            check(org.size() == 3, "bad origin");
        }
        int angle = pyintf(e.get("angle", "0"));
        if (cn == "trigger_changelevel") {
            const BspModel &mo = bsp.models.at(size_t(std::stoi(e.get("model").substr(1))));
            for (int k = 0; k < 3; k++) {
                org[size_t(k)] = pyround((mo.mins[k] + mo.maxs[k]) / 2);
                half[size_t(k)] = pyround((mo.maxs[k] - mo.mins[k]) / 2);
            }
        } else if (cn == "info_intermission") {
            std::vector<double> ma;
            for (auto &v : split_ws(e.get("mangle", "0 0 0"))) ma.push_back(pyfloat(v));
            check(ma.size() >= 2, "bad mangle");
            angle = int(pyround(ma[1]));
            half = {pyround(ma[0]), 0, 0};
        }
        out.u8(ci);
        out.u8(variant);
        out.i16(angle);
        for (auto v : org) out.i16(v);
        for (auto v : half) out.i16(v);
        int tn = names(e.opt("targetname"));
        int tg = names(e.opt("target"));
        out.u8(tn);
        out.u8(tg);
        count++;
    }
    return out.data();
}

// ------------------------------------------------------------ models

static int tri_colour(const Bytes &skin, int sw, int sh, const int pts[3][2], const Palette &pal, int limit)
{
    int minx = std::min({pts[0][0], pts[1][0], pts[2][0]}), maxx = std::max({pts[0][0], pts[1][0], pts[2][0]});
    int miny = std::min({pts[0][1], pts[1][1], pts[2][1]}), maxy = std::max({pts[0][1], pts[1][1], pts[2][1]});
    int x0 = std::max(0, minx), x1 = std::min(sw - 1, maxx);
    int y0 = std::max(0, miny), y1 = std::min(sh - 1, maxy);
    double ax = pts[0][0], ay = pts[0][1], bx = pts[1][0], by = pts[1][1], cx = pts[2][0], cy = pts[2][1];
    // integer coordinates: the area is exact
    double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    int64_t acc[3] = {0, 0, 0};
    int n = 0;
    if (std::fabs(area) > 1e-6) {
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                double px = x + 0.5, py = y + 0.5;
                double w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
                double w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
                double w2 = 1 - w0 - w1;
                if (w0 >= -0.01 && w1 >= -0.01 && w2 >= -0.01) {
                    int c = skin.at(size_t(y) * size_t(sw) + size_t(x));
                    acc[0] += pal.rgb[3 * c];
                    acc[1] += pal.rgb[3 * c + 1];
                    acc[2] += pal.rgb[3 * c + 2];
                    n++;
                }
            }
    }
    if (n == 0) {   // tiny triangle: the texel under its centre
        int x = std::min(sw - 1, std::max(0, int(std::trunc(double(pts[0][0] + pts[1][0] + pts[2][0]) / 3))));
        int y = std::min(sh - 1, std::max(0, int(std::trunc(double(pts[0][1] + pts[1][1] + pts[2][1]) / 3))));
        int c = skin.at(size_t(y) * size_t(sw) + size_t(x));
        return pal.nearest(pal.rgb[3 * c], pal.rgb[3 * c + 1], pal.rgb[3 * c + 2], limit);
    }
    return pal.nearest(double(acc[0]) / n, double(acc[1]) / n, double(acc[2]) / n, limit);
}

FlatModel load_mdl_flat(const Bytes &d, const Palette &pal, bool fullbright)
{
    Reader r(d);
    check(d.size() >= 84 && std::memcmp(d.data(), "IDPO", 4) == 0 && r.i32(4) == 6, "not a Quake model");
    FlatModel md;
    for (int k = 0; k < 3; k++) {
        md.scale[k] = r.f32(8 + 4 * size_t(k));
        md.origin[k] = r.f32(20 + 4 * size_t(k));
    }
    md.radius = r.f32(32);
    int nskins = r.i32(48), sw = r.i32(52), sh = r.i32(56), nverts = r.i32(60), ntris = r.i32(64),
        nframes = r.i32(68);
    int flags = r.i32(76);
    size_t o = 84;
    std::vector<Bytes> skins;
    size_t ss = size_t(sw) * size_t(sh);
    for (int i = 0; i < nskins; i++) {
        int t = r.i32(o);
        o += 4;
        if (t == 0) {
            skins.push_back(r.slice(o, ss));
            o += ss;
        } else {
            int n = r.i32(o);
            o += 4 + 4 * size_t(n);
            skins.push_back(r.slice(o, ss));
            o += size_t(n) * ss;
        }
    }
    std::vector<std::array<int, 3>> st;
    for (int i = 0; i < nverts; i++, o += 12) st.push_back({r.i32(o), r.i32(o + 4), r.i32(o + 8)});
    std::vector<std::array<int, 4>> tris;
    for (int i = 0; i < ntris; i++, o += 16) tris.push_back({r.i32(o), r.i32(o + 4), r.i32(o + 8), r.i32(o + 12)});
    for (int i = 0; i < nframes; i++) {
        int t = r.i32(o);
        o += 4;
        if (t == 0) {
            o += 8 + 16;
            md.frames.push_back(r.slice(o, 4 * size_t(nverts)));
            o += 4 * size_t(nverts);
        } else {
            int n = r.i32(o);
            o += 4 + 8 + 4 * size_t(n);
            Bytes first;
            for (int k = 0; k < n; k++) {
                o += 8 + 16;
                if (k == 0) first = r.slice(o, 4 * size_t(nverts));
                o += 4 * size_t(nverts);
            }
            md.frames.push_back(first);
        }
    }
    for (auto &skin : skins) {
        Bytes cl;
        for (auto &t : tris) {
            int pts[3][2];
            for (int k = 0; k < 3; k++) {
                auto &v = st.at(size_t(t[size_t(k) + 1]));
                int s = v[1];
                if (v[0] && !t[0]) s += sw / 2;
                pts[k][0] = s;
                pts[k][1] = v[2];
            }
            // full bright models (flames) may use the bright end of the palette
            cl.push_back(uint8_t(tri_colour(skin, sw, sh, pts, pal, fullbright ? 256 : 224)));
        }
        md.colours.push_back(cl);
    }
    if (fullbright) flags |= 0x4000;
    md.flags = flags;
    md.nverts = nverts;
    for (auto &t : tris) md.tris.push_back({t[1], t[2], t[3]});
    return md;
}

static int nearest_normal(const Vec3 &n)
{
    double best = -2;
    int bi = 0;
    for (int i = 0; i < 162; i++) {
        double d = ANORMS[i][0] * n[0] + ANORMS[i][1] * n[1] + ANORMS[i][2] * n[2];
        if (d > best) {
            best = d;
            bi = i;
        }
    }
    return bi;
}

FlatModel load_bsp_model(const Bytes &d, const Palette &pal)
{
    Bsp m = Bsp::parse(d);
    std::vector<std::pair<Vec3, int>> pts_all;
    FlatModel md;
    Bytes cols;
    const BspModel &m0 = m.models.at(0);
    for (int fi = m0.firstface; fi < m0.firstface + m0.numfaces; fi++) {
        const BspFace &f = m.faces.at(size_t(fi));
        auto pts = m.face_polygon(f);
        const BspPlane &pl = m.planes.at(size_t(f.plane));
        Vec3 n = !f.side ? Vec3{pl.n[0], pl.n[1], pl.n[2]} : Vec3{-pl.n[0], -pl.n[1], -pl.n[2]};
        const BspTexture *t = m.face_texture(f);
        check(t != nullptr, "item box face without a texture");
        const Bytes &mip = t->mips[0];
        int64_t acc[3] = {0, 0, 0};
        for (uint8_t c : mip)
            for (int k = 0; k < 3; k++) acc[k] += pal.rgb[3 * c + k];
        double ln = double(mip.size());
        int col = pal.nearest(acc[0] / ln, acc[1] / ln, acc[2] / ln);
        int ni = nearest_normal(n);
        int base = int(pts_all.size());
        for (auto &p : pts) pts_all.push_back({p, ni});
        for (int k = 1; k + 1 < int(pts.size()); k++) {
            md.tris.push_back({base, base + k, base + k + 1});
            cols.push_back(uint8_t(col));
        }
    }
    double mins[3], maxs[3];
    for (int k = 0; k < 3; k++) {
        mins[k] = 1e300;
        maxs[k] = -1e300;
        for (auto &p : pts_all) {
            mins[k] = std::min(mins[k], p.first[size_t(k)]);
            maxs[k] = std::max(maxs[k], p.first[size_t(k)]);
        }
    }
    for (int k = 0; k < 3; k++) {
        md.scale[k] = std::max(1e-3, (maxs[k] - mins[k]) / 255.0);
        md.origin[k] = mins[k];
    }
    Bytes frame;
    for (auto &p : pts_all) {
        for (int k = 0; k < 3; k++) {
            int64_t v = pyround((p.first[size_t(k)] - mins[k]) / md.scale[k]);
            check(v >= 0 && v <= 255, "item box vertex out of range");
            frame.push_back(uint8_t(v));
        }
        frame.push_back(uint8_t(p.second));
    }
    double s = 0;
    for (int k = 0; k < 3; k++) {
        double v = std::max(std::fabs(mins[k]), std::fabs(maxs[k]));
        s += v * v;
    }
    md.radius = std::sqrt(s);
    md.flags = 0;
    md.nverts = int(pts_all.size());
    md.frames = {frame};
    md.colours = {cols};
    return md;
}

static int64_t fx(double v) { return pyround(v * 65536.0); }

Bytes build_model(const FlatModel &md, bool big)
{
    int nv = md.nverts, nt = int(md.tris.size()), ns = int(md.colours.size());
    int nf = md.logical_frames();
    bool mapped = !md.frame_map.empty();
    check(nv <= 256, "model with more than 256 vertices");
    const size_t hdr_size = 4 * 2 + 6 * 4 + 2 * 2 + 4 * 4;
    Bytes tris;
    for (auto &t : md.tris) {
        for (int k = 0; k < 3; k++) {
            check(t[size_t(k)] >= 0 && t[size_t(k)] < 256, "triangle vertex out of range");
            tris.push_back(uint8_t(t[size_t(k)]));
        }
        tris.push_back(0);
    }
    Bytes cols;
    for (auto &c : md.colours) cols.insert(cols.end(), c.begin(), c.end());
    while (cols.size() % 4) cols.push_back(0);
    Bytes frames;
    for (auto &f : md.frames) frames.insert(frames.end(), f.begin(), f.end());
    // per-frame bounding box of the byte vertices: tight screen rectangles
    Bytes bbox;
    for (int lf = 0; lf < nf; lf++) {
        const Bytes &fr = md.frames.at(size_t(mapped ? md.frame_map[size_t(lf)] : lf));
        for (int k = 0; k < 3; k++) {
            int lo = 255;
            for (int i = 0; i < nv; i++) lo = std::min<int>(lo, fr.at(size_t(4 * i + k)));
            bbox.push_back(uint8_t(lo));
        }
        for (int k = 0; k < 3; k++) {
            int hi = 0;
            for (int i = 0; i < nv; i++) hi = std::max<int>(hi, fr.at(size_t(4 * i + k)));
            bbox.push_back(uint8_t(hi));
        }
    }
    if (mapped) {
        for (int m : md.frame_map) {
            check(m >= 0 && m < int(md.frames.size()) && m < 256, "bad frame map");
            bbox.push_back(uint8_t(m));
        }
    }
    while (bbox.size() % 4) bbox.push_back(0);
    size_t t_ofs = hdr_size, c_ofs = t_ofs + tris.size(), f_ofs = c_ofs + cols.size(), b_ofs = f_ofs + frames.size();
    Buf h(big);
    h.u16(nv);
    h.u16(nt);
    h.u16(nf);
    h.u16(ns);
    for (double v : md.scale) h.i32(fx(v));
    for (double v : md.origin) h.i32(fx(v));
    h.i16(pyceil(md.radius));
    h.i16(md.flags | (mapped ? MF_FRAMEMAP : 0));
    h.u32(int64_t(t_ofs));
    h.u32(int64_t(c_ofs));
    h.u32(int64_t(f_ofs));
    h.u32(int64_t(b_ofs));
    h.bytes(tris);
    h.bytes(cols);
    h.bytes(frames);
    h.bytes(bbox);
    return h.data();
}

FlatModel load_model(const Pak &pak, const std::string &name, const Palette &pal)
{
    std::string path = model_path(name);
    const Bytes &d = pak.get(path);
    if (ends_with(path, ".mdl")) return load_mdl_flat(d, pal, contains(FULLBRIGHT, name));
    return load_bsp_model(d, pal);
}

// ------------------------------------------------------------ sounds

Sound load_wav(const Bytes &d)
{
    check(d.size() >= 12 && std::memcmp(d.data(), "RIFF", 4) == 0 && std::memcmp(d.data() + 8, "WAVE", 4) == 0,
          "not a .wav file");
    Reader r(d);
    size_t o = 12;
    bool have_fmt = false;
    int tag = 0, ch = 0, rate = 0, bits = 0;
    Bytes data;
    bool have_data = false;
    int64_t loop = -1;
    int64_t loop_len = 0;
    while (o + 8 <= d.size()) {
        std::string cid(reinterpret_cast<const char *>(d.data() + o), 4);
        uint32_t size = r.u32(o + 4);
        Bytes body = r.slice(o + 8, size);
        if (cid == "fmt ") {
            Reader b(body);
            tag = b.u16(0);
            ch = b.u16(2);
            rate = int(b.u32(4));
            bits = b.u16(14);
            have_fmt = true;
        } else if (cid == "data") {
            data = body;
            have_data = true;
        } else if (cid == "cue " && body.size() >= 28) {
            loop = Reader(body).i32(24);
        } else if (cid == "LIST" && loop >= 0 && body.size() >= 24 && std::memcmp(body.data() + 20, "mark", 4) == 0) {
            loop_len = Reader(body).i32(16);
        }
        o += 8 + size_t(size) + (size & 1);
    }
    check(have_fmt && have_data, "a .wav without fmt or data");
    check(tag == 1 && ch == 1, "only mono PCM .wav files are supported");
    if (bits == 16) {
        Bytes b8;
        for (size_t i = 0; i + 1 < data.size() || i < data.size(); i += 2) {
            // (as in Python, an odd last byte would fail: struct needs 2)
            check(i + 1 < data.size(), "odd-sized 16-bit data");
            int v = int16_t(data[i] | data[i + 1] << 8);
            b8.push_back(uint8_t(((v >> 8) + 128) & 255));
        }
        data = b8;
    }
    if (loop >= 0 && loop_len) {
        int64_t end = loop + loop_len;
        if (end < 0) end = std::max<int64_t>(0, int64_t(data.size()) + end);
        if (size_t(end) < data.size()) data.resize(size_t(end));
    }
    if (rate != 11025) {   // nearest-sample resample
        int64_t n = int64_t(std::trunc(double(data.size()) * 11025 / rate));
        Bytes out;
        for (int64_t i = 0; i < n; i++) out.push_back(data.at(size_t(std::trunc(double(i) * rate / 11025))));
        data = out;
        if (loop >= 0) loop = int64_t(std::trunc(double(loop) * 11025 / rate));
    }
    Sound s;
    s.data = data;
    s.loop = int(loop);
    s.rate = 11025;
    return s;
}

// ------------------------------------------------------------ movers

static int64_t i16v(double v)
{
    int64_t r = pyround(v);
    check(r >= -32768 && r < 32768, "value out of 16-bit range: " + std::to_string(v));
    return r;
}

static int64_t wait10(double w) { return w < 0 ? -1 : i16v(w * 10); }

// math.degrees, as CPython computes it
static double degrees(double x) { return x * (180.0 / 3.141592653589793); }

static std::map<std::string, Lump> add_movers(Buf &blob, const std::vector<Entity> &ents, const Bsp &bsp,
                                              const std::vector<Mover> &movers, Names &names, const Entity &world,
                                              std::string *log)
{
    bool big = blob.big();
    auto trigs = build_triggers(ents, bsp, movers);
    // string 1 is always the level's name (worldspawn's message)
    std::string wm = world.get("message", "");
    if (wm.empty()) wm = " ";
    std::vector<std::string> msgs = {"", strip(replace_all(wm, "\n", " "))};
    auto msg = [&](const OptStr &t0) -> int {
        if (!t0 || t0->empty()) return 0;
        std::string t = strip(replace_all(*t0, "\n", " "));
        int i = index_of(msgs, t);
        if (i < 0) {
            msgs.push_back(t);
            i = int(msgs.size()) - 1;
        }
        return i;
    };
    std::map<std::string, Lump> out;
    Buf keys(big), recs(big);
    int nkeys = 0;
    for (auto &m : movers) {
        int startpos = (m.cls == "func_plat" && !(m.targetname && !m.targetname->empty())) ? 1 : 0;
        check(m.pos.size() <= 3, "mover with more than 3 positions");
        std::vector<Vec3> pos = m.pos;
        while (pos.size() < 3) pos.push_back({0, 0, 0});
        // qbsp stores model bounds 1 unit inside the brush; SV_LinkEdict
        // adds the unit back, which gives the brush's own bounds
        int flags = m.flags & 0xFFFF;
        if (m.cls == "func_door" && m.items)
            flags = (flags & ~0x18) | (m.items == 2 ? 0x8000 : 0x4000);   // key door
        recs.u8(index_of(MOVER_IDS, m.cls));
        recs.u8(m.sounds);
        int a = names(m.targetname);
        int b = names(m.target);
        int c = msg(m.message);
        recs.u8(a);
        recs.u8(b);
        recs.u8(c);
        recs.u8(m.group);
        recs.u8(int64_t(m.keys.size()));
        recs.u8(startpos);
        recs.u16(flags);
        recs.u16(nkeys);
        recs.i16(i16v(m.speed));
        recs.i16(wait10(m.wait));
        recs.i16(i16v(m.dmg));
        recs.i16(i16v(m.health));
        recs.i16(m.hull1);
        recs.i16(m.hull2);
        for (int k = 0; k < 3; k++) recs.i16(i16v(m.mins[size_t(k)] - 1));
        for (int k = 0; k < 3; k++) recs.i16(i16v(m.maxs[size_t(k)] + 1));
        for (auto &p : pos)
            for (double v : p) recs.i16(i16v(v));
        recs.u8(int64_t(m.pos.size()));
        recs.u8(0);
        for (auto &k : m.keys) {
            for (double v : k) keys.i16(i16v(v));
            nkeys++;
        }
    }
    out["movers"] = {blob.add(recs), movers.size()};
    out["mkeys"] = {blob.add(keys), size_t(nkeys)};
    Buf tr(big);
    for (auto &t : trigs) {
        int64_t facing = -1000;
        if (t.facing) facing = i16v(degrees(std::atan2((*t.facing)[1], (*t.facing)[0])));
        Vec3 dest = t.has_dest ? t.dest : Vec3{0, 0, 0};
        tr.u8(index_of(TRIGGER_IDS, t.cls));
        tr.u8(t.sounds);
        int a = names(t.targetname);
        int b = names(t.target);
        int c = names(t.killtarget);
        int d = msg(t.message);
        tr.u8(a);
        tr.u8(b);
        tr.u8(c);
        tr.u8(d);
        tr.u16(t.flags & 0xFFFF);
        tr.i16(wait10(t.wait));
        tr.i16(i16v(t.health));
        tr.i16(i16v(t.count));
        tr.i16(t.owner);
        for (int k = 0; k < 3; k++) tr.i16(i16v(t.mins[size_t(k)] - 1));
        for (int k = 0; k < 3; k++) tr.i16(i16v(t.maxs[size_t(k)] + 1));
        tr.i16(facing);
        for (double v : dest) tr.i16(i16v(v));
        tr.i16(i16v(t.has_dest ? t.dest_yaw : 0));
        tr.u16(0);
    }
    out["triggers"] = {blob.add(tr), trigs.size()};
    // messages: a table of offsets (from the blob start), then the text
    size_t base = blob.add(Bytes(4 * msgs.size(), 0));
    std::vector<size_t> offs;
    for (auto &t : msgs) {
        Bytes b(t.begin(), t.end());
        b.push_back(0);
        offs.push_back(blob.add(b));
    }
    for (size_t i = 0; i < offs.size(); i++) blob.put_at(base + 4 * i, offs[i], 4);
    out["strings"] = {base, msgs.size()};
    if (log)
        *log += "movers: " + std::to_string(movers.size()) + " (" + std::to_string(nkeys) +
                " keyframes), triggers: " + std::to_string(trigs.size()) + ", names: " +
                std::to_string(names.ids.size()) + ", messages: " + std::to_string(msgs.size() - 1) + "\n";
    return out;
}

std::map<std::string, Lump> add_game_data(Buf &blob, const Pak &pak, const Palette &pal,
                                          const std::vector<Entity> &ents, const Bsp &bsp,
                                          const std::vector<Mover> &movers, const Entity &world,
                                          const std::vector<std::string> &extra_models,
                                          const std::vector<std::string> &extra_sounds,
                                          const GameDataOptions &opt, std::string *log)
{
    bool big = blob.big();
    std::map<std::string, Lump> out;
    Names names;
    std::set<std::string> need_m, need_s;
    level_needs(ents, movers, world, extra_models, extra_sounds, need_m, need_s);
    int n = 0;
    Bytes eb = entity_records(ents, bsp, big, names, world, n);
    out["ents"] = {blob.add(eb), size_t(n)};

    size_t base = blob.add(Bytes(4 * MODELS.size(), 0));
    std::vector<size_t> offs;
    size_t msize = 0;
    for (auto &mn : MODELS) {
        if (!need_m.count(mn.name)) {
            offs.push_back(0);
            continue;
        }
        auto it = opt.models.find(mn.name);
        Bytes b = build_model(it != opt.models.end() ? it->second : load_model(pak, mn.name, pal), big);
        blob.align(4);
        offs.push_back(blob.add(b));
        msize += b.size();
    }
    for (size_t i = 0; i < offs.size(); i++) blob.put_at(base + 4 * i, offs[i], 4);
    out["models"] = {base, MODELS.size()};

    Buf an(big);
    for (auto &a : ANORMS)
        for (int k = 0; k < 3; k++) an.i16(pyround(a[k] * 16384));
    out["anorms"] = {blob.add(an), 162};

    // models drawn in their place beyond a distance
    if (!opt.lods.empty()) {
        Buf lt(big);
        std::vector<std::pair<size_t, int>> lods;
        for (auto &mn : MODELS) {
            auto it = opt.lods.find(mn.name);
            if (it == opt.lods.end() || !need_m.count(mn.name)) {
                lods.push_back({0, 0});
                continue;
            }
            Bytes b = build_model(it->second.model, big);
            blob.align(4);
            lods.push_back({blob.add(b), it->second.dist});
            msize += b.size();
        }
        for (auto &l : lods) {
            lt.u32(int64_t(l.first));
            lt.u16(std::clamp(l.second, 0, 65535));
            lt.u16(0);
        }
        out["x:mdllod"] = {blob.add(lt), MODELS.size()};
    }

    base = blob.add(Bytes(12 * SOUNDS.size(), 0));
    Buf ent(big);
    size_t ssize = 0;
    for (auto &s : SOUNDS) {
        if (!need_s.count(s) || opt.drop_sounds.count(s)) {
            ent.u32(0);
            ent.u32(0);
            ent.i32(-1);
            continue;
        }
        auto bit = opt.bank.find(s);
        if (bit != opt.bank.end()) {
            // in the shared bank, mapped in after the level (offset bit 31)
            ent.u32(int64_t(0x80000000u | bit->second.ofs));
            ent.u32(bit->second.len);
            ent.i32(bit->second.loop);
            continue;
        }
        auto it = opt.sounds.find(s);
        Sound snd = it != opt.sounds.end() ? it->second : load_wav(pak.get("sound/" + s + ".wav"));
        auto eit = opt.sound_edits.find(s);
        if (eit != opt.sound_edits.end() && !eit->second.plain()) snd = apply_sound_edit(snd, eit->second);
        Bytes stored = encode_sound(snd, opt.sound_format);
        size_t o = blob.add(stored);
        ssize += stored.size();
        ent.u32(int64_t(o));
        ent.u32(int64_t(sound_length_field(snd, opt.sound_format)));
        ent.i32(snd.loop);
    }
    std::copy(ent.data().begin(), ent.data().end(), blob.data().begin() + std::ptrdiff_t(base));
    out["sounds"] = {base, SOUNDS.size()};
    auto mv = add_movers(blob, ents, bsp, movers, names, world, log);
    for (auto &kv : mv) out[kv.first] = kv.second;
    if (log)
        *log += "game data: " + std::to_string(n) + " entities, " + std::to_string(need_m.size()) + " models " +
                std::to_string(msize / 1024) + " KB, " + std::to_string(need_s.size()) + " sounds " +
                std::to_string(ssize / 1024) + " KB\n";
    return out;
}

}  // namespace q32x
