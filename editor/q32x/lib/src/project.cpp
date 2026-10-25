#include "q32x/project.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <filesystem>
#include <set>

#include "q32x/demo.h"
#include "q32x/image.h"
#include "q32x/models.h"
#include "q32x/texreduce.h"

namespace q32x {

namespace fs = std::filesystem;

static fs::path u8path(const std::string &s) { return fs::path(std::u8string(s.begin(), s.end())); }
static std::string pathstr(const fs::path &p)
{
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

bool glob_match(const std::string &p, const std::string &s)
{
    size_t pi = 0, si = 0, star = std::string::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && p[pi] == '*') {   // (before the literal test: liquids' names start with '*')
            star = pi++;
            mark = si;
        } else if (pi < p.size() && (p[pi] == '?' || p[pi] == s[si])) {
            pi++;
            si++;
        } else if (star != std::string::npos) {
            pi = star + 1;
            si = ++mark;
        } else
            return false;
    }
    while (pi < p.size() && p[pi] == '*') pi++;
    return pi == p.size();
}

std::string to_hex(const Bytes &b)
{
    static const char *h = "0123456789abcdef";
    std::string s;
    for (auto c : b) {
        s += h[c >> 4];
        s += h[c & 15];
    }
    return s;
}

Bytes from_hex(const std::string &s)
{
    Bytes b;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        fail("bad hex data in the project");
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2) b.push_back(uint8_t(v(s[i]) << 4 | v(s[i + 1])));
    return b;
}

// ------------------------------------------------------------ JSON

static Json ints(const std::vector<int> &v)
{
    Json a = Json::array();
    for (int x : v) a.push(x);
    return a;
}
static std::vector<int> to_ints(const Json &j)
{
    std::vector<int> v;
    for (auto &x : j.items()) v.push_back(x.as_int());
    return v;
}

static Json edits_json(const LevelEdits &e)
{
    Json j = Json::object();
    if (!e.faces.empty()) {
        Json f = Json::object();
        for (auto &kv : e.faces) {
            Json o = Json::object();
            if (kv.second.colour >= 0) o.set("colour", kv.second.colour);
            if (kv.second.exact) o.set("exact", true);
            if (kv.second.lock) o.set("lock", true);
            if (kv.second.remove) o.set("remove", true);
            if (kv.second.no_split) o.set("no_split", true);
            if (kv.second.band >= 0) o.set("band", kv.second.band);
            f.set(std::to_string(kv.first), o);
        }
        j.set("faces", f);
    }
    if (!e.remaps.empty()) {
        Json a = Json::array();
        for (auto &r : e.remaps) {
            Json o = Json::object();
            std::vector<int> m(r.map.begin(), r.map.end());
            o.set("map", ints(m));
            if (!r.leafs.empty()) o.set("leafs", ints(r.leafs));
            a.push(o);
        }
        j.set("remaps", a);
    }
    if (!e.colour_rules.empty()) {
        Json a = Json::array();
        for (auto &r : e.colour_rules) {
            Json o = Json::object();
            Json m = Json::array();
            for (auto &g : r.match) m.push(g);
            o.set("match", m);
            o.set("surface", r.surface);
            o.set("colour", r.colour);
            a.push(o);
        }
        j.set("colour_rules", a);
    }
    {
        Json l = Json::object();
        l.set("enabled", e.light.enabled);
        l.set("bands", e.light.bands);
        l.set("min_area", e.light.min_area);
        l.set("max_pieces", e.light.max_pieces);
        l.set("budget", e.light.budget);
        l.set("contrast", e.light.contrast);
        if (!e.light.off_leafs.empty()) l.set("off_leafs", ints(e.light.off_leafs));
        if (e.light.hard_shadows) {
            l.set("hard_shadows", true);
            l.set("lit_row", e.light.lit_row);
            l.set("shadow_step", e.light.shadow_step);
        }
        j.set("light_to_polygons", l);
    }
    {
        Json r = Json::object();
        r.set("merge_textured", e.reduce.merge_textured);
        r.set("max_ext", e.reduce.max_ext);
        r.set("min_area", e.reduce.min_area);
        r.set("fix_tjunctions", e.reduce.fix_tjunctions);
        j.set("reduce", r);
    }
    j.set("vertex_order", e.vertex_order);
    j.set("flat_only", e.flat_only);
    j.set("light_grid", e.light_grid);
    j.set("doors_block", e.doors_block);
    if (e.simple_collision) j.set("simple_collision", true);
    if (!e.door_exclude.empty()) j.set("door_exclude", ints(std::vector<int>(e.door_exclude.begin(), e.door_exclude.end())));
    if (!e.areas.empty()) {
        Json a = Json::array();
        for (auto &ar : e.areas) {
            Json o = Json::object();
            o.set("name", ar.name);
            o.set("far", ar.far);
            o.set("leafs", ints(ar.leafs));
            a.push(o);
        }
        j.set("draw_distance_areas", a);
    }
    if (!e.mover_models.empty())
        j.set("movers_as_models", ints(std::vector<int>(e.mover_models.begin(), e.mover_models.end())));
    if (e.flatpal.enabled || e.flatpal.rgb != std::array<uint8_t, 768>{}) {
        const FlatPalOptions &f = e.flatpal;
        Json o = Json::object();
        o.set("enabled", f.enabled);
        o.set("colours", to_hex(Bytes(f.rgb.begin(), f.rgb.end())));
        if (!f.source.empty()) o.set("source", f.source);
        o.set("vividness", f.vividness);
        o.set("variety", f.variety);
        o.set("special_kinds", f.special_kinds);
        o.set("levels", f.levels);
        o.set("shade", f.shade);
        o.set("occlusion", f.occlusion);
        o.set("reach", f.reach);
        o.set("rays", f.rays);
        o.set("up_bias", f.up_bias);
        o.set("ambient", f.ambient);
        o.set("lights", f.lights);
        o.set("lightmaps", f.lightmaps);
        o.set("highlight", f.highlight);
        j.set("flat_palette", o);
    }
    return j;
}

static LevelEdits edits_from(const Json &j)
{
    LevelEdits e;
    for (auto &kv : j["faces"].members()) {
        FaceEdit f;
        const Json &o = kv.second;
        f.colour = o["colour"].as_int(-1);
        f.exact = o["exact"].as_bool();
        f.lock = o["lock"].as_bool();
        f.remove = o["remove"].as_bool();
        f.no_split = o["no_split"].as_bool();
        f.band = o["band"].as_int(-1);
        e.faces[std::stoi(kv.first)] = f;
    }
    for (auto &r : j["remaps"].items()) {
        PaletteRemap pr = PaletteRemap::identity();
        auto m = to_ints(r["map"]);
        for (size_t i = 0; i < m.size() && i < 256; i++) pr.map[i] = uint8_t(std::clamp(m[i], 0, 255));
        pr.leafs = to_ints(r["leafs"]);
        e.remaps.push_back(pr);
    }
    for (auto &r : j["colour_rules"].items()) {
        ColourRule cr;
        for (auto &g : r["match"].items()) cr.match.push_back(g.as_string(""));
        cr.surface = r["surface"].as_int(0);
        cr.colour = r["colour"].as_int(0);
        e.colour_rules.push_back(cr);
    }
    const Json &l = j["light_to_polygons"];
    e.light.enabled = l["enabled"].as_bool();
    e.light.bands = l["bands"].as_int(2);
    e.light.min_area = l["min_area"].as_number(2048);
    e.light.max_pieces = l["max_pieces"].as_int(12);
    e.light.budget = l["budget"].as_int(0);
    e.light.contrast = l["contrast"].as_number(1.0);
    e.light.off_leafs = to_ints(l["off_leafs"]);
    e.light.hard_shadows = l["hard_shadows"].as_bool();
    e.light.lit_row = l["lit_row"].as_int(32);
    e.light.shadow_step = l["shadow_step"].as_int(12);
    const Json &r = j["reduce"];
    e.reduce.merge_textured = r["merge_textured"].as_bool();
    e.reduce.max_ext = r["max_ext"].as_int(256);
    e.reduce.min_area = r["min_area"].as_number(0);
    e.reduce.fix_tjunctions = r["fix_tjunctions"].as_bool(true);
    e.vertex_order = j["vertex_order"].as_bool();
    e.flat_only = j["flat_only"].as_bool();
    e.light_grid = j["light_grid"].as_int(0);
    e.doors_block = j["doors_block"].as_bool();
    e.simple_collision = j["simple_collision"].as_bool();
    for (int d : to_ints(j["door_exclude"])) e.door_exclude.insert(d);
    for (auto &a : j["draw_distance_areas"].items()) {
        DrawArea ar;
        ar.name = a["name"].as_string("");
        ar.far = a["far"].as_int(0);
        ar.leafs = to_ints(a["leafs"]);
        e.areas.push_back(ar);
    }
    for (int m : to_ints(j["movers_as_models"])) e.mover_models.insert(m);
    const Json &fp = j["flat_palette"];
    {
        FlatPalOptions &f = e.flatpal;
        const FlatPalOptions d;
        f.enabled = fp["enabled"].as_bool();
        Bytes c = from_hex(fp["colours"].as_string(""));
        for (size_t i = 0; i < c.size() && i < 768; i++) f.rgb[i] = c[i];
        f.source = fp["source"].as_string("");
        f.vividness = fp["vividness"].as_number(d.vividness);
        f.variety = fp["variety"].as_number(d.variety);
        f.special_kinds = fp["special_kinds"].as_bool(d.special_kinds);
        f.levels = fp["levels"].as_int(d.levels);
        f.shade = fp["shade"].as_bool(d.shade);
        f.occlusion = fp["occlusion"].as_number(d.occlusion);
        f.reach = fp["reach"].as_number(d.reach);
        f.rays = fp["rays"].as_int(d.rays);
        f.up_bias = fp["up_bias"].as_number(d.up_bias);
        f.ambient = fp["ambient"].as_number(d.ambient);
        f.lights = fp["lights"].as_number(d.lights);
        f.lightmaps = fp["lightmaps"].as_number(d.lightmaps);
        f.highlight = fp["highlight"].as_number(d.highlight);
    }
    return e;
}

Json Project::to_json() const
{
    Json j = Json::object();
    j.set("format", 1);
    j.set("pak", pak_path);
    if (!program_path.empty()) j.set("program", program_path);
    j.set("version", version);
    j.set("rom_name", rom_name);
    Json lv = Json::array();
    for (auto &l : levels) {
        Json o = Json::object();
        o.set("name", l.name);
        if (!l.bsp.empty()) o.set("bsp", l.bsp);
        if (!l.file.empty()) o.set("file", l.file);
        if (!l.demo.empty()) o.set("demo", l.demo);
        o.set("flat_tol", l.flat_tol);
        o.set("edits", edits_json(l.edits));
        lv.push(o);
    }
    j.set("levels", lv);
    Json tr = Json::array();
    for (auto &r : texture_rules) {
        Json o = Json::object();
        Json m = Json::array();
        for (auto &p : r.match) m.push(p);
        o.set("match", m);
        if (!r.image.empty()) o.set("image", r.image);
        if (!r.pixels.empty()) {
            o.set("pixels", to_hex(r.pixels));
            o.set("pixels_w", r.pixels_w);
            o.set("pixels_h", r.pixels_h);
        }
        o.set("reduce", r.reduce);
        o.set("min_size", r.min_size);
        o.set("tile", r.tile);
        if (!r.levels.empty()) {
            Json l = Json::array();
            for (auto &n : r.levels) l.push(n);
            o.set("levels", l);
        }
        tr.push(o);
    }
    j.set("texture_rules", tr);
    if (!reduced.empty()) {
        Json o = Json::object();
        for (auto &kv : reduced) {
            Json t = Json::object();
            t.set("min_mip", kv.second.min_mip);
            t.set("four_bit", kv.second.four_bit);
            t.set("pal", ints(std::vector<int>(kv.second.pal.begin(), kv.second.pal.end())));
            t.set("pixels", to_hex(kv.second.pixels));
            o.set(kv.first, t);
        }
        j.set("reduced_textures", o);
    }
    j.set("shared_bank", shared_bank);
    if (!sounds.empty()) {
        Json o = Json::object();
        for (auto &kv : sounds) {
            Json s = Json::object();
            if (kv.second.edit.gain != 1.0) s.set("gain", kv.second.edit.gain);
            if (kv.second.edit.trim) s.set("trim", true);
            if (kv.second.edit.threshold != 2) s.set("threshold", kv.second.edit.threshold);
            if (!kv.second.file.empty()) s.set("file", kv.second.file);
            if (kv.second.bank >= 0) s.set("bank", kv.second.bank);
            if (kv.second.drop) s.set("drop", true);
            o.set(kv.first, s);
        }
        j.set("sounds", o);
    }
    if (!models.empty()) {
        Json o = Json::object();
        for (auto &kv : models) {
            Json m = Json::object();
            m.set("frame_step", kv.second.frame_step);
            m.set("simplify", kv.second.simplify);
            m.set("lod_ratio", kv.second.lod_ratio);
            m.set("lod_dist", kv.second.lod_dist);
            o.set(kv.first, m);
        }
        j.set("models", o);
    }
    Json m = Json::object();
    m.set("weapon_size", ints(std::vector<int>(md.weapon_size.begin(), md.weapon_size.end())));
    m.set("compress", md.compress);
    if (md.flat_weapons) m.set("flat_weapons", true);
    m.set("light_scale", md.light_scale);
    m.set("dither", md.dither);
    if (md.border) m.set("border", to_hex(*md.border));
    if (md.font) m.set("font", to_hex(*md.font));
    if (!md.edited.empty()) {
        Json sets = Json::array();
        for (auto &kv : md.edited) {
            const MdWeaponSet &s = kv.second;
            Json o = Json::object();
            o.set("weapon", s.weapon);
            o.set("size", s.size);
            o.set("pal", ints(std::vector<int>(s.pal.begin(), s.pal.end())));
            Json fr = Json::array();
            for (auto &f : s.frames) fr.push(to_hex(f));
            o.set("frames", fr);
            Json bl = Json::array();
            for (auto &fb : s.blocks) {
                Json b2 = Json::array();
                for (auto &b : fb) b2.push(ints({b.y, b.x, b.rows, b.cols, b.pal}));
                bl.push(b2);
            }
            o.set("blocks", bl);
            sets.push(o);
        }
        m.set("edited_sets", sets);
    }
    j.set("mega_drive", m);
    return j;
}

Project Project::from_json(const Json &j, const std::string &path)
{
    Project p;
    p.path = path;
    check(j["format"].as_int(0) == 1, "not a Quake 32X project (or a newer format)");
    p.pak_path = j["pak"].as_string("");
    p.program_path = j["program"].as_string("");
    p.version = j["version"].as_string("0.7.0");
    p.rom_name = j["rom_name"].as_string("quake32x");
    for (auto &o : j["levels"].items()) {
        LevelProject l;
        l.name = o["name"].as_string("");
        l.bsp = o["bsp"].as_string("");
        l.file = o["file"].as_string("");
        l.demo = o["demo"].as_string("");
        l.flat_tol = o["flat_tol"].as_int(4);
        l.edits = edits_from(o["edits"]);
        p.levels.push_back(l);
    }
    for (auto &o : j["texture_rules"].items()) {
        TextureRule r;
        for (auto &m : o["match"].items()) r.match.push_back(m.as_string(""));
        r.image = o["image"].as_string("");
        r.pixels = from_hex(o["pixels"].as_string(""));
        r.pixels_w = o["pixels_w"].as_int(0);
        r.pixels_h = o["pixels_h"].as_int(0);
        r.reduce = o["reduce"].as_bool(true);
        r.min_size = o["min_size"].as_int(8);
        r.tile = o["tile"].as_bool(false);
        for (auto &n : o["levels"].items()) r.levels.push_back(n.as_string(""));
        p.texture_rules.push_back(r);
    }
    for (auto &kv : j["reduced_textures"].members()) {
        ReducedTexture t;
        t.min_mip = kv.second["min_mip"].as_int(2);
        t.four_bit = kv.second["four_bit"].as_bool(true);
        auto pal = to_ints(kv.second["pal"]);
        for (size_t i = 0; i < pal.size() && i < 16; i++) t.pal[i] = uint8_t(pal[i]);
        t.pixels = from_hex(kv.second["pixels"].as_string(""));
        p.reduced[kv.first] = t;
    }
    p.shared_bank = j["shared_bank"].as_bool();
    for (auto &kv : j["sounds"].members()) {
        SoundProject s;
        s.edit.gain = kv.second["gain"].as_number(1.0);
        s.edit.trim = kv.second["trim"].as_bool();
        s.edit.threshold = kv.second["threshold"].as_int(2);
        s.file = kv.second["file"].as_string("");
        s.bank = kv.second["bank"].as_int(-1);
        s.drop = kv.second["drop"].as_bool();
        p.sounds[kv.first] = s;
    }
    for (auto &kv : j["models"].members()) {
        ModelProject m;
        m.frame_step = kv.second["frame_step"].as_int(1);
        m.simplify = kv.second["simplify"].as_number(1.0);
        m.lod_ratio = kv.second["lod_ratio"].as_number(0);
        m.lod_dist = kv.second["lod_dist"].as_int(512);
        p.models[kv.first] = m;
    }
    const Json &m = j["mega_drive"];
    auto ws = to_ints(m["weapon_size"]);
    for (size_t i = 0; i < ws.size() && i < 6; i++) p.md.weapon_size[i] = ws[i];
    p.md.compress = m["compress"].as_bool();
    p.md.flat_weapons = m["flat_weapons"].as_bool();
    p.md.light_scale = m["light_scale"].as_number(1.3);
    p.md.dither = m["dither"].as_number(0.6);
    if (m.has("border")) p.md.border = from_hex(m["border"].as_string(""));
    if (m.has("font")) p.md.font = from_hex(m["font"].as_string(""));
    for (auto &o : m["edited_sets"].items()) {
        MdWeaponSet s;
        s.weapon = o["weapon"].as_int();
        s.size = std::clamp(o["size"].as_int(), 0, 5);
        s.vw = MD_SIZES[size_t(s.size)][0];
        s.vh = MD_SIZES[size_t(s.size)][1];
        auto pal = to_ints(o["pal"]);
        for (size_t i = 0; i < pal.size() && i < 48; i++) s.pal[i] = uint16_t(pal[i]);
        for (auto &f : o["frames"].items()) s.frames.push_back(from_hex(f.as_string("")));
        for (auto &fb : o["blocks"].items()) {
            std::vector<MdBlock> bs;
            for (auto &b : fb.items()) {
                auto v = to_ints(b);
                if (v.size() == 5) bs.push_back({v[0], v[1], v[2], v[3], v[4]});
            }
            s.blocks.push_back(bs);
        }
        p.md.edited[{s.weapon, s.size}] = s;
    }
    return p;
}

Project Project::load(const std::string &path)
{
    Bytes b = read_file(path);
    return from_json(Json::parse(std::string(b.begin(), b.end())), path);
}

void Project::save(const std::string &p)
{
    path = p;
    std::string s = to_json().dump(2);
    write_file(p, Bytes(s.begin(), s.end()));
}

Project Project::quake_default(const std::string &pak_path)
{
    Project p;
    p.pak_path = pak_path;
    p.levels.push_back({"e1m1", "maps/e1m1.bsp", "", "", 4, {}});
    p.levels.push_back({"e1m2", "maps/e1m2.bsp", "", "", 4, {}});
    p.levels.push_back({"e1m3", "maps/e1m3.bsp", "", "demo1.dem", 4, {}});
    return p;
}

std::string Project::resolve(const std::string &f) const
{
    if (f.empty()) return f;
    fs::path p = u8path(f);
    if (p.is_absolute() || path.empty()) return f;
    return pathstr(u8path(path).parent_path() / p);
}

const Pak &Project::pak() const
{
    if (pak_) return *pak_;
    if (!own_pak_) {
        check(!pak_path.empty(), "the project names no pak0.pak");
        own_pak_ = std::make_shared<Pak>(Pak::load(resolve(pak_path)));
    }
    return *own_pak_;
}

Palette Project::palette() const { return Palette::from(pak().get("gfx/palette.lmp")); }

Bytes Project::level_bsp(const LevelProject &lp) const
{
    if (!lp.file.empty()) return read_file(resolve(lp.file));
    return pak().get(lp.bsp);
}

// ------------------------------------------------------------ textures

void Project::texture_edits(const Bsp &bsp, const std::string &level, LevelEdits &e) const
{
    Palette pal = palette();
    std::map<std::string, RgbImage> images;
    for (auto &t : bsp.textures) {
        if (!t) continue;
        auto hand = reduced.find(t->name);
        if (hand != reduced.end()) {
            e.textures[t->name] = hand->second;
            continue;
        }
        const TextureRule *rule = nullptr;
        for (auto &r : texture_rules) {
            if (!r.levels.empty() && std::find(r.levels.begin(), r.levels.end(), level) == r.levels.end()) continue;
            for (auto &m : r.match)
                if (glob_match(lower(m), t->name)) rule = &r;
            if (rule) break;
        }
        if (!rule) continue;
        RgbImage im;
        if (!rule->image.empty()) {
            auto it = images.find(rule->image);
            if (it == images.end()) it = images.emplace(rule->image, load_png(resolve(rule->image))).first;
            im = it->second;
        } else if (!rule->pixels.empty() && rule->pixels.size() == size_t(rule->pixels_w) * size_t(rule->pixels_h)) {
            im = RgbImage(rule->pixels_w, rule->pixels_h);
            for (int i = 0; i < rule->pixels_w * rule->pixels_h; i++) {
                int c = rule->pixels[size_t(i)];
                im.set(i % rule->pixels_w, i / rule->pixels_w, pal.rgb[3 * c], pal.rgb[3 * c + 1], pal.rgb[3 * c + 2]);
            }
        } else
            continue;
        auto fit = [&](int w, int h) {
            if (!rule->tile) return resize_nearest(im, w, h);
            RgbImage o(w, h);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    size_t s = (size_t(y % im.h) * size_t(im.w) + size_t(x % im.w)) * 3;
                    o.set(x, y, im.px[s], im.px[s + 1], im.px[s + 2]);
                }
            return o;
        };
        bool special = starts_with(t->name, "*") || starts_with(t->name, "sky");
        if (!rule->reduce || special) {
            // the texture's own pixels replaced, at its full size (liquids and
            // skies are always drawn from their full-size pixels)
            int w = t->w, h = t->h;
            RgbImage px = fit(w, h);
            if (starts_with(t->name, "sky") && rule->tile == false) {
                // a sky's two layers side by side: the image on each
                RgbImage half = fit(w / 2, h);
                px = RgbImage(w, h);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        size_t s = (size_t(y) * size_t(half.w) + size_t(x % half.w)) * 3;
                        px.set(x, y, half.px[s], half.px[s + 1], half.px[s + 2]);
                    }
            }
            e.texture_pixels[t->name] = image_to_palette(px, pal, 224);
            continue;
        }
        int m = 0;
        while (m < 3 && (t->w >> (m + 1)) >= rule->min_size && (t->h >> (m + 1)) >= rule->min_size) m++;
        int w = t->w >> m, h = t->h >> m;
        BspTexture tmp = *t;
        tmp.mips[m] = image_to_palette(fit(w, h), pal, 224);
        e.textures[t->name] = auto_reduce(tmp, pal, m, w * h <= 256);
    }
}

// ------------------------------------------------------------ sounds, models

static std::set<std::string> level_sounds(const Project &p, const LevelProject &lp)
{
    Bsp m = Bsp::parse(p.level_bsp(lp));
    auto ents = parse_entities(m.entities);
    auto movers = build_movers(ents, m);
    Entity world;
    if (!ents.empty() && ents[0].classname() == "worldspawn") world = ents[0];
    std::vector<std::string> xm, xs;
    if (!lp.demo.empty()) {
        auto d = convert_demo(p.pak().get(lp.demo), movers, nullptr);
        xm = d.models;
        xs = d.sounds;
    }
    std::set<std::string> nm, ns;
    level_needs(ents, movers, world, xm, xs, nm, ns);
    return ns;
}

SoundBank Project::build_bank(std::string *log) const
{
    SoundBank bank;
    if (!shared_bank) return bank;
    std::map<std::string, int> uses;
    for (auto &lp : levels)
        for (auto &s : level_sounds(*this, lp)) uses[s]++;
    std::vector<std::string> in;
    for (auto &kv : uses) {
        auto it = sounds.find(kv.first);
        int b = it != sounds.end() ? it->second.bank : -1;
        if (it != sounds.end() && it->second.drop) continue;
        // automatic: the sounds every level has (each level still has to fit
        // in the window the bank leaves it)
        if (b == 1 || (b < 0 && kv.second >= int(levels.size()) && levels.size() >= 2)) in.push_back(kv.first);
    }
    for (auto &s : in) {
        auto it = sounds.find(s);
        Sound snd = (it != sounds.end() && !it->second.file.empty()) ? convert_wav(read_file(resolve(it->second.file)))
                                                                     : load_wav(pak().get("sound/" + s + ".wav"));
        if (it != sounds.end() && !it->second.edit.plain()) snd = apply_sound_edit(snd, it->second.edit);
        while (bank.data.size() % 4) bank.data.push_back(0);
        BankEntry e;
        e.ofs = uint32_t(bank.data.size());
        e.len = uint32_t(snd.data.size());
        e.loop = snd.loop;
        bank.data.insert(bank.data.end(), snd.data.begin(), snd.data.end());
        bank.entries[s] = e;
    }
    if (log)
        *log += "sound bank: " + std::to_string(bank.entries.size()) + " sounds, " +
                std::to_string(bank.data.size() / 1024) + " KB\n";
    return bank;
}

GameDataOptions Project::game_options(const SoundBank *bank) const
{
    GameDataOptions g;
    for (auto &kv : sounds) {
        if (kv.second.drop) g.drop_sounds.insert(kv.first);
        if (!kv.second.edit.plain()) g.sound_edits[kv.first] = kv.second.edit;
        if (!kv.second.file.empty()) g.sounds[kv.first] = convert_wav(read_file(resolve(kv.second.file)));
    }
    if (bank) g.bank = bank->entries;
    if (!models.empty()) {
        Palette pal = palette();
        for (auto &kv : models) {
            int mi = model_index(kv.first);
            if (mi < 0) continue;
            const ModelProject &mp = kv.second;
            FlatModel m = load_model(pak(), kv.first, pal);
            if (mp.frame_step > 1) {
                const std::string &path = MODELS[size_t(mi)].path;
                if (ends_with(path, ".mdl"))
                    m = trim_frames(m, every_nth_frame(model_frame_names(pak().get(path)), mp.frame_step));
            }
            if (mp.simplify < 1.0) m = simplify_model(m, mp.simplify);
            if (mp.frame_step > 1 || mp.simplify < 1.0) g.models[kv.first] = m;
            if (mp.lod_ratio > 0 && mp.lod_ratio < 1.0) g.lods[kv.first] = {simplify_model(m, mp.lod_ratio), mp.lod_dist};
        }
    }
    return g;
}

// ------------------------------------------------------------ levels

LevelOptions Project::level_options(const LevelProject &lp, bool big_endian) const
{
    LevelOptions o;
    o.big_endian = big_endian;
    o.demo = lp.demo;
    o.flat_tol = lp.flat_tol;
    o.edits = lp.edits;
    if (!texture_rules.empty() || !reduced.empty()) texture_edits(Bsp::parse(level_bsp(lp)), lp.name, o.edits);
    return o;
}

Bytes Project::build_level(const LevelProject &lp, bool big_endian, LevelInfo *info, std::string *log) const
{
    LevelOptions o = level_options(lp, big_endian);
    SoundBank bank = build_bank(nullptr);
    o.game = game_options(shared_bank ? &bank : nullptr);
    Bytes bsp = level_bsp(lp);
    o.bsp_data = &bsp;
    return q32x::build_level(pak(), lp.bsp, o, info, log);
}

Bytes Project::build_level(const std::string &name, const LevelOptions &opt, LevelInfo *info, std::string *log) const
{
    for (auto &lp : levels)
        if (lp.name == name || lp.bsp == name) {
            LevelOptions o = level_options(lp, opt.big_endian);
            if (!opt.demo.empty()) o.demo = opt.demo;
            o.flat_tol = opt.flat_tol;
            SoundBank bank = build_bank(nullptr);
            o.game = game_options(shared_bank ? &bank : nullptr);
            Bytes bsp = level_bsp(lp);
            o.bsp_data = &bsp;
            return q32x::build_level(pak(), lp.bsp, o, info, log);
        }
    // not one of the project's levels: the project's textures and sounds on it
    LevelOptions o = opt;
    o.game = game_options(nullptr);
    if (!texture_rules.empty() || !reduced.empty()) texture_edits(Bsp::parse(pak().get(name)), name, o.edits);
    return q32x::build_level(pak(), name, o, info, log);
}

MdGfxOptions Project::mdgfx_options() const { return md; }

// ------------------------------------------------------------ budgets

BudgetReport Project::check_level(const LevelProject &lp, const LevelInfo &info, size_t window) const
{
    BudgetReport r;
    r.level = lp.name;
    r.bytes = info.size;
    r.window = window;
    auto item = [&](const std::string &what, long v, long lim) { r.items.push_back({what, v, lim, v > lim}); };
    item("Level data (bytes)", long(info.size), long(window));
    item("Faces (all records)", info.faces, MAX_LEVEL_FACES);
    item("Nodes", info.nodes, 4096);
    item("Leaves", info.leafs, 4096);
    item("Vertices", info.verts, 65535);
    item("Doors, lifts, buttons and trains", info.movers, 64);
    item("Triggers", info.triggers, 64);
    item("Entity records", info.entities, 192);
    r.warnings = info.warnings;
    return r;
}

std::vector<BudgetReport> Project::check_all(std::string *log) const
{
    std::vector<BudgetReport> out;
    SoundBank bank = build_bank(log);
    size_t bank_pages = (bank.data.size() + ROM_PAGE - 1) / ROM_PAGE;
    size_t window = (ROM_WINDOW_PAGES - bank_pages) * ROM_PAGE;
    for (auto &lp : levels) {
        LevelInfo info;
        LevelOptions o = level_options(lp, true);
        o.game = game_options(shared_bank ? &bank : nullptr);
        Bytes bsp = level_bsp(lp);
        o.bsp_data = &bsp;
        q32x::build_level(pak(), lp.bsp, o, &info, nullptr);
        BudgetReport r = check_level(lp, info, window);
        if (log) {
            *log += lp.name + ":\n";
            for (auto &i : r.items)
                *log += "  " + std::string(i.error ? "OVER " : "     ") + i.what + ": " + std::to_string(i.value) +
                        " of " + std::to_string(i.limit) + "\n";
            for (auto &w : r.warnings) *log += "  warning: " + w + "\n";
        }
        out.push_back(r);
    }
    return out;
}

// ------------------------------------------------------------ auto-optimise

std::vector<std::string> Project::auto_optimise(size_t li, std::string *log)
{
    std::vector<std::string> done;
    check(li < levels.size(), "no such level");
    LevelProject &lp = levels[li];
    LevelEdits &e = lp.edits;
    auto note = [&](const std::string &s) {
        done.push_back(s);
        if (log) *log += "  " + s + "\n";
    };
    // 1. the picture stays exactly as it is
    if (!e.doors_block) { e.doors_block = true; note("closed doors hide what is behind them"); }
    if (!e.simple_collision) { e.simple_collision = true; note("simpler collision (the same solid space)"); }
    if (!e.vertex_order) { e.vertex_order = true; note("vertices in the order faces use them"); }
    if (!e.reduce.fix_tjunctions) { e.reduce.fix_tjunctions = true; note("T-junctions mended"); }
    if (!e.reduce.merge_textured) { e.reduce.merge_textured = true; note("textured faces with the same mapping merged"); }
    if (levels.size() >= 2 && !shared_bank) { shared_bank = true; note("shared sound bank"); }
    if (!md.compress) { md.compress = true; note("packed weapon tiles"); }

    // 2. tighten until it fits, the least visible first
    auto measure = [&](LevelInfo &info, size_t &window) {
        SoundBank bank = build_bank(nullptr);
        size_t pages = (bank.data.size() + ROM_PAGE - 1) / ROM_PAGE;
        window = (ROM_WINDOW_PAGES - pages) * ROM_PAGE;
        LevelOptions o = level_options(lp, true);
        o.game = game_options(shared_bank ? &bank : nullptr);
        Bytes bsp = level_bsp(lp);
        o.bsp_data = &bsp;
        q32x::build_level(pak(), lp.bsp, o, &info, nullptr);
    };
    auto fits = [&](const LevelInfo &i, size_t window) {
        return i.size <= window && i.faces <= MAX_LEVEL_FACES && i.nodes <= 4096 && i.leafs <= 4096;
    };
    struct Step {
        std::string what;
        std::function<bool()> apply;   // false: nothing to do
    };
    std::vector<Step> steps = {
        {"model lighting from a 64-unit light grid", [&] { if (e.light_grid > 0) return false; e.light_grid = 64; return true; }},
        {"one weapon render for the three largest screen sizes", [&] {
             if (md.weapon_size[0] == 2) return false;
             md.weapon_size = {2, 2, 2, 3, 4, 5};
             return true; }},
        {"monster animations keep every second frame", [&] {
             bool any = false;
             for (auto &n : FRAME_MODELS) {
                 auto &m = models[n];
                 if (m.frame_step < 2) { m.frame_step = 2; any = true; }
             }
             return any; }},
        {"faces smaller than 16 square units dropped", [&] {
             if (e.reduce.min_area >= 16) return false;
             e.reduce.min_area = 16;
             return true; }},
        {"monsters keep 70% of their triangles, with a distance version", [&] {
             bool any = false;
             for (auto &n : FRAME_MODELS) {
                 auto &m = models[n];
                 if (m.simplify > 0.7) { m.simplify = 0.7; any = true; }
                 if (m.lod_ratio <= 0) { m.lod_ratio = 0.4; m.lod_dist = 768; any = true; }
             }
             return any; }},
    };
    LevelInfo info;
    size_t window = 0;
    measure(info, window);
    if (log) *log += lp.name + ": " + std::to_string(info.size / 1024) + " KB of " + std::to_string(window / 1024) + " KB, " +
                     std::to_string(info.faces) + " faces\n";
    for (auto &st : steps) {
        if (fits(info, window)) break;
        if (!st.apply()) continue;
        note(st.what);
        measure(info, window);
        if (log) *log += "    -> " + std::to_string(info.size / 1024) + " KB, " + std::to_string(info.faces) + " faces\n";
    }
    // the light-to-polygon pieces must leave the face limit room
    if (e.light.enabled && info.faces > MAX_LEVEL_FACES - 200) {
        e.light.budget = std::max(0, MAX_LEVEL_FACES - 1000 - (info.faces - info.pieces));
        note("light-to-polygon budget " + std::to_string(e.light.budget));
        measure(info, window);
    }
    if (fits(info, window))
        note("fits: " + std::to_string(info.size / 1024) + " KB of " + std::to_string(window / 1024) + " KB");
    else
        note("still too big (" + std::to_string(info.size / 1024) + " KB of " + std::to_string(window / 1024) +
             " KB): reduce textures, leave sounds out or use flat shading only");
    return done;
}

// ------------------------------------------------------------ export

ExportResult Project::export_all(const std::string &dir, std::string *log) const
{
    ExportResult res;
    try {
        fs::create_directories(u8path(dir));
        auto out = [&](const std::string &name) { return pathstr(u8path(dir) / u8path(name)); };
        SoundBank bank = build_bank(log);
        std::vector<RomLevel> rl;
        for (auto &lp : levels) {
            LevelInfo info;
            LevelOptions o = level_options(lp, true);
            o.game = game_options(shared_bank ? &bank : nullptr);
            Bytes bsp = level_bsp(lp);
            o.bsp_data = &bsp;
            std::string l;
            Bytes b = q32x::build_level(pak(), lp.bsp, o, &info, &l);
            std::string f = out(lp.name + ".lvl");
            write_file(f, b);
            res.files.push_back(f);
            if (log) *log += lp.name + ": " + std::to_string(b.size() / 1024) + " KB\n";
            rl.push_back({lp.name, b});
        }
        MdGfx md_out = build_mdgfx(pak(), md, log);
        write_file(out("md_data.bin"), md_out.data);
        res.files.push_back(out("md_data.bin"));
        if (shared_bank) {
            write_file(out("bank.bin"), bank.data);
            res.files.push_back(out("bank.bin"));
        }
        if (!program_path.empty()) {
            Bytes prog = read_file(resolve(program_path));
            // the Mega Drive data sits at a fixed place in the program (src/mars.ld)
            const size_t MD_AT = 0x30000;
            check(prog.size() > MD_AT && std::memcmp(prog.data() + MD_AT, "MDQ1", 4) == 0,
                  "the program has no Mega Drive data at 0x30000 (it needs game 0.7.0 or later)");
            prog.resize(MD_AT);
            prog.insert(prog.end(), md_out.data.begin(), md_out.data.end());
            RomInfo info;
            Bytes rom = build_rom(prog, rl, version, &info, shared_bank ? bank.data : Bytes{});
            std::string f = out(rom_name + ".32x");
            write_file(f, rom);
            res.files.push_back(f);
            if (log)
                *log += f + ": " + std::to_string(info.size / 1024) + " KB, checksum " + std::to_string(info.checksum) + "\n";
        }
        res.ok = true;
    } catch (const std::exception &e) {
        res.error = e.what();
        if (log) *log += std::string("error: ") + e.what() + "\n";
    }
    return res;
}

}  // namespace q32x
