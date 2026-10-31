#include "q32x/level.h"
#include "q32x/collision.h"

#include <cstdio>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <thread>
#include <tuple>

#include "q32x/demo.h"
#include "q32x/geom.h"
#include "q32x/lightpoly.h"
#include "q32x/movers.h"
#include "q32x/portals.h"
#include "q32x/texreduce.h"

namespace q32x {

const std::vector<std::string> LEVEL_LUMPS = {
    "planes", "nodes", "leafs", "marksurf", "faces", "fverts", "verts",
    "texinfo", "textures", "light", "vis", "leaffrags", "clipnodes",
    "palette", "colormap", "conchars", "skytex", "facesph", "nodeparent",
    "backtile", "ents", "models", "anorms", "sounds", "movers", "mkeys",
    "triggers", "strings", "demo", "flatof", "flatnode", "flatlist"};

const std::vector<std::string> LEVEL_EXT_LUMPS = {"flatref", "flatmark", "lightgrid", "leafarea",
                                                  "doors", "leaffar", "mvmodels", "mdllod",
                                                  "flatpal"};

bool keep_textured(const std::string &name)
{
    // buttons and switches (wall, floor and shootable), exit signs, key-door
    // symbols and slipgates
    static const std::regex re(
        R"(^(\+[0-9a-j])?(.*button.*|.*basebtn.*|basebutn.*|.*floorsw.*|.*shoot.*|switch.*)"
        R"(|z_exit.*|exit0.*|key0.*|wkey0.*|slip[0-9]*)$|^\+[0-9a-j]slip$)");
    return std::regex_search(lower(name), re);
}

PaletteRemap PaletteRemap::identity()
{
    PaletteRemap r;
    for (int i = 0; i < 256; i++) r.map[size_t(i)] = uint8_t(i);
    return r;
}

bool LevelEdits::empty() const
{
    return faces.empty() && remaps.empty() && colour_rules.empty() && !light.enabled && !reduce.merge_textured && reduce.min_area <= 0 &&
           !vertex_order && !flat_only && light_grid <= 0 && !doors_block && !simple_collision && areas.empty() && textures.empty() &&
           mover_models.empty() && texture_pixels.empty() && flat_textures.empty() &&
           !flatpal.enabled;
}

// '*' and '?' globs (as the project's texture rules)
static bool colour_glob(const std::string &p, const std::string &s)
{
    size_t pi = 0, si = 0, star = std::string::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && p[pi] == '*') {
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

static int64_t fx(double v) { return pyround(v * 65536.0); }

static int v13_3(double v)
{
    int64_t r = pyround(v * 8.0);
    check(r >= -32768 && r <= 32767, "vertex out of the 13.3 range");
    return int(r);
}

using Poly = std::vector<Vec3>;

// split a polygon by the plane n.x = d -> (front, back); empty = none
static void split_poly(const Poly &pts, const double n[3], double d, Poly &front, Poly &back, double eps = 0.01)
{
    front.clear();
    back.clear();
    size_t cnt = pts.size();
    std::vector<double> dists(cnt);
    std::vector<int> sides(cnt);
    bool all_ge = true, all_le = true;
    for (size_t i = 0; i < cnt; i++) {
        dists[i] = pts[i][0] * n[0] + pts[i][1] * n[1] + pts[i][2] * n[2] - d;
        sides[i] = dists[i] > eps ? 1 : dists[i] < -eps ? -1 : 0;
        if (sides[i] < 0) all_ge = false;
        if (sides[i] > 0) all_le = false;
    }
    if (all_ge) {      // (coplanar: kept on the front)
        front = pts;
        return;
    }
    if (all_le) {
        back = pts;
        return;
    }
    for (size_t i = 0; i < cnt; i++) {
        const Vec3 &p = pts[i];
        int s = sides[i];
        double dp = dists[i];
        if (s >= 0) front.push_back(p);
        if (s <= 0) back.push_back(p);
        size_t j = (i + 1) % cnt;
        const Vec3 &q = pts[j];
        int sq = sides[j];
        double dq = dists[j];
        if (s == 0 || sq == 0 || s == sq) continue;
        double t = dp / (dp - dq);
        Vec3 mid = {p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1]), p[2] + t * (q[2] - p[2])};
        front.push_back(mid);
        back.push_back(mid);
    }
    if (front.size() < 3) front.clear();
    if (back.size() < 3) back.clear();
}

namespace {

struct FaceRec {
    int plane = 0, flags = 0, texinfo = 0, row = 0, avgc = 0;
    int64_t tmins[2] = {0, 0}, ext[2] = {0, 0};
    int lofs = -1;
    int lit = 0, unlit = 0;
    std::vector<int> vl;
    int tag = 0;
    bool has_org = false;
    Vec3 org{};
    int bsp = -1;          // the BSP face it came from
    bool exact = false;    // a hand-set colour at every light level
};

struct Frag {
    int leaf;
    Poly poly;
    int fi, side;
    Poly pts0;
};

// a face's light map, before it is stored
struct RawLight {
    bool ok = false;
    std::vector<uint8_t> L;
    int64_t smax = 0, tmax = 0;
    int64_t tmins[2] = {0, 0}, ext[2] = {0, 0};
};

// an output world face: the BSP faces it stands for
struct Plan {
    std::vector<int> members;
    Poly poly;
    bool merged = false;
    RawLight light;          // merged faces: their joined light map
    bool remove = false;
    // the user palette's shading (occlusion and lights) on the light-map grid
    std::vector<uint8_t> syn;
    int syn_avg = -1;
    int64_t syn_tmins[2] = {0, 0};
    int syn_smax = 0, syn_tmax = 0;
};

using VKey = std::array<int, 3>;

}  // namespace

// Solid brush models that never move (func_wall, and func_bossgate, which
// stays while the player has not every rune) are drawn with the world; for
// the player and monsters to stand on them and bump into them, their clip
// hulls (1 and 2) are joined to the world's: every empty leaf of the world's
// hull near the model takes a copy of the model's hull, so a point is solid
// where either is. (Shots and missiles, which trace hull 0, still pass.)
int merge_static_hulls(Bsp &m)
{
    int n = 0;
    for (auto &e : parse_entities(m.entities)) {
        std::string cls = e.classname(), mdl = e.get("model");
        if ((cls != "func_wall" && cls != "func_bossgate") || !starts_with(mdl, "*") || !present(e)) continue;
        int mi = std::atoi(mdl.c_str() + 1);
        if (mi <= 0 || mi >= int(m.models.size())) continue;
        const BspModel &mo = m.models[size_t(mi)];
        // (a level whose clip hulls would grow past their limit keeps the
        // model apart: drawn, but not solid)
        std::vector<BspClipnode> saved = m.clipnodes;
        for (int h = 1; h <= 2; h++) {
            int mh = mo.headnode[h];
            if (mh < 0) continue;
            // the model's hull, copied in for each leaf it reaches
            std::function<int(int)> copy = [&](int c) -> int {
                if (c < 0) return c;
                BspClipnode cn = m.clipnodes.at(size_t(c));
                int idx = int(m.clipnodes.size());
                m.clipnodes.push_back(cn);
                int a = copy(cn.child[0]), b = copy(cn.child[1]);
                m.clipnodes[size_t(idx)].child[0] = a;
                m.clipnodes[size_t(idx)].child[1] = b;
                return idx;
            };
            const double pad = 72;   // (the hulls are the model's grown by the player's or a monster's size)
            double lo[3], hi[3];
            for (int k = 0; k < 3; k++) {
                lo[k] = mo.mins[k] - pad;
                hi[k] = mo.maxs[k] + pad;
            }
            const size_t original = m.clipnodes.size();
            int shared = 0;   // one copy for every leaf (the hull is then a graph, which a trace walks the same)
            bool copied = false;
            std::function<void(int, std::array<double, 6>)> walk = [&](int node, std::array<double, 6> bb) {
                const BspClipnode cn = m.clipnodes.at(size_t(node));
                const BspPlane &pl = m.planes.at(size_t(cn.plane));
                for (int side = 0; side < 2; side++) {
                    std::array<double, 6> cb = bb;
                    if (pl.type < 3) {   // an axial plane narrows the bounds
                        if (side == 0) cb[size_t(pl.type)] = std::max(cb[size_t(pl.type)], pl.d);
                        else cb[size_t(3 + pl.type)] = std::min(cb[size_t(3 + pl.type)], pl.d);
                    }
                    bool near = true;
                    for (int k = 0; k < 3; k++)
                        if (cb[size_t(k)] > hi[k] || cb[size_t(3 + k)] < lo[k]) near = false;
                    if (!near) continue;
                    int c = cn.child[side];
                    if (c >= 0) {
                        if (size_t(c) < original) walk(c, cb);
                    } else if (c == CONTENTS_EMPTY) {
                        if (!copied) {
                            shared = copy(mh);
                            copied = true;
                        }
                        m.clipnodes[size_t(node)].child[side] = shared;
                    }
                }
            };
            int wh = m.models[0].headnode[h];
            if (wh >= 0) walk(wh, {-1e9, -1e9, -1e9, 1e9, 1e9, 1e9});
        }
        if (m.clipnodes.size() >= 32767) {
            m.clipnodes = std::move(saved);
            continue;
        }
        n++;
    }
    return n;
}

Bytes build_level(const Pak &pak, const std::string &bsp_name, const LevelOptions &opt, LevelInfo *info,
                  std::string *log)
{
    const bool big = opt.big_endian;
    const LevelEdits &E = opt.edits;
    Bsp m = Bsp::parse(opt.bsp_data ? *opt.bsp_data : pak.get(bsp_name));
    int merged_static = merge_static_hulls(m);
    std::vector<CollisionStats> collision;
    if (E.simple_collision) collision = simplify_collision(m);
    Palette pal = Palette::from(pak.get("gfx/palette.lmp"));
    Bytes colormap = Reader(pak.get("gfx/colormap.lmp")).slice(0, 64 * 256);
    check(colormap.size() == 64 * 256, "colormap too short");
    auto wad = read_wad(pak.get("gfx.wad"));
    check(wad.count("conchars") && wad["conchars"].size() >= 128 * 128, "gfx.wad has no conchars");
    const Bytes &conchars = wad["conchars"];
    std::vector<Entity> ents = parse_entities(m.entities);
    std::vector<std::string> warnings;

    std::vector<BspPlane> &planes = m.planes;
    const auto &texinfo = m.texinfo;
    const auto &textures = m.textures;
    const auto &bspfaces = m.faces;
    std::vector<BspNode> nodes = m.nodes;
    const auto &leafs = m.leafs;
    const auto &models = m.models;
    auto say = [&](const std::string &s) {
        if (log) *log += s + "\n";
    };
    auto warn = [&](const std::string &s) {
        warnings.push_back(s);
        say("warning: " + s);
    };
    if (merged_static) say("solid walls joined to the world's collision: " + std::to_string(merged_static));
    for (auto &c : collision) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "collision hull %d: %d -> %d nodes, %.1f -> %.1f tests a point (worst %d -> %d)%s",
                      c.hull, c.nodes_before, c.nodes_after, c.depth_before, c.depth_after, c.max_before, c.max_after,
                      c.mismatches ? " MISMATCH" : "");
        say(buf);
        if (c.mismatches) warn("collision hull " + std::to_string(c.hull) + ": the rebuilt hull differs at " + std::to_string(c.mismatches) + " test points");
    }

    // textures painted over
    for (auto &t : m.textures) {
        if (!t) continue;
        auto it = E.texture_pixels.find(t->name);
        if (it == E.texture_pixels.end()) continue;
        if (it->second.size() != size_t(t->w) * size_t(t->h)) {
            warn("texture " + t->name + ": the replacement is not " + std::to_string(t->w) + "x" + std::to_string(t->h));
            continue;
        }
        std::vector<int> all(256);
        for (int c = 0; c < 256; c++) all[size_t(c)] = c;
        t->mips[0] = it->second;
        int w = t->w, h = t->h;
        for (int k = 1; k < 4; k++) {
            t->mips[k] = downsample(t->mips[k - 1], w, h, pal, all);
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
    }

    // ------------------------------------------------ reduced textures
    std::vector<std::optional<ReducedTexture>> reduced(textures.size());
    for (size_t i = 0; i < textures.size(); i++) {
        if (!textures[i]) continue;
        auto it = E.textures.find(textures[i]->name);
        if (it == E.textures.end()) continue;
        const BspTexture &t = *textures[i];
        if (starts_with(t.name, "*") || starts_with(t.name, "sky")) {
            warn("texture " + t.name + ": liquids and skies cannot be reduced");
            continue;
        }
        ReducedTexture r = it->second;
        r.min_mip = std::clamp(r.min_mip, 0, 3);
        int w = t.w >> r.min_mip, h = t.h >> r.min_mip;
        if (r.pixels.size() != size_t(w) * size_t(h)) r = auto_reduce(t, pal, r.min_mip, r.four_bit);
        if (r.four_bit && w * h > 256) {
            warn("texture " + t.name + ": a 4-bit texture may be at most 256 texels at its finest level; kept at 8 bits");
            r.four_bit = false;
            auto lv = reduced_levels(t, r, pal);
            r.pixels = lv[size_t(r.min_mip)];
        }
        reduced[i] = r;
    }
    std::vector<std::vector<Bytes>> reduced_lv(textures.size());
    for (size_t i = 0; i < textures.size(); i++)
        if (reduced[i]) reduced_lv[i] = reduced_levels(*textures[i], *reduced[i], pal);

    // ------------------------------------------------ texture info / flags
    std::vector<int> tex_avg;
    std::vector<Rgb> tex_rgb;
    for (size_t i = 0; i < textures.size(); i++) {
        const auto &t = textures[i];
        if (!t) {
            tex_avg.push_back(0);
            tex_rgb.push_back({0, 0, 0});
            continue;
        }
        const Bytes &mip = reduced[i] ? reduced_lv[i][3] : t->mips[3];
        int64_t rs = 0, gs = 0, bs = 0;
        for (uint8_t c : mip) {
            rs += pal.rgb[3 * c];
            gs += pal.rgb[3 * c + 1];
            bs += pal.rgb[3 * c + 2];
        }
        double n = double(std::max<size_t>(1, mip.size()));
        tex_avg.push_back(pal.nearest(rs / n, gs / n, bs / n));
        tex_rgb.push_back({rs / n, gs / n, bs / n});
    }

    // ------------------------------------------------ the user's flat palette
    // Flat mode in the user's own colours: each texture's average made more
    // vivid and matched to them; the game swaps in the palette and a colour
    // table for models while textures are off (FLATPAL)
    const FlatPalOptions &FP = E.flatpal;
    const bool UP = FP.enabled, USH = UP && FP.shade;
    std::optional<FlatPalTables> fpt;
    std::vector<int> tex_user(textures.size(), 0);
    if (UP) {
        fpt = build_flatpal_tables(FP, pal, colormap);
        std::vector<Rgb> viv;
        tex_user = flatpal_texture_colours(m, tex_rgb, FP, &viv);
        if (std::getenv("Q32X_FLATPAL_DEBUG"))
            for (size_t i = 0; i < textures.size(); i++)
                if (textures[i]) {
                    char b[200];
                    std::snprintf(b, sizeof b, "  %-16s %3.0f %3.0f %3.0f -> %3.0f %3.0f %3.0f -> %d", textures[i]->name.c_str(),
                                  tex_rgb[i][0], tex_rgb[i][1], tex_rgb[i][2], viv[i][0], viv[i][1], viv[i][2], tex_user[i]);
                    say(b);
                }
    }
    // a flat colour at a light row: Quake's colormap, or the user palette's own shades
    auto fcm = [&](bool user, int row, int c) -> int {
        return user ? fpt->ucm.at(size_t(row * 256 + c)) : colormap.at(size_t(row * 256 + c));
    };
    std::optional<FlatShader> shader;
    if (USH) shader.emplace(m, ents, FP);

    auto always_flat = [&](const std::string &name) {
        for (auto &g : E.flat_textures)
            if (colour_glob(lower(g), lower(name))) return true;
        return false;
    };
    auto tex_flags = [&](int ti) {
        const auto &t = textures.at(size_t(texinfo.at(size_t(ti)).miptex));
        std::string name = t ? t->name : "";
        int fl = 0;
        if (keep_textured(name)) fl |= 64;   // drawn textured in flat-shaded mode too
        else if (always_flat(name)) fl |= 128;   // drawn flat with textures on too
        if (starts_with(name, "sky")) fl |= 2;
        if (starts_with(name, "*")) {
            fl |= 4;
            // the liquid drawer assumes Quake's 64x64 turbulent textures
            check(t->w == 64 && t->h == 64, "liquid texture not 64x64: " + name);
            if (!starts_with(name, "*tele")) fl |= 16;   // liquid: may be drawn as a see-through mesh
        }
        return fl;
    };
    const int NOMERGE = 2 | 4 | 16 | 64;   // sky, liquids, kept textured (switches...)

    auto fedit = [&](int fi) -> const FaceEdit * {
        auto it = E.faces.find(fi);
        return it == E.faces.end() ? nullptr : &it->second;
    };

    // colour remaps: the faces each applies to
    std::vector<std::set<int>> remap_faces(E.remaps.size());
    for (size_t r = 0; r < E.remaps.size(); r++)
        for (int l : E.remaps[r].leafs) {
            if (l < 0 || l >= int(leafs.size())) continue;
            const BspLeaf &lf = leafs[size_t(l)];
            for (int k = lf.firstmark; k < lf.firstmark + lf.nummark && k < int(m.marksurfaces.size()); k++)
                remap_faces[r].insert(m.marksurfaces[size_t(k)]);
        }
    auto remap_colour = [&](int fi, int c) {
        for (size_t r = 0; r < E.remaps.size(); r++)
            if (E.remaps[r].leafs.empty() || remap_faces[r].count(fi)) c = E.remaps[r].map[size_t(c)];
        return c;
    };

    // ------------------------------------------------ face records
    std::map<std::array<int, 5>, int> vert_index;
    std::vector<std::array<int, 4>> out_verts;
    auto vindex = [&](const Vec3 &p, int mover, int key) {
        // a moving model's vertices are its own: the renderer shifts them by
        // that mover's offset (the vertex record's last word is mover + 1)
        std::array<int, 3> q = {v13_3(p[0]), v13_3(p[1]), v13_3(p[2])};
        std::array<int, 5> k = {mover, key, q[0], q[1], q[2]};
        auto it = vert_index.find(k);
        if (it != vert_index.end()) return it->second;
        int idx = int(out_verts.size());
        vert_index[k] = idx;
        out_verts.push_back({q[0], q[1], q[2], mover + 1});
        return idx;
    };

    Bytes lightdata;
    std::map<int, std::pair<int, int>> face_light_cache;

    auto face_extents = [&](const Poly &pts, int ti, int64_t tmins[2], int64_t ext[2]) {
        const auto &tv = texinfo.at(size_t(ti)).vecs;
        double mins[2] = {1e18, 1e18}, maxs[2] = {-1e18, -1e18};
        for (auto &p : pts)
            for (int j = 0; j < 2; j++) {
                double val = p[0] * tv[j * 4] + p[1] * tv[j * 4 + 1] + p[2] * tv[j * 4 + 2] + tv[j * 4 + 3];
                mins[j] = std::min(mins[j], val);
                maxs[j] = std::max(maxs[j], val);
            }
        for (int j = 0; j < 2; j++) {
            int64_t bmin = pyfloor(mins[j] / 16), bmax = pyceil(maxs[j] / 16);
            tmins[j] = bmin * 16;
            ext[j] = (bmax - bmin) * 16;
        }
    };

    auto lightable = [&](int fi) {
        const BspFace &f = bspfaces.at(size_t(fi));
        return !(f.lightofs < 0 || f.styles[0] == 255 || (tex_flags(f.texinfo) & 6));
    };

    auto raw_light = [&](int fi, const Poly &orig_pts) {
        RawLight r;
        const BspFace &f = bspfaces.at(size_t(fi));
        face_extents(orig_pts, f.texinfo, r.tmins, r.ext);
        r.smax = (r.ext[0] >> 4) + 1;
        r.tmax = (r.ext[1] >> 4) + 1;
        if (!lightable(fi)) return r;
        int nmaps = 0;
        for (int s : f.styles)
            if (s != 255) nmaps++;
        int64_t size = r.smax * r.tmax;
        r.L.resize(size_t(size));
        for (int64_t i = 0; i < size; i++) {
            int64_t bl = 0;
            for (int k = 0; k < nmaps; k++) bl += int64_t(m.lighting.at(size_t(f.lightofs + k * size + i))) * 264;
            int64_t t = (255 * 256 - bl) >> 2;
            if (t < 64) t = 64;
            int64_t L = t >> 6;
            if (L > 255) L = 255;
            r.L[size_t(i)] = uint8_t(L);
        }
        r.ok = true;
        return r;
    };

    auto store_light = [&](const RawLight &r) -> std::pair<int, int> {
        if (!r.ok) return {-1, 255};
        int64_t tot = 0;
        for (auto v : r.L) tot += v;
        int o = int(lightdata.size());
        lightdata.insert(lightdata.end(), r.L.begin(), r.L.end());
        while (lightdata.size() % 2) lightdata.push_back(0);
        return {o, int(floordiv(tot, int64_t(r.L.size())))};
    };

    auto build_light = [&](int fi, const Poly &orig_pts) -> std::pair<int, int> {
        auto it = face_light_cache.find(fi);
        if (it != face_light_cache.end()) return it->second;
        auto res = store_light(raw_light(fi, orig_pts));
        face_light_cache[fi] = res;
        return res;
    };

    std::vector<FaceRec> face_recs;

    // the user palette's shading of a brush model's face (as it lies in the model)
    std::map<int, int> frag_cache;
    auto facing = [&](int fi) {
        const BspFace &f = bspfaces.at(size_t(fi));
        const BspPlane &pl = planes.at(size_t(f.plane));
        return f.side ? Vec3{-pl.n[0], -pl.n[1], -pl.n[2]} : Vec3{pl.n[0], pl.n[1], pl.n[2]};
    };
    auto frag_light = [&](int fi, const Poly &pts) {
        auto it = frag_cache.find(fi);
        if (it != frag_cache.end()) return it->second;
        int v = shader->poly_light(pts, facing(fi));
        frag_cache[fi] = v;
        return v;
    };

    // faces that are only ever seen from inside a liquid (pool floors and
    // walls below the surface): the renderer draws them a mip level coarser
    // when they are seen through see-through water from above
    std::set<int> in_liquid, in_air;
    for (auto &lf : leafs) {
        for (int k = lf.firstmark; k < lf.firstmark + lf.nummark && k < int(m.marksurfaces.size()); k++) {
            int ms = m.marksurfaces[size_t(k)];
            if (lf.contents == -3 || lf.contents == -4 || lf.contents == -5)
                in_liquid.insert(ms);
            else if (lf.contents == CONTENTS_EMPTY)
                in_air.insert(ms);
        }
    }
    std::set<int> underwater;
    for (int f : in_liquid)
        if (!in_air.count(f)) underwater.insert(f);

    // the flat colour of a face at a light row
    // (quake_side: in Quake's palette even with the user's: models the game lights itself)
    auto face_colours = [&](int fi, int flags, int avgL, int &row, int &avgc, int &lit, int &unlit, bool &exact,
                            bool quake_side) {
        const BspFace &f = bspfaces.at(size_t(fi));
        const bool up = UP && !quake_side;
        avgc = (up ? tex_user : tex_avg).at(size_t(texinfo.at(size_t(f.texinfo)).miptex));
        const FaceEdit *e = fedit(fi);
        exact = false;
        if (UP && quake_side) {
            // (the rules' and faces' colours are the user palette's)
        } else if (e && e->colour >= 0 && e->colour < 256) {
            avgc = e->colour;
            exact = e->exact;
        } else {
            if (!E.colour_rules.empty()) {
                const BspTexture *t = m.face_texture(f);
                const BspPlane &pl = planes.at(size_t(f.plane));
                double nz = f.side ? -pl.n[2] : pl.n[2];
                int surf = nz > 0.7 ? 1 : nz < -0.7 ? 3 : 2;
                for (auto &cr : E.colour_rules) {
                    if (cr.surface && cr.surface != surf) continue;
                    bool hit = false;
                    for (auto &g : cr.match)
                        if (t && colour_glob(lower(g), t->name)) hit = true;
                    if (hit) {
                        avgc = std::clamp(cr.colour, 0, 255);
                        break;
                    }
                }
            }
            avgc = remap_colour(fi, avgc);
        }
        row = !(flags & 6) ? (avgL << 6) >> 8 : 32;
        if (E.light.hard_shadows && !(flags & 6)) row = std::clamp(E.light.lit_row, 0, 63);
        if ((flags & 8) && !(up && USH)) row = 63;
        lit = fcm(up, row, avgc);
        unlit = fcm(up, 32, avgc);
        if (flags & 2) lit = unlit;   // sky: use the darker back layer average
        if (exact) lit = unlit = avgc;
    };

    // a face record. light: the stored light map (offset, average) and the
    // extents, or computed from orig_pts
    auto add_face = [&](int fi, const Poly &pts, const Poly &orig_pts, int side_flag, bool allow_empty, int mover,
                        int key, const Plan *plan) -> int {
        const BspFace &f = bspfaces.at(size_t(fi));
        int ti = f.texinfo;
        int flags = tex_flags(ti) | side_flag;
        if (plan) {
            bool uw = true;
            for (int mm : plan->members)
                if (!underwater.count(mm)) uw = false;
            if (uw && !(flags & 4)) flags |= 32;
        } else if (underwater.count(fi) && !(flags & 4))
            flags |= 32;
        FaceRec r;
        int lofs, avgL;
        if (plan && plan->merged) {
            r.tmins[0] = plan->light.tmins[0];
            r.tmins[1] = plan->light.tmins[1];
            r.ext[0] = plan->light.ext[0];
            r.ext[1] = plan->light.ext[1];
            std::tie(lofs, avgL) = store_light(plan->light);
        } else {
            face_extents(orig_pts, ti, r.tmins, r.ext);
            std::tie(lofs, avgL) = build_light(fi, orig_pts);
        }
        if (lofs < 0 && !(flags & 6)) flags |= 8;   // no lightmap -> dark
        if (USH && !(flags & 6)) avgL = plan && plan->syn_avg >= 0 ? plan->syn_avg : frag_light(fi, orig_pts);
        int row, avgc, lit, unlit;
        bool exact;
        face_colours(fi, flags, avgL, row, avgc, lit, unlit, exact, false);
        if (flags & 128) {
            // flat with textures on too: its colour in Quake's palette (the one
            // textured mode shows) in place of the light map's offset
            int q = avgc;
            // (liquids and skies are drawn unshaded: Quake's full-bright colours too)
            if (UP)
                q = pal.nearest(FP.rgb[size_t(3 * avgc)], FP.rgb[size_t(3 * avgc + 1)], FP.rgb[size_t(3 * avgc + 2)],
                                (flags & 6) ? 256 : 224);
            int qlit = exact ? q : fcm(false, row, q), qunlit = exact ? q : fcm(false, 32, q);
            if (flags & 2) qlit = qunlit;
            lofs = qlit << 8 | qunlit;
        }
        // remove collinear duplicates in quantized space
        std::vector<int> vl;
        bool removed = plan && plan->remove;
        if (!removed)
            for (auto &p : pts) {
                int vi = vindex(p, mover, key);
                if (vl.empty() || vl.back() != vi) vl.push_back(vi);
            }
        if (vl.size() > 1 && vl.front() == vl.back()) vl.pop_back();
        if (vl.size() < 3) {
            if (!allow_empty) return -1;
            vl.clear();
        }
        r.plane = f.plane;
        r.flags = flags;
        r.texinfo = ti;
        r.row = row;
        r.avgc = avgc;
        r.lofs = lofs;
        r.lit = lit;
        r.unlit = unlit;
        r.vl = vl;
        r.tag = mover >= 0 ? (mover + 1) | (key << 8) : 0;
        r.bsp = fi;
        r.exact = exact;
        face_recs.push_back(std::move(r));
        return int(face_recs.size()) - 1;
    };

    // ------------------------------------------------ the world faces
    const int nbspworld = models.at(0).numfaces;
    check(models[0].firstface == 0, "the world model's faces do not start at 0");
    std::vector<Plan> plan;
    std::vector<int> bsp_to_plan(static_cast<size_t>(nbspworld), -1);
    bool replanned = false;   // node face ranges and marks are rewritten
    int textured_merged = 0, removed_faces = 0;
    {
        for (int fi = 0; fi < nbspworld; fi++) {
            Plan p;
            p.members = {fi};
            p.poly = m.face_polygon(bspfaces.at(size_t(fi)));
            const FaceEdit *e = fedit(fi);
            if (e && e->remove) p.remove = true;
            plan.push_back(std::move(p));
        }
        // faces too small to see, unless locked or special
        if (E.reduce.min_area > 0)
            for (auto &p : plan) {
                int fi = p.members[0];
                const FaceEdit *e = fedit(fi);
                if ((e && e->lock) || (tex_flags(bspfaces[size_t(fi)].texinfo) & NOMERGE)) continue;
                if (!p.remove && winding_area(p.poly) < E.reduce.min_area) p.remove = true;
            }
        if (E.reduce.merge_textured) {
            // neighbouring faces of a node side with the same texture mapping
            // become one face with a joined light map: fewer faces in
            // textured mode, the same picture
            std::vector<Plan> out;
            std::vector<bool> used(static_cast<size_t>(nbspworld), false);
            auto qkey = [](const Vec3 &p) { return VKey{v13_3(p[0]), v13_3(p[1]), v13_3(p[2])}; };
            for (size_t ni = 0; ni < nodes.size(); ni++) {
                int ff = nodes[ni].firstface, nf = nodes[ni].numfaces;
                if (ff + nf > nbspworld) continue;
                std::vector<Plan> cur;
                for (int i = ff; i < ff + nf; i++) {
                    cur.push_back(plan[size_t(i)]);
                    used[size_t(i)] = true;
                }
                auto mkey = [&](const Plan &p) {
                    int fi = p.members[0];
                    const BspFace &f = bspfaces[size_t(fi)];
                    const FaceEdit *e = fedit(fi);
                    return std::make_tuple(f.texinfo, f.side, tex_flags(f.texinfo), int(underwater.count(fi)),
                                           f.styles[0], f.styles[1], int(lightable(fi)), e ? e->colour : -1,
                                           e ? int(e->exact) : 0, e ? int(e->no_split) : 0, e ? e->band : -1);
                };
                auto can = [&](const Plan &p) {
                    int fi = p.members[0];
                    const FaceEdit *e = fedit(fi);
                    return !p.remove && !(e && e->lock) && !(tex_flags(bspfaces[size_t(fi)].texinfo) & 4) &&
                           p.poly.size() >= 3;
                };
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (size_t x = 0; x < cur.size(); x++) {
                        if (!can(cur[x])) continue;
                        for (size_t y = x + 1; y < cur.size(); y++) {
                            if (!can(cur[y]) || mkey(cur[x]) != mkey(cur[y])) continue;
                            // a shared edge, in quantized space
                            const Poly &A = cur[x].poly, &B = cur[y].poly;
                            size_t na = A.size(), nb = B.size();
                            std::vector<VKey> ka, kb;
                            for (auto &p : A) ka.push_back(qkey(p));
                            for (auto &p : B) kb.push_back(qkey(p));
                            Poly U;
                            bool ok = false;
                            for (size_t i = 0; i < na && !ok; i++) {
                                VKey a0 = ka[i], a1 = ka[(i + 1) % na];
                                for (size_t j = 0; j < nb && !ok; j++) {
                                    if (kb[j] != a1 || kb[(j + 1) % nb] != a0) continue;
                                    for (size_t k = 0; k < na; k++) U.push_back(A[(i + 1 + k) % na]);
                                    for (size_t k = 0; k + 2 < nb; k++) U.push_back(B[(j + 2 + k) % nb]);
                                    ok = true;
                                }
                            }
                            if (!ok || U.size() > 16) continue;
                            // convex (collinear points are kept: neighbours may share them)
                            std::set<VKey> seen;
                            bool dup = false;
                            for (auto &p : U)
                                if (!seen.insert(qkey(p)).second) dup = true;
                            if (dup) continue;
                            const BspPlane &pl = planes[size_t(bspfaces[size_t(cur[x].members[0])].plane)];
                            int kk = 0;
                            for (int k = 1; k < 3; k++)
                                if (std::fabs(pl.n[k]) > std::fabs(pl.n[kk])) kk = k;
                            int ax0 = (kk + 1) % 3, ax1 = (kk + 2) % 3;
                            int sign = 0;
                            bool convex = true;
                            size_t n = U.size();
                            for (size_t k = 0; k < n && convex; k++) {
                                VKey p0 = qkey(U[(k + n - 1) % n]), p1 = qkey(U[k]), p2 = qkey(U[(k + 1) % n]);
                                int64_t c = int64_t(p1[size_t(ax0)] - p0[size_t(ax0)]) * (p2[size_t(ax1)] - p1[size_t(ax1)]) -
                                            int64_t(p1[size_t(ax1)] - p0[size_t(ax1)]) * (p2[size_t(ax0)] - p1[size_t(ax0)]);
                                if (c == 0) continue;
                                int sg = c > 0 ? 1 : -1;
                                if (sign && sg != sign) convex = false;
                                sign = sg;
                            }
                            if (!convex) continue;
                            int fi = cur[x].members[0];
                            int64_t tmins[2], ext[2];
                            face_extents(U, bspfaces[size_t(fi)].texinfo, tmins, ext);
                            if (ext[0] > E.reduce.max_ext || ext[1] > E.reduce.max_ext) continue;
                            if (int64_t(ext[0]) * ext[1] > int64_t(128 * 128) * 64) continue;
                            // the joined light map
                            RawLight J;
                            J.tmins[0] = tmins[0];
                            J.tmins[1] = tmins[1];
                            J.ext[0] = ext[0];
                            J.ext[1] = ext[1];
                            J.smax = (ext[0] >> 4) + 1;
                            J.tmax = (ext[1] >> 4) + 1;
                            if (lightable(fi)) {
                                std::vector<RawLight> parts;
                                for (auto *pp : {&cur[x], &cur[y]}) {
                                    if (pp->merged)
                                        parts.push_back(pp->light);
                                    else
                                        parts.push_back(raw_light(pp->members[0], pp->poly));
                                }
                                J.L.assign(size_t(J.smax * J.tmax), 0);
                                std::vector<int> cnt(J.L.size(), 0), acc(J.L.size(), 0);
                                for (auto &pt : parts)
                                    for (int64_t t = 0; t < pt.tmax; t++)
                                        for (int64_t s = 0; s < pt.smax; s++) {
                                            int64_t gs = (pt.tmins[0] >> 4) + s - (J.tmins[0] >> 4);
                                            int64_t gt = (pt.tmins[1] >> 4) + t - (J.tmins[1] >> 4);
                                            if (gs < 0 || gt < 0 || gs >= J.smax || gt >= J.tmax) continue;
                                            size_t o = size_t(gt * J.smax + gs);
                                            acc[o] += pt.L[size_t(t * pt.smax + s)];
                                            cnt[o]++;
                                        }
                                // samples no part covers take the nearest covered one
                                for (int64_t t = 0; t < J.tmax; t++)
                                    for (int64_t s = 0; s < J.smax; s++) {
                                        size_t o = size_t(t * J.smax + s);
                                        if (cnt[o]) {
                                            J.L[o] = uint8_t((acc[o] + cnt[o] / 2) / cnt[o]);
                                            continue;
                                        }
                                        int64_t bd = INT64_MAX;
                                        size_t bo = o;
                                        for (int64_t t2 = 0; t2 < J.tmax; t2++)
                                            for (int64_t s2 = 0; s2 < J.smax; s2++) {
                                                size_t o2 = size_t(t2 * J.smax + s2);
                                                if (!cnt[o2]) continue;
                                                int64_t d = (s2 - s) * (s2 - s) + (t2 - t) * (t2 - t);
                                                if (d < bd) {
                                                    bd = d;
                                                    bo = o2;
                                                }
                                            }
                                        J.L[o] = uint8_t((acc[bo] + cnt[bo] / 2) / std::max(1, cnt[bo]));
                                    }
                                J.ok = true;
                            }
                            cur[x].poly = U;
                            cur[x].members.insert(cur[x].members.end(), cur[y].members.begin(), cur[y].members.end());
                            cur[x].merged = true;
                            cur[x].light = J;
                            cur.erase(cur.begin() + std::ptrdiff_t(y));
                            changed = true;
                            y--;
                        }
                    }
                }
                for (auto &p : cur) {
                    if (p.merged) textured_merged += int(p.members.size()) - 1;
                    out.push_back(std::move(p));
                }
            }
            for (int i = 0; i < nbspworld; i++)
                if (!used[size_t(i)]) out.push_back(plan[size_t(i)]);   // (in no node)
            if (textured_merged) {
                plan = std::move(out);
                replanned = true;
            }
        }
        for (size_t i = 0; i < plan.size(); i++) {
            for (int mm : plan[i].members) bsp_to_plan[size_t(mm)] = int(i);
            if (plan[i].remove) removed_faces++;
        }
        if (replanned) {
            // the nodes' face ranges over the new faces
            for (auto &n : nodes) {
                int ff = n.firstface, nf = n.numfaces;
                if (ff + nf > nbspworld || nf == 0) continue;
                int lo = INT32_MAX, hi = -1;
                for (int i = ff; i < ff + nf; i++) {
                    lo = std::min(lo, bsp_to_plan[size_t(i)]);
                    hi = std::max(hi, bsp_to_plan[size_t(i)]);
                }
                n.firstface = lo;
                n.numfaces = hi - lo + 1;
            }
        }
    }
    const int nworld = int(plan.size());
    if (USH) {
        // occlusion and lights on every world face's light-map grid, on all cores
        auto t0 = std::chrono::steady_clock::now();
        std::atomic<size_t> next{0};
        std::string err;
        std::mutex err_lock;
        auto work = [&] {
            for (;;) {
                size_t i = next++;
                if (i >= plan.size()) return;
                Plan &p = plan[i];
                int fi = p.members[0];
                const BspFace &f = bspfaces.at(size_t(fi));
                if (p.remove || p.poly.size() < 3 || (tex_flags(f.texinfo) & 6)) continue;
                try {
                    int64_t tm[2], ex[2];
                    face_extents(p.poly, f.texinfo, tm, ex);
                    int smax = int(ex[0] >> 4) + 1, tmax = int(ex[1] >> 4) + 1;
                    std::vector<uint8_t> q;
                    if (FP.lightmaps > 0) {
                        RawLight L = p.merged ? p.light : raw_light(fi, p.poly);
                        if (L.ok && L.smax == smax && L.tmax == tmax) q = L.L;
                    }
                    p.syn = shader->light_map(p.poly, facing(fi), texinfo.at(size_t(f.texinfo)).vecs, tm, smax, tmax, q);
                    int64_t tot = 0;
                    for (auto v : p.syn) tot += v;
                    p.syn_avg = int(floordiv(tot, int64_t(p.syn.size())));
                    p.syn_tmins[0] = tm[0];
                    p.syn_tmins[1] = tm[1];
                    p.syn_smax = smax;
                    p.syn_tmax = tmax;
                } catch (std::exception &e) {
                    std::lock_guard<std::mutex> g(err_lock);
                    err = e.what();
                }
            }
        };
        unsigned nt = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        std::vector<std::thread> th;
        for (unsigned k = 0; k < nt; k++) th.emplace_back(work);
        for (auto &t : th) t.join();
        check(err.empty(), "flat palette shading: " + err);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        char buf[160];
        std::snprintf(buf, sizeof buf, "flat palette shading: %d lights, %d faces, %.1f s on %u threads", shader->nlights(),
                      nworld, secs, nt);
        say(buf);
        int hist[8] = {};
        for (auto &p : plan)
            if (p.syn_avg >= 0) hist[std::min(7, p.syn_avg / 32)]++;
        std::string h = "  faces by light (bright to dark):";
        for (int k : hist) h += " " + std::to_string(k);
        say(h);
    }
    std::vector<int> face_map(static_cast<size_t>(nworld));
    for (int i = 0; i < nworld; i++) {
        const Plan &p = plan[size_t(i)];
        int fi = p.members[0];
        const BspFace &f = bspfaces.at(size_t(fi));
        int idx = add_face(fi, p.poly, p.poly, f.side ? 1 : 0, true, -1, 0, &p);
        check(idx == i, "world face numbering");
        face_map[size_t(i)] = idx;
    }
    // (in a submodel's node range, the faces are submodel faces: left alone)
    if (replanned)
        for (auto &n : nodes)
            if (n.firstface + n.numfaces > nbspworld) {
                n.firstface = nworld;
                n.numfaces = 0;
            }

    // ------------------------------------------------ bmodels -> leaf fragments
    // Brush models are clipped into the world leaves. A moving one (doors,
    // plats, buttons) is clipped once per keyframe along its path; its
    // fragments carry (mover + 1) | key << 8, and the renderer only draws the
    // keyframe nearest the mover's position, shifted by the rest.
    std::vector<Mover> movers = build_movers(ents, m);

    auto clip_model = [&](int mi, const Vec3 &org) {
        const BspModel &mo = models.at(size_t(mi));
        std::vector<Frag> out;
        for (int fi = mo.firstface; fi < mo.firstface + mo.numfaces; fi++) {
            const BspFace &f = bspfaces.at(size_t(fi));
            const BspTexture *tex = m.face_texture(f);
            check(tex != nullptr, "brush model face without a texture");
            if (tex->name == "trigger" || tex->name == "clip") continue;
            Poly pts0 = m.face_polygon(f);
            Poly pts;
            for (auto &p : pts0) pts.push_back({p[0] + org[0], p[1] + org[1], p[2] + org[2]});
            int side = f.side ? 1 : 0;
            // push down the world tree
            std::vector<std::pair<int, Poly>> stack = {{models[0].headnode[0], pts}};
            Poly fr, bk;
            while (!stack.empty()) {
                auto [node, poly] = std::move(stack.back());
                stack.pop_back();
                if (node < 0) {
                    int leaf = -1 - node;
                    if (leafs.at(size_t(leaf)).contents != CONTENTS_SOLID) out.push_back({leaf, poly, fi, side, pts0});
                    continue;
                }
                const BspNode &n = m.nodes.at(size_t(node));
                const BspPlane &pl = planes.at(size_t(n.plane));
                split_poly(poly, pl.n, pl.d, fr, bk);
                if (!fr.empty()) stack.push_back({n.child[0], fr});
                if (!bk.empty()) stack.push_back({n.child[1], bk});
            }
        }
        return out;
    };

    // a mover whose every keyframe costs many fragments gets its keyframes
    // further apart (up to 64 units; the renderer shifts by the rest)
    const int FRAG_BUDGET = 300;
    for (auto &mv : movers) {
        size_t per_key = clip_model(mv.model, mv.keys.at(0)).size();
        int step = KEY_STEP;
        while (step < 64 && per_key * keys_along(mv.pos, step).size() > size_t(FRAG_BUDGET)) step *= 2;
        mv.step = step;
        mv.keys = keys_along(mv.pos, step);
    }

    // movers drawn as models (their own flat-coloured model, no fragments)
    std::map<int, FlatModel> mover_model;
    for (int mi : E.mover_models) {
        if (mi < 0 || mi >= int(movers.size())) continue;
        const BspModel &mo = models.at(size_t(movers[size_t(mi)].model));
        FlatModel fm;
        std::vector<std::pair<Vec3, int>> verts;
        Bytes cols;
        for (int fi = mo.firstface; fi < mo.firstface + mo.numfaces; fi++) {
            const BspFace &f = bspfaces.at(size_t(fi));
            const BspTexture *tex = m.face_texture(f);
            if (!tex || tex->name == "trigger" || tex->name == "clip") continue;
            Poly pts = m.face_polygon(f);
            int flags = tex_flags(f.texinfo) | (f.side ? 1 : 0);
            auto [lofs, avgL] = build_light(fi, pts);
            if (lofs < 0 && !(flags & 6)) flags |= 8;
            int row, avgc, lit, unlit;
            bool exact;
            face_colours(fi, flags, avgL, row, avgc, lit, unlit, exact, true);
            const BspPlane &pl = planes[size_t(f.plane)];
            Vec3 n = f.side ? Vec3{-pl.n[0], -pl.n[1], -pl.n[2]} : Vec3{pl.n[0], pl.n[1], pl.n[2]};
            int best = 0;
            double bd = -2;
            for (int k = 0; k < 162; k++) {
                double d = ANORMS[k][0] * n[0] + ANORMS[k][1] * n[1] + ANORMS[k][2] * n[2];
                if (d > bd) {
                    bd = d;
                    best = k;
                }
            }
            int base = int(verts.size());
            for (auto &p : pts) verts.push_back({p, best});
            for (int k = 1; k + 1 < int(pts.size()); k++) {
                fm.tris.push_back({base, base + k + 1, base + k});
                cols.push_back(uint8_t(avgc));   // (the game lights models itself)
            }
        }
        if (verts.size() > 256 || verts.empty()) {
            warn("mover " + std::to_string(mi) + " (*" + std::to_string(movers[size_t(mi)].model) +
                 ") has too many vertices to draw as a model; kept as level faces");
            continue;
        }
        double mn[3], mx[3];
        for (int k = 0; k < 3; k++) {
            mn[k] = 1e300;
            mx[k] = -1e300;
            for (auto &v : verts) {
                mn[k] = std::min(mn[k], v.first[size_t(k)]);
                mx[k] = std::max(mx[k], v.first[size_t(k)]);
            }
            fm.scale[k] = std::max(1e-3, (mx[k] - mn[k]) / 255.0);
            fm.origin[k] = mn[k];
        }
        Bytes frame;
        for (auto &v : verts) {
            for (int k = 0; k < 3; k++)
                frame.push_back(uint8_t(std::clamp<int64_t>(pyround((v.first[size_t(k)] - mn[k]) / fm.scale[k]), 0, 255)));
            frame.push_back(uint8_t(v.second));
        }
        double r2 = 0;
        for (int k = 0; k < 3; k++) {
            double v = std::max(std::fabs(mn[k]), std::fabs(mx[k]));
            r2 += v * v;
        }
        fm.radius = std::sqrt(r2);
        fm.flags = 0;
        fm.nverts = int(verts.size());
        fm.frames = {frame};
        fm.colours = {cols};
        mover_model[mi] = fm;
    }

    struct Group {
        int model;
        Vec3 off;
        int mover, key;
    };
    std::vector<Group> groups;
    for (size_t mi = 0; mi < movers.size(); mi++) {
        if (mover_model.count(int(mi))) continue;
        for (size_t k = 0; k < movers[mi].keys.size(); k++)
            groups.push_back({movers[mi].model, movers[mi].keys[k], int(mi), int(k)});
    }
    for (auto &e : ents) {
        std::string mdl = e.get("model");
        std::string cls = e.get("classname");
        if (std::find(STATIC_CLASSES.begin(), STATIC_CLASSES.end(), cls) != STATIC_CLASSES.end() &&
            starts_with(mdl, "*") && present(e))
            groups.push_back({std::stoi(mdl.substr(1)), {0.0, 0.0, 0.0}, -1, 0});
    }
    check(movers.size() < 255, "too many movers");
    for (auto &mv : movers) check(mv.keys.size() < 256, "a mover with too many keyframes");
    std::map<int, std::vector<int>> leaf_frags;
    int nfrag = 0;
    std::map<int, int> per_mover;

    // faces pressed against another solid face (the teeth where a split
    // door's halves meet, a door's edges against its frame, a lift's sides
    // against the shaft) can never be seen; drawn, they show through the
    // door in front of them (the renderer does not order the fragments of
    // one leaf), so they are left out. Against: the world's faces, the
    // static brush models, and the movers that move with this one, at the
    // same keyframe.
    struct Solid {
        Vec3 n;
        double d;
        Poly poly;
    };
    auto face_solid = [&](int fi, const Vec3 &off) {
        const BspFace &f = bspfaces.at(size_t(fi));
        const BspPlane &pl = planes.at(size_t(f.plane));
        Solid sd;
        sd.n = f.side ? Vec3{-pl.n[0], -pl.n[1], -pl.n[2]} : Vec3{pl.n[0], pl.n[1], pl.n[2]};
        sd.poly = m.face_polygon(f);
        for (auto &p : sd.poly) p = add3(p, off);
        sd.d = dot(sd.n, sd.poly.at(0));
        return sd;
    };
    auto covered = [&](const Solid &a, const std::vector<Solid> &against) {
        for (auto &b : against) {
            if (dot(a.n, b.n) > -0.999 || std::fabs(a.d + b.d) > 0.5) continue;
            // a's corners all within b (b's edges, with half a unit to spare)
            bool all = true;
            Vec3 c = winding_centre(b.poly);
            for (auto &p : a.poly) {
                for (size_t i = 0; i < b.poly.size() && all; i++) {
                    const Vec3 &u = b.poly[i], &v = b.poly[(i + 1) % b.poly.size()];
                    Vec3 en = normalize(cross(sub(v, u), b.n));
                    if (dot(en, sub(c, u)) > 0) en = scale3(en, -1);   // (outwards, whichever way b winds)
                    if (dot(en, sub(p, u)) > 0.5) all = false;
                }
                if (!all) break;
            }
            if (all) return true;
        }
        return false;
    };
    std::vector<Solid> world_solids;
    for (int fi = 0; fi < models[0].numfaces; fi++) {
        int fl = tex_flags(bspfaces[size_t(fi)].texinfo);
        if (fl & 6) continue;   // (not liquids or sky)
        world_solids.push_back(face_solid(fi, {0.0, 0.0, 0.0}));
    }
    auto model_solids = [&](int mi, const Vec3 &off) {
        std::vector<Solid> out;
        const BspModel &mo = models.at(size_t(mi));
        for (int fi = mo.firstface; fi < mo.firstface + mo.numfaces; fi++) {
            const BspTexture *tex = m.face_texture(bspfaces.at(size_t(fi)));
            if (!tex || tex->name == "trigger" || tex->name == "clip") continue;
            out.push_back(face_solid(fi, off));
        }
        return out;
    };
    std::vector<Solid> static_solids;
    for (auto &g : groups)
        if (g.mover < 0) {
            auto ss = model_solids(g.model, g.off);
            static_solids.insert(static_solids.end(), ss.begin(), ss.end());
        }
    int hidden_faces = 0;
    for (auto &g : groups) {
        std::set<int> hidden;
        {
            std::vector<Solid> against = static_solids;
            for (auto &h : groups) {
                if (&h == &g || h.mover < 0 || g.mover < 0) continue;
                const Mover &a = movers[size_t(g.mover)], &b = movers[size_t(h.mover)];
                bool together = h.mover != g.mover && a.group >= 0 && a.group == b.group && h.key == g.key;
                if (together) {
                    auto ss = model_solids(h.model, h.off);
                    against.insert(against.end(), ss.begin(), ss.end());
                }
            }
            if (g.mover < 0) against.clear();   // (static models: against the world only)
            const BspModel &mo = models.at(size_t(g.model));
            for (int fi = mo.firstface; fi < mo.firstface + mo.numfaces; fi++) {
                Solid a = face_solid(fi, g.off);
                if (covered(a, world_solids) || covered(a, against)) hidden.insert(fi);
            }
        }
        hidden_faces += int(hidden.size());
        if (std::getenv("Q32X_DEBUG_HIDDEN") && g.mover >= 0) {
            std::string l = "  mover " + std::to_string(g.mover) + " key " + std::to_string(g.key) + " *" + std::to_string(g.model) + " hidden:";
            for (int h : hidden) l += " " + std::to_string(h);
            say(l);
        }
        for (auto &fr : clip_model(g.model, g.off)) {
            if (hidden.count(fr.fi)) continue;
            const FaceEdit *e = fedit(fr.fi);
            if (e && e->remove) continue;
            int idx = add_face(fr.fi, fr.poly, fr.pts0, fr.side, false, g.mover, g.key, nullptr);
            if (idx >= 0) {
                // texture space and the plane move with the model: adjusted
                // copies per offset are made below
                face_recs[size_t(idx)].has_org = true;
                face_recs[size_t(idx)].org = g.off;
                leaf_frags[fr.leaf].push_back(idx);
                nfrag++;
                per_mover[g.mover]++;
            }
        }
    }
    say("bmodel fragments: " + std::to_string(nfrag) + " (faces pressed against others left out: " +
        std::to_string(hidden_faces) + ")");
    check(face_recs.size() <= size_t(MAX_LEVEL_FACES), "more faces than MAX_MAP_FACES in src/r_local.h");
    {
        std::vector<std::pair<int, int>> pm(per_mover.begin(), per_mover.end());
        std::stable_sort(pm.begin(), pm.end(), [](auto &a, auto &b) { return a.second > b.second; });
        for (size_t i = 0; i < pm.size() && i < 6; i++)
            if (pm[i].first >= 0) {
                auto &mv = movers[size_t(pm[i].first)];
                say("  mover " + std::to_string(pm[i].first) + " (" + mv.cls + " *" + std::to_string(mv.model) +
                    "): " + std::to_string(pm[i].second) + " fragments, " + std::to_string(mv.keys.size()) +
                    " keyframes " + std::to_string(mv.step) + " apart");
            }
    }

    // texinfo for fragments: texture coords are relative to the model's own
    // coordinates; for moved models (plats) adjusted texinfos are made.
    std::vector<BspTexinfo> out_texinfo = texinfo;
    std::map<std::tuple<int, double, double, double>, int> ti_cache, pl_cache;
    for (auto &rec : face_recs) {
        if (!rec.has_org) continue;
        const Vec3 &org = rec.org;
        if (org[0] == 0 && org[1] == 0 && org[2] == 0) continue;
        auto key = std::make_tuple(rec.texinfo, org[0], org[1], org[2]);
        auto it = ti_cache.find(key);
        if (it == ti_cache.end()) {
            BspTexinfo t = texinfo.at(size_t(rec.texinfo));
            for (int j = 0; j < 2; j++)
                t.vecs[j * 4 + 3] -= t.vecs[j * 4] * org[0] + t.vecs[j * 4 + 1] * org[1] + t.vecs[j * 4 + 2] * org[2];
            it = ti_cache.emplace(key, int(out_texinfo.size())).first;
            out_texinfo.push_back(t);
        }
        rec.texinfo = it->second;
        // the plane moves with the model as well
        auto pkey = std::make_tuple(rec.plane, org[0], org[1], org[2]);
        auto pit = pl_cache.find(pkey);
        if (pit == pl_cache.end()) {
            BspPlane p = planes.at(size_t(rec.plane));
            p.d = p.d + (p.n[0] * org[0] + p.n[1] * org[1] + p.n[2] * org[2]);
            planes.push_back(p);
            pit = pl_cache.emplace(pkey, int(planes.size()) - 1).first;
        }
        rec.plane = pit->second;
    }

    // ------------------------------------------------ node / leaf parents
    std::vector<int> nparent(nodes.size(), -1), lparent(leafs.size(), -1);
    for (size_t i = 0; i < nodes.size(); i++)
        for (int c : nodes[i].child) {
            if (c >= 0)
                nparent.at(size_t(c)) = int(i);
            else
                lparent.at(size_t(-1 - c)) = int(i);
        }

    // reorder each node's faces: front-facing (same side as the plane) first,
    // so the BSP walk can pick a side's faces without reading face records
    std::vector<int> perm(face_recs.size());
    for (size_t i = 0; i < perm.size(); i++) perm[i] = int(i);
    std::vector<int> numfront(nodes.size(), 0);
    {
        std::vector<FaceRec> new_recs = face_recs;
        std::vector<Plan> new_plan = plan;
        for (size_t ni = 0; ni < nodes.size(); ni++) {
            int ff = nodes[ni].firstface, nf = nodes[ni].numfaces;
            if (ff + nf > nworld) continue;   // submodel node (never walked)
            std::vector<int> order;
            for (int i = ff; i < ff + nf; i++)
                if (!(face_recs[size_t(i)].flags & 1)) order.push_back(i);
            numfront[ni] = int(order.size());
            check(order.size() < 256, "a node with 256 or more front faces");
            for (int i = ff; i < ff + nf; i++)
                if (face_recs[size_t(i)].flags & 1) order.push_back(i);
            for (size_t k = 0; k < order.size(); k++) {
                perm[size_t(order[k])] = ff + int(k);
                new_recs[size_t(ff) + k] = face_recs[size_t(order[k])];
                new_plan[size_t(ff) + k] = plan[size_t(order[k])];
            }
        }
        face_recs = new_recs;
        plan = new_plan;
    }
    for (auto &v : face_map) v = perm[size_t(v)];

    // ------------------------------------------------ light to polygons
    // Each face's light map as hard light and shadow regions (bands), and the
    // regions as polygons for flat shading: a face with several bands is drawn
    // flat as its pieces.
    std::vector<std::vector<std::pair<std::vector<int>, int>>> pieces(static_cast<size_t>(nworld));   // (vl, row)
    std::map<int, std::vector<uint8_t>> bandmaps;
    std::vector<int> band_rows;
    int npieces = 0;
    if (E.light.enabled || (USH && FP.levels >= 2)) {
        std::set<int> off_faces;
        for (int l : E.light.off_leafs) {
            if (l < 0 || l >= int(leafs.size())) continue;
            const BspLeaf &lf = leafs[size_t(l)];
            for (int k = lf.firstmark; k < lf.firstmark + lf.nummark && k < int(m.marksurfaces.size()); k++)
                off_faces.insert(m.marksurfaces[size_t(k)]);
        }
        std::vector<LightFace> lfs(static_cast<size_t>(nworld));
        std::vector<bool> cand(static_cast<size_t>(nworld), false);
        for (int i = 0; i < nworld; i++) {
            FaceRec &r = face_recs[size_t(i)];
            const Plan &p = plan[size_t(i)];
            int fi = p.members[0];
            const FaceEdit *e = fedit(fi);
            if (r.vl.size() < 3 || (r.flags & (NOMERGE | (USH ? 0 : 8))) || r.exact || (e && (e->lock || e->no_split)) ||
                off_faces.count(fi) || !(USH ? !p.syn.empty() : lightable(fi)))
                continue;
            RawLight L;
            if (USH) {
                L.ok = true;
                L.L = p.syn;
                L.smax = p.syn_smax;
                L.tmax = p.syn_tmax;
                L.tmins[0] = p.syn_tmins[0];
                L.tmins[1] = p.syn_tmins[1];
            } else
                L = p.merged ? p.light : raw_light(fi, p.poly);
            if (!L.ok) continue;
            LightFace &lf = lfs[size_t(i)];
            lf.L = L.L;
            lf.smax = int(L.smax);
            lf.tmax = int(L.tmax);
            lf.tmins[0] = L.tmins[0];
            lf.tmins[1] = L.tmins[1];
            const BspTexinfo &ti = texinfo.at(size_t(bspfaces[size_t(fi)].texinfo));
            std::copy(ti.vecs, ti.vecs + 8, lf.vecs);
            const BspPlane &pl = planes.at(size_t(bspfaces[size_t(fi)].plane));
            lf.n = {pl.n[0], pl.n[1], pl.n[2]};
            lf.d = pl.d;
            lf.poly = p.poly;
            if (e && e->band >= 0) lf.forced_band = e->band;
            cand[size_t(i)] = true;
        }
        std::vector<const LightFace *> all;
        for (int i = 0; i < nworld; i++)
            if (cand[size_t(i)]) all.push_back(&lfs[size_t(i)]);
        BandSet bands = choose_bands(all, E.light.enabled ? E.light.bands : std::clamp(FP.levels, 2, 8), E.light.contrast);
        if (E.light.hard_shadows) {
            // the brightest band at the lit row, each darker one a step darker
            std::vector<size_t> ord(bands.rows.size());
            for (size_t k = 0; k < ord.size(); k++) ord[k] = k;
            std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return bands.rows[a] < bands.rows[b]; });
            for (size_t k = 0; k < ord.size(); k++)
                bands.rows[ord[k]] = std::clamp(E.light.lit_row + int(k) * E.light.shadow_step, 0, 63);
        }
        band_rows = bands.rows;
        auto set_row = [&](FaceRec &r, int row) {
            r.row = row;
            if (!r.exact && !(r.flags & 2)) r.lit = fcm(UP, row, r.avgc);
        };
        // the pieces must fit beside the faces the level has (the renderer's
        // face limit), with room for the flat-mode merges
        int room = MAX_LEVEL_FACES - int(face_recs.size()) - 800;
        int budget_left = std::max(0, E.light.budget > 0 ? std::min(E.light.budget, room) : room);
        for (int i = 0; i < nworld; i++) {
            if (!cand[size_t(i)]) continue;
            FaceRec &r = face_recs[size_t(i)];
            LightPieces lp = light_pieces(lfs[size_t(i)], bands, E.light.min_area);
            bandmaps[plan[size_t(i)].members[0]] = lp.bandmap;
            if (lp.uniform >= 0) {
                set_row(r, bands.rows[size_t(lp.uniform)]);
                continue;
            }
            // the pieces of each band merged into as few convex polygons as will go
            // (vertices numbered locally until the face's pieces are kept)
            std::map<VKey, int> lmap;
            std::vector<Vec3> lpos;
            std::vector<std::array<int, 4>> lverts;
            auto lindex = [&](const Vec3 &p) {
                VKey k = {v13_3(p[0]), v13_3(p[1]), v13_3(p[2])};
                auto it = lmap.find(k);
                if (it != lmap.end()) return it->second;
                int idx = int(lverts.size());
                lmap[k] = idx;
                lverts.push_back({k[0], k[1], k[2], 0});
                lpos.push_back(p);
                return idx;
            };
            std::vector<std::pair<std::vector<int>, int>> polys;
            for (auto &pc : lp.pieces) {
                std::vector<int> vl;
                for (auto &p : pc.first) {
                    int vi = lindex(p);
                    if (vl.empty() || vl.back() != vi) vl.push_back(vi);
                }
                if (vl.size() > 1 && vl.front() == vl.back()) vl.pop_back();
                std::set<int> u(vl.begin(), vl.end());
                if (vl.size() < 3 || u.size() != vl.size()) continue;
                polys.push_back({vl, pc.second});
            }
            const BspPlane &pl = planes.at(size_t(r.plane));
            int kk = 0;
            for (int k = 1; k < 3; k++)
                if (std::fabs(pl.n[k]) > std::fabs(pl.n[kk])) kk = k;
            int ax[2], j2 = 0;
            for (int k = 0; k < 3; k++)
                if (k != kk) ax[j2++] = k;
            auto merge = [&](const std::vector<int> &A, const std::vector<int> &B, std::vector<int> &outv) {
                size_t na = A.size(), nb = B.size();
                for (size_t a = 0; a < na; a++) {
                    int a0 = A[a], a1 = A[(a + 1) % na];
                    for (size_t b = 0; b < nb; b++) {
                        if (B[b] != a1 || B[(b + 1) % nb] != a0) continue;
                        std::vector<int> U;
                        for (size_t k = 0; k < na; k++) U.push_back(A[(a + 1 + k) % na]);
                        for (size_t k = 0; k + 2 < nb; k++) U.push_back(B[(b + 2 + k) % nb]);
                        std::set<int> uu(U.begin(), U.end());
                        if (uu.size() != U.size()) return false;
                        std::vector<std::pair<int, bool>> keep;
                        size_t n = U.size();
                        for (size_t k = 0; k < n; k++) {
                            auto &p0 = lverts[size_t(U[(k + n - 1) % n])], &p1 = lverts[size_t(U[k])],
                                 &p2 = lverts[size_t(U[(k + 1) % n])];
                            int64_t c = int64_t(p1[size_t(ax[0])] - p0[size_t(ax[0])]) * (p2[size_t(ax[1])] - p1[size_t(ax[1])]) -
                                        int64_t(p1[size_t(ax[1])] - p0[size_t(ax[1])]) * (p2[size_t(ax[0])] - p1[size_t(ax[0])]);
                            if (c != 0) keep.push_back({U[k], c > 0});
                        }
                        if (keep.size() < 3 || keep.size() > size_t(MAX_LEVEL_FLAT_PIECES)) return false;
                        for (auto &kp : keep)
                            if (kp.second != keep[0].second) return false;
                        outv.clear();
                        for (auto &kp : keep) outv.push_back(kp.first);
                        return true;
                    }
                }
                return false;
            };
            bool changed = true;
            std::vector<int> u;
            while (changed) {
                changed = false;
                for (size_t x = 0; x < polys.size(); x++)
                    for (size_t y = x + 1; y < polys.size(); y++) {
                        if (polys[x].second != polys[y].second) continue;
                        if (merge(polys[x].first, polys[y].first, u)) {
                            polys[x].first = u;
                            polys.erase(polys.begin() + std::ptrdiff_t(y));
                            y--;
                            changed = true;
                        }
                    }
            }
            int extra = int(polys.size());
            if (polys.size() < 2 || int(polys.size()) > E.light.max_pieces || extra > budget_left) {
                // left whole, in its most common band
                std::vector<int> cnt(bands.rows.size(), 0);
                for (auto b : lp.bandmap) cnt[b]++;
                int best = int(std::max_element(cnt.begin(), cnt.end()) - cnt.begin());
                set_row(r, bands.rows[size_t(best)]);
                continue;
            }
            budget_left -= extra;
            for (auto &p : polys) {
                std::vector<int> vl;
                for (int lv : p.first) vl.push_back(vindex(lpos[size_t(lv)], -1, 0));
                pieces[size_t(i)].push_back({vl, bands.rows[size_t(p.second)]});
            }
            npieces += int(polys.size());
        }
        say("light to polygons: " + std::to_string(bands.rows.size()) + " bands, " + std::to_string(npieces) +
            " pieces");
    }

    // ------------------------------------------------ flat-shaded face sets
    // Flat shading fills a face with one colour, so neighbouring faces of a
    // node side with the same colours (lit and unlit) can be drawn as one
    // polygon where their union is convex: fewer faces, the same picture.
    // Merged faces are new records after the others; flatof links each face
    // to the merged face it is part of (the renderer marks both visible);
    // flatnode/flatlist give each node side's faces in flat mode.
    const size_t MERGE_MAXV = 16;          // room for the clipper (MAXCLIPVERTS 24)
    const int FLAT_TOL = opt.flat_tol;

    int ax[2] = {0, 1};
    auto area2 = [&](const std::vector<int> &vl) -> int64_t {
        int64_t s = 0;
        size_t n = vl.size();
        for (size_t k = 0; k < n; k++) {
            auto &a = out_verts[size_t(vl[(k + n - 1) % n])];
            auto &b = out_verts[size_t(vl[k])];
            s += int64_t(a[size_t(ax[0])]) * b[size_t(ax[1])] - int64_t(b[size_t(ax[0])]) * a[size_t(ax[1])];
        }
        s = s < 0 ? -s : s;
        return s ? s : 1;
    };
    const size_t nface_tex = face_recs.size();
    std::vector<int> flatof(nface_tex, 0xFFFF);
    std::vector<std::vector<int>> flatref(nface_tex);
    struct FlatNode {
        int first, nfront, nback;
    };
    std::vector<FlatNode> flatnode;
    std::vector<int> flatlist;

    auto merge_two = [&](const std::vector<int> &A, const std::vector<int> &B, std::vector<int> &outv) -> bool {
        size_t na = A.size(), nb = B.size();
        std::map<int, size_t> pos;
        for (size_t k = 0; k < nb; k++) pos[B[k]] = k;
        for (size_t i = 0; i < na; i++) {
            int a0 = A[i], a1 = A[(i + 1) % na];
            auto it = pos.find(a1);
            if (it == pos.end() || B[(it->second + 1) % nb] != a0) continue;
            size_t j = it->second;
            std::vector<int> U;
            for (size_t k = 0; k < na; k++) U.push_back(A[(i + 1 + k) % na]);
            for (size_t k = 0; k + 2 < nb; k++) U.push_back(B[(j + 2 + k) % nb]);
            std::set<int> uniq(U.begin(), U.end());
            if (uniq.size() != U.size()) return false;
            size_t n = U.size();
            std::vector<std::pair<int64_t, int64_t>> P;
            for (int v : U) P.push_back({out_verts[size_t(v)][size_t(ax[0])], out_verts[size_t(v)][size_t(ax[1])]});
            // drop collinear points, then all turns must agree
            std::vector<std::pair<int, bool>> keep;
            for (size_t k = 0; k < n; k++) {
                auto p0 = P[(k + n - 1) % n], p1 = P[k], p2 = P[(k + 1) % n];
                int64_t c = (p1.first - p0.first) * (p2.second - p1.second) -
                            (p1.second - p0.second) * (p2.first - p1.first);
                if (c != 0) keep.push_back({U[k], c > 0});
            }
            if (keep.size() < 3 || keep.size() > MERGE_MAXV) return false;
            for (auto &kp : keep)
                if (kp.second != keep[0].second) return false;
            outv.clear();
            for (auto &kp : keep) outv.push_back(kp.first);
            return true;
        }
        return false;
    };

    struct MPoly {
        std::vector<int> vl;
        std::vector<int> members;
        bool has_key;
        std::tuple<int, int, int, int> key;
        int lo, hi;
        bool alive = true;
        bool piece = false;      // its shape is not a face's own
        int64_t wsum = 0, wrow = 0;
    };
    int64_t flat_world_before = 0;
    for (size_t ni = 0; ni < nodes.size(); ni++) {
        int ff = nodes[ni].firstface, nf = nodes[ni].numfaces;
        if (ff + nf > nworld) {
            flatnode.push_back({int(flatlist.size()), 0, 0});   // a submodel's node: never walked
            continue;
        }
        flat_world_before += nf;
        std::vector<int> sides[2];
        for (int sd = 0; sd < 2; sd++) {
            int a = sd == 0 ? ff : ff + numfront[ni];
            int b = sd == 0 ? ff + numfront[ni] : ff + nf;
            std::vector<MPoly> polys;
            for (int i = a; i < b; i++) {
                const FaceRec &r = face_recs[size_t(i)];
                if (!pieces[size_t(i)].empty()) {
                    for (auto &pc : pieces[size_t(i)]) {
                        MPoly p;
                        p.vl = pc.first;
                        p.members = {i};
                        p.has_key = true;
                        p.key = std::make_tuple(r.avgc, r.unlit, r.flags, r.plane);
                        p.lo = p.hi = pc.second;
                        p.piece = true;
                        polys.push_back(p);
                    }
                    continue;
                }
                MPoly p;
                p.vl = r.vl;
                p.members = {i};
                p.has_key = !(r.flags & NOMERGE) && r.vl.size() >= 3;
                p.key = std::make_tuple(r.avgc, r.unlit, r.flags, r.plane);
                p.lo = p.hi = r.row;
                polys.push_back(p);
            }
            if (!polys.empty()) {
                const BspPlane &pl = planes.at(size_t(face_recs[size_t(polys[0].members[0])].plane));
                int kk = 0;
                for (int k = 1; k < 3; k++)
                    if (std::fabs(pl.n[k]) > std::fabs(pl.n[kk])) kk = k;
                int j = 0;
                for (int k = 0; k < 3; k++)
                    if (k != kk) ax[j++] = k;
            }
            for (auto &p : polys) {
                p.wsum = area2(p.vl);
                p.wrow = p.wsum * p.lo;
            }
            bool changed = true;
            std::vector<int> u;
            while (changed) {
                changed = false;
                for (size_t x = 0; x < polys.size(); x++) {
                    if (!polys[x].alive || !polys[x].has_key) continue;
                    for (size_t y = x + 1; y < polys.size(); y++) {
                        if (!polys[y].alive || !polys[y].has_key || polys[y].key != polys[x].key) continue;
                        int lo = std::min(polys[x].lo, polys[y].lo), hi = std::max(polys[x].hi, polys[y].hi);
                        if (hi - lo > FLAT_TOL) continue;
                        if (merge_two(polys[x].vl, polys[y].vl, u)) {
                            polys[x].vl = u;
                            polys[x].members.insert(polys[x].members.end(), polys[y].members.begin(),
                                                    polys[y].members.end());
                            polys[x].lo = lo;
                            polys[x].hi = hi;
                            polys[x].piece = polys[x].piece || polys[y].piece;
                            polys[x].wsum += polys[y].wsum;
                            polys[x].wrow += polys[y].wrow;
                            polys[y].alive = false;
                            changed = true;
                        }
                    }
                }
                std::vector<MPoly> keep;
                for (auto &p : polys)
                    if (p.alive) keep.push_back(std::move(p));
                polys = std::move(keep);
            }
            for (auto &p : polys) {
                if (p.members.size() == 1 && !p.piece) {
                    sides[sd].push_back(p.members[0]);
                    continue;
                }
                FaceRec rec = face_recs[size_t(p.members[0])];
                rec.vl = p.vl;
                rec.tag = 0;
                if (!(rec.flags & 8) || USH) {   // (dark faces stay dark)
                    rec.row = int(pyround(double(p.wrow) / double(p.wsum)));
                    if (!rec.exact && !(rec.flags & 2)) rec.lit = fcm(UP, rec.row, rec.avgc);
                }
                face_recs.push_back(rec);
                int g = int(face_recs.size()) - 1;
                std::set<int> seen;
                for (int mm : p.members) {
                    if (flatof[size_t(mm)] == 0xFFFF) flatof[size_t(mm)] = g;
                    if (seen.insert(mm).second) flatref[size_t(mm)].push_back(g);
                }
                sides[sd].push_back(g);
            }
        }
        check(sides[0].size() < 256 && sides[1].size() < 256, "a node side with 256 or more flat faces");
        flatnode.push_back({int(flatlist.size()), int(sides[0].size()), int(sides[1].size())});
        flatlist.insert(flatlist.end(), sides[0].begin(), sides[0].end());
        flatlist.insert(flatlist.end(), sides[1].begin(), sides[1].end());
    }
    flatof.resize(face_recs.size(), 0xFFFF);
    int nmerged = 0;
    for (size_t i = 0; i < nface_tex; i++)
        if (flatof[i] != 0xFFFF) nmerged++;
    say("flat faces: " + std::to_string(nmerged) + " faces of " + std::to_string(nworld) + " merged into " +
        std::to_string(face_recs.size() - nface_tex) + " (world faces drawn flat: " + std::to_string(flatlist.size()) +
        " instead of " + std::to_string(flat_world_before) + ")");
    check(face_recs.size() <= size_t(MAX_LEVEL_FACES), "more faces than MAX_MAP_FACES in src/r_local.h");

    // ------------------------------------------------ T-junctions
    // Pieces put new points on face edges; the flat-mode polygons next to them
    // get those points too, so no pixel gaps open along the edges.
    if (npieces && E.reduce.fix_tjunctions) {
        std::set<int> flat_set(flatlist.begin(), flatlist.end());
        std::vector<int> drawn(flat_set.begin(), flat_set.end());
        // the points pieces added: grid-hashed
        std::set<int> piece_pts;
        for (size_t g = nface_tex; g < face_recs.size(); g++)
            for (int v : face_recs[g].vl) piece_pts.insert(v);
        std::map<std::array<int, 3>, std::vector<int>> grid;
        auto cellof = [&](int v) {
            auto &p = out_verts[size_t(v)];
            return std::array<int, 3>{p[0] >> 6, p[1] >> 6, p[2] >> 6};
        };
        for (int v : piece_pts)
            if (out_verts[size_t(v)][3] == 0) grid[cellof(v)].push_back(v);
        int added = 0;
        for (int g : drawn) {
            FaceRec &r = face_recs[size_t(g)];
            if (r.tag || r.vl.size() < 3) continue;
            std::vector<int> outv;
            size_t n = r.vl.size();
            for (size_t k = 0; k < n; k++) {
                int a = r.vl[k], b = r.vl[(k + 1) % n];
                outv.push_back(a);
                auto &pa = out_verts[size_t(a)], &pb = out_verts[size_t(b)];
                double ex = pb[0] - pa[0], ey = pb[1] - pa[1], ez = pb[2] - pa[2];
                double len2 = ex * ex + ey * ey + ez * ez;
                if (len2 < 4) continue;
                std::vector<std::pair<double, int>> on;
                int lo[3], hi[3];
                for (int c = 0; c < 3; c++) {
                    lo[c] = std::min(pa[size_t(c)], pb[size_t(c)]) >> 6;
                    hi[c] = std::max(pa[size_t(c)], pb[size_t(c)]) >> 6;
                }
                if ((hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1) > 4096) continue;
                for (int x = lo[0]; x <= hi[0]; x++)
                    for (int y = lo[1]; y <= hi[1]; y++)
                        for (int z = lo[2]; z <= hi[2]; z++) {
                            auto it = grid.find({x, y, z});
                            if (it == grid.end()) continue;
                            for (int v : it->second) {
                                if (v == a || v == b) continue;
                                auto &pv = out_verts[size_t(v)];
                                double vx = pv[0] - pa[0], vy = pv[1] - pa[1], vz = pv[2] - pa[2];
                                double t = (vx * ex + vy * ey + vz * ez) / len2;
                                if (t <= 0.001 || t >= 0.999) continue;
                                double dx = vx - t * ex, dy = vy - t * ey, dz = vz - t * ez;
                                if (dx * dx + dy * dy + dz * dz > 0.36) continue;   // 0.6 of an eighth
                                on.push_back({t, v});
                            }
                        }
                std::sort(on.begin(), on.end());
                for (auto &o : on)
                    if (std::find(outv.begin(), outv.end(), o.second) == outv.end() &&
                        std::find(r.vl.begin(), r.vl.end(), o.second) == r.vl.end())
                        outv.push_back(o.second);
            }
            if (outv.size() != r.vl.size() && outv.size() <= 18) {
                added += int(outv.size() - r.vl.size());
                r.vl = outv;
            }
        }
        say("T-junctions: " + std::to_string(added) + " points added");
    }

    // ------------------------------------------------ vertex order
    if (E.vertex_order) {
        std::vector<int> remap(out_verts.size(), -1);
        std::vector<std::array<int, 4>> nv;
        for (auto &r : face_recs)
            for (int &v : r.vl) {
                if (remap[size_t(v)] < 0) {
                    remap[size_t(v)] = int(nv.size());
                    nv.push_back(out_verts[size_t(v)]);
                }
                v = remap[size_t(v)];
            }
        say("vertex order: " + std::to_string(out_verts.size()) + " vertices -> " + std::to_string(nv.size()));
        out_verts = nv;
    }

    // ------------------------------------------------ door areas
    AreaMap areas;
    if (E.doors_block) {
        auto portals = build_portals(m);
        std::vector<int> excl(E.door_exclude.begin(), E.door_exclude.end());
        areas = build_areas(m, movers, portals, excl);
        if (areas.count > 127 || areas.doors.size() > 127) {
            warn("too many door areas (" + std::to_string(areas.count) + "): doors do not hide what is behind them");
            areas = AreaMap();
        } else {
            say("doors: " + std::to_string(areas.doors.size()) + " divide the level into " +
                std::to_string(areas.count) + " areas" +
                (areas.leaky.empty() ? "" : " (" + std::to_string(areas.leaky.size()) + " doors do not close off their openings)"));
        }
    }

    // ------------------------------------------------ light grid
    Bytes lightgrid;
    int grid_step = E.light_grid > 0 ? E.light_grid : (E.flat_only ? 64 : 0);
    if (grid_step > 0) {
        // Quake's light probe (the light map under a point) at every grid point
        const BspModel &w = models[0];
        int org[3], dims[3];
        for (int k = 0; k < 3; k++) {
            org[k] = int(std::floor(w.mins[k] / grid_step)) * grid_step;
            dims[k] = int(std::ceil((w.maxs[k] - org[k]) / grid_step)) + 1;
        }
        std::function<int(int, const Vec3 &, const Vec3 &)> probe = [&](int num, const Vec3 &start,
                                                                       const Vec3 &end) -> int {
            if (num < 0) return -1;
            const BspNode &n = nodes.at(size_t(num));
            const BspPlane &pl = planes.at(size_t(n.plane));
            double front = start[0] * pl.n[0] + start[1] * pl.n[1] + start[2] * pl.n[2] - pl.d;
            double back = end[0] * pl.n[0] + end[1] * pl.n[1] + end[2] * pl.n[2] - pl.d;
            int side = front < 0;
            if ((back < 0) == bool(side)) return probe(n.child[side], start, end);
            double frac = front / (front - back);
            Vec3 mid = {start[0] + (end[0] - start[0]) * frac, start[1] + (end[1] - start[1]) * frac,
                        start[2] + (end[2] - start[2]) * frac};
            int r = probe(n.child[side], start, mid);
            if (r >= 0) return r;
            for (int i = 0; i < n.numfaces; i++) {
                const FaceRec &f = face_recs.at(size_t(n.firstface + i));
                if (f.flags & 6) continue;
                const BspTexinfo &ti = out_texinfo.at(size_t(f.texinfo));
                double s = mid[0] * ti.vecs[0] + mid[1] * ti.vecs[1] + mid[2] * ti.vecs[2] + ti.vecs[3];
                double t = mid[0] * ti.vecs[4] + mid[1] * ti.vecs[5] + mid[2] * ti.vecs[6] + ti.vecs[7];
                int64_t ds = int64_t(std::floor(s)) - f.tmins[0], dt = int64_t(std::floor(t)) - f.tmins[1];
                if (ds < 0 || dt < 0 || ds > f.ext[0] || dt > f.ext[1]) continue;
                if (f.lofs < 0) return 128;
                return lightdata.at(size_t(f.lofs + (dt >> 4) * ((f.ext[0] >> 4) + 1) + (ds >> 4)));
            }
            return probe(n.child[!side], mid, end);
        };
        auto point_leaf = [&](const Vec3 &p) {
            int n = models[0].headnode[0];
            while (n >= 0) {
                const BspNode &nd = nodes.at(size_t(n));
                const BspPlane &pl = planes.at(size_t(nd.plane));
                n = nd.child[p[0] * pl.n[0] + p[1] * pl.n[1] + p[2] * pl.n[2] - pl.d < 0 ? 1 : 0];
            }
            return -1 - n;
        };
        size_t total = size_t(dims[0]) * size_t(dims[1]) * size_t(dims[2]);
        std::vector<int> val(total, -1);
        for (int z = 0; z < dims[2]; z++)
            for (int y = 0; y < dims[1]; y++)
                for (int x = 0; x < dims[0]; x++) {
                    Vec3 p = {double(org[0] + x * grid_step), double(org[1] + y * grid_step),
                              double(org[2] + z * grid_step) + 8};
                    if (!leaf_open(m, point_leaf(p))) continue;
                    int r = probe(models[0].headnode[0], p, {p[0], p[1], p[2] - 2048});
                    val[(size_t(z) * size_t(dims[1]) + size_t(y)) * size_t(dims[0]) + size_t(x)] = r < 0 ? 128 : r;
                }
        // points in solid space take the average of their open neighbours
        for (int pass = 0; pass < 4; pass++) {
            std::vector<int> nv = val;
            for (int z = 0; z < dims[2]; z++)
                for (int y = 0; y < dims[1]; y++)
                    for (int x = 0; x < dims[0]; x++) {
                        size_t o = (size_t(z) * size_t(dims[1]) + size_t(y)) * size_t(dims[0]) + size_t(x);
                        if (val[o] >= 0) continue;
                        int s = 0, c = 0;
                        for (int dz = -1; dz <= 1; dz++)
                            for (int dy = -1; dy <= 1; dy++)
                                for (int dx = -1; dx <= 1; dx++) {
                                    int xx = x + dx, yy = y + dy, zz = z + dz;
                                    if (xx < 0 || yy < 0 || zz < 0 || xx >= dims[0] || yy >= dims[1] || zz >= dims[2]) continue;
                                    int v = val[(size_t(zz) * size_t(dims[1]) + size_t(yy)) * size_t(dims[0]) + size_t(xx)];
                                    if (v >= 0) {
                                        s += v;
                                        c++;
                                    }
                                }
                        if (c) nv[o] = s / c;
                    }
            val = nv;
        }
        Buf g(big);
        for (int k = 0; k < 3; k++) g.i16(org[k]);
        g.u16(grid_step);
        for (int k = 0; k < 3; k++) g.u16(dims[k]);
        g.u16(0);
        for (int v : val) g.u8(v < 0 ? 128 : v);
        lightgrid = g.data();
    }

    // flat only: light maps are kept for the faces still drawn textured
    if (E.flat_only) {
        Bytes kept;
        std::map<int, int> moved;
        for (auto &r : face_recs) {
            if (r.lofs < 0) continue;
            if (!(r.flags & 64)) {
                r.lofs = -1;
                continue;
            }
            auto it = moved.find(r.lofs);
            if (it == moved.end()) {
                int64_t size = ((r.ext[0] >> 4) + 1) * ((r.ext[1] >> 4) + 1);
                int o = int(kept.size());
                kept.insert(kept.end(), lightdata.begin() + r.lofs, lightdata.begin() + r.lofs + size);
                while (kept.size() % 2) kept.push_back(0);
                it = moved.emplace(r.lofs, o).first;
            }
            r.lofs = it->second;
        }
        lightdata = kept;
    }

    // ------------------------------------------------ build blob
    Buf blob(big);
    blob.zeros(LEVEL_HDR_SIZE);
    std::map<std::string, Lump> L, X;

    {   // planes: nx ny nz dist (16.16)
        Buf b(big);
        for (auto &p : planes) {
            b.i32(fx(p.n[0]));
            b.i32(fx(p.n[1]));
            b.i32(fx(p.n[2]));
            b.i32(fx(p.d));
        }
        L["planes"] = {blob.add(b), planes.size()};
    }
    {   // nodes (32 bytes, 16-byte aligned so a node is exactly two cache lines)
        Buf b(big);
        for (size_t i = 0; i < nodes.size(); i++) {
            const BspNode &n = nodes[i];
            const BspPlane &pl = planes.at(size_t(n.plane));
            b.i16(n.child[0]);
            b.i16(n.child[1]);
            b.u16(n.firstface);
            b.u16(n.numfaces);
            for (int k = 0; k < 3; k++) b.i16(n.mins[k]);
            for (int k = 0; k < 3; k++) b.i16(n.maxs[k]);
            b.u8(pl.type < 3 ? pl.type : 3);
            b.u8(numfront[i]);
            for (int k = 0; k < 3; k++) b.i16(pyround(pl.n[k] * 16384));
            b.i32(fx(pl.d));
        }
        L["nodes"] = {blob.add(b, 16), nodes.size()};
        Buf p(big);
        for (size_t i = 0; i < nodes.size(); i++) p.i16(nparent[i]);
        L["nodeparent"] = {blob.add(p), nodes.size()};
    }
    // the leaves' faces (rewritten when faces were merged)
    std::vector<int> marks;
    std::vector<std::pair<int, int>> leafmarks;
    for (auto &lf : leafs) {
        if (!replanned) {
            leafmarks.push_back({lf.firstmark, lf.nummark});
            continue;
        }
        int first = int(marks.size());
        std::vector<int> seen;
        for (int k = lf.firstmark; k < lf.firstmark + lf.nummark && k < int(m.marksurfaces.size()); k++) {
            int ms = m.marksurfaces[size_t(k)];
            int f = ms < nbspworld ? face_map[size_t(bsp_to_plan[size_t(ms)])] : 0xFFFF;
            if (std::find(seen.begin(), seen.end(), f) != seen.end()) continue;
            seen.push_back(f);
            marks.push_back(f);
        }
        leafmarks.push_back({first, int(marks.size()) - first});
    }
    if (!replanned)
        for (int ms : m.marksurfaces) marks.push_back(ms < nbspworld ? face_map[size_t(ms)] : 0xFFFF);
    std::vector<int> fraglist;
    {   // leafs: 28 bytes
        Buf b(big);
        for (size_t i = 0; i < leafs.size(); i++) {
            const BspLeaf &lf = leafs[i];
            auto it = leaf_frags.find(int(i));
            size_t first = fraglist.size(), nfr = 0;
            if (it != leaf_frags.end()) {
                fraglist.insert(fraglist.end(), it->second.begin(), it->second.end());
                nfr = it->second.size();
            }
            b.i16(lf.contents);
            b.i16(lparent[i]);
            b.i32(lf.visofs);
            b.u16(leafmarks[i].first);
            b.u16(leafmarks[i].second);
            b.u16(int64_t(first));
            b.u16(int64_t(nfr));
            for (int k = 0; k < 3; k++) b.i16(lf.mins[k]);
            for (int k = 0; k < 3; k++) b.i16(lf.maxs[k]);
        }
        L["leafs"] = {blob.add(b), leafs.size()};
    }
    {
        Buf b(big);
        for (int ms : marks) b.u16(ms);
        L["marksurf"] = {blob.add(b), marks.size()};
        Buf f(big);
        for (int fi : fraglist) f.u16(fi);
        L["leaffrags"] = {blob.add(f), fraglist.size()};
    }
    std::vector<int> fverts;
    {   // faces (24 bytes)
        Buf b(big);
        for (auto &rec : face_recs) {
            size_t first = fverts.size();
            fverts.insert(fverts.end(), rec.vl.begin(), rec.vl.end());
            check(rec.vl.size() < 256, "a face with 256 or more vertices");
            check(rec.tmins[0] >= -32768 && rec.tmins[0] < 32768 && rec.tmins[1] >= -32768 && rec.tmins[1] < 32768,
                  "texture extents out of range");
            b.u16(int64_t(first));
            b.u8(int64_t(rec.vl.size()));
            b.u8(rec.flags);
            b.u16(rec.plane);
            b.u16(rec.texinfo);
            b.i16(rec.tmins[0]);
            b.i16(rec.tmins[1]);
            b.u16(rec.ext[0]);
            b.u16(rec.ext[1]);
            b.i32(rec.lofs);
            b.u8(rec.lit);
            b.u8(rec.unlit);
            b.u16(rec.tag);
        }
        L["faces"] = {blob.add(b), face_recs.size()};
        say("faces: " + std::to_string(face_recs.size()) + " world: " + std::to_string(nworld));
    }
    {   // compact per-face geometry record (16 bytes, one cache line)
        Buf b(big);
        size_t first = 0;
        for (auto &rec : face_recs) {
            size_t nv = rec.vl.size();
            int64_t sph[4] = {0, 0, 0, -1};
            if (!rec.vl.empty()) {
                int64_t ci[3];
                for (int k = 0; k < 3; k++) {
                    int lo = INT32_MAX, hi = INT32_MIN;
                    for (int v : rec.vl) {
                        lo = std::min(lo, out_verts[size_t(v)][size_t(k)]);
                        hi = std::max(hi, out_verts[size_t(v)][size_t(k)]);
                    }
                    ci[k] = pyround((double(lo) + double(hi)) / 2.0 / 8.0);
                }
                double r = 0;
                bool any = false;
                for (int v : rec.vl) {
                    double s = 0;
                    for (int k = 0; k < 3; k++) {
                        double d = out_verts[size_t(v)][size_t(k)] / 8.0 - double(ci[k]);
                        s += d * d;
                    }
                    double rr = std::sqrt(s);
                    if (!any || rr > r) r = rr;
                    any = true;
                }
                // a mover's fragment is drawn up to half a keyframe step away
                int64_t slack = rec.tag ? floordiv(movers.at(size_t((rec.tag & 0xFF) - 1)).step, 2) + 1 : 0;
                sph[0] = ci[0];
                sph[1] = ci[1];
                sph[2] = ci[2];
                sph[3] = pyceil(r) + 1 + slack;
            }
            b.u16(int64_t(first));
            b.u8(int64_t(nv));
            b.u8(rec.flags);
            b.u8(rec.lit);
            b.u8(rec.unlit);
            b.u16(rec.tag);
            for (auto v : sph) b.i16(v);
            first += nv;
        }
        L["facesph"] = {blob.add(b, 16), face_recs.size()};
    }
    {
        Buf b(big);
        for (int v : fverts) b.u16(v);
        L["fverts"] = {blob.add(b), fverts.size()};
        check(out_verts.size() < 65536, "too many vertices");
        Buf vb(big);
        for (auto &v : out_verts)
            for (int c : v) vb.i16(c);
        L["verts"] = {blob.add(vb), out_verts.size()};
    }
    {   // texinfo: s[4], t[4] (16.16), texture(H), flags(H) -> 36 bytes
        Buf b(big);
        for (auto &t : out_texinfo) {
            for (double v : t.vecs) b.i32(fx(v));
            b.u16(t.miptex);
            b.u16(t.flags & 0xFFFF);
        }
        L["texinfo"] = {blob.add(b), out_texinfo.size()};
    }
    size_t texture_bytes = 0;
    {   // textures: (w, h, avg, flags, pad, mipofs[4]) = 24 bytes, then the mips
        std::vector<bool> tex_used(textures.size(), !E.flat_only);
        for (size_t i = 0; i < textures.size(); i++)
            if (textures[i] && !keep_textured(textures[i]->name) && always_flat(textures[i]->name)) tex_used[i] = false;
        if (E.flat_only)
            for (auto &r : face_recs)
                if ((r.flags & 64) && !r.vl.empty())
                    tex_used[size_t(out_texinfo[size_t(r.texinfo)].miptex)] = true;
        Bytes texdata;
        Buf table(big);
        SharedLib *lib = opt.game.lib;
        std::vector<bool> in_lib(textures.size(), false);
        for (size_t i = 0; i < textures.size(); i++) {
            const auto &t = textures[i];
            if (!t) {
                table.zeros(24);
                continue;
            }
            int64_t mo[4] = {0, 0, 0, 0};
            int tflags = 0;
            // this texture's texels (mo: from their start), then in the
            // library or after the others
            Bytes texdata_all;
            std::swap(texdata, texdata_all);
            if (!tex_used[i]) {
                // (flat only: no texels; the mips point at nothing)
            } else if (reduced[i]) {
                const ReducedTexture &r = *reduced[i];
                tflags = r.min_mip | (r.four_bit ? 4 : 0);
                for (int k = r.min_mip; k < 4; k++) {
                    while (texdata.size() % 4) texdata.push_back(0);
                    mo[k] = int64_t(texdata.size());
                    if (k == r.min_mip && r.four_bit) {
                        // 16 palette indices, then two texels a byte (the first high)
                        texdata.insert(texdata.end(), r.pal.begin(), r.pal.end());
                        for (size_t p = 0; p + 1 < r.pixels.size() || p < r.pixels.size(); p += 2) {
                            int a = r.pixels[p] & 15, b2 = p + 1 < r.pixels.size() ? r.pixels[p + 1] & 15 : 0;
                            texdata.push_back(uint8_t(a << 4 | b2));
                        }
                    } else {
                        const Bytes &lv = reduced_lv[i][size_t(k)];
                        texdata.insert(texdata.end(), lv.begin(), lv.end());
                    }
                }
                for (int k = 0; k < r.min_mip; k++) mo[k] = mo[r.min_mip];
            } else {
                for (int k = 0; k < 4; k++) {
                    while (texdata.size() % 4) texdata.push_back(0);
                    mo[k] = int64_t(texdata.size());
                    texdata.insert(texdata.end(), t->mips[k].begin(), t->mips[k].end());
                }
            }
            std::swap(texdata, texdata_all);   // texdata_all: this texture's
            if (!texdata_all.empty()) {
                int64_t at;
                if (lib) {
                    at = lib->add(texdata_all);
                    in_lib[i] = true;
                } else {
                    while (texdata.size() % 4) texdata.push_back(0);
                    at = int64_t(texdata.size());
                    texdata.insert(texdata.end(), texdata_all.begin(), texdata_all.end());
                }
                for (auto &v : mo) v += at;
            }
            table.u16(t->w);
            table.u16(t->h);
            table.u8(tex_avg[i]);
            table.u8(tflags);
            table.u16(0);
            for (auto v : mo) table.i32(v);
        }
        size_t tofs = blob.add(table);
        L["textures"] = {tofs, textures.size()};
        size_t texbase = blob.add(texdata);
        texture_bytes = texdata.size();
        // the mip offsets are absolute within the blob (or the library's,
        // which the cartridge builder makes the level's)
        for (size_t i = 0; i < textures.size(); i++) {
            if (!textures[i]) continue;
            size_t base = tofs + i * 24 + 8;
            if (in_lib[i]) {
                for (int k = 0; k < 4; k++) opt.game.relocs->push_back(uint32_t(base + 4 * size_t(k)));
                continue;
            }
            for (int k = 0; k < 4; k++) {
                uint64_t v = blob.get_at(base + 4 * size_t(k), 4);
                blob.put_at(base + 4 * size_t(k), uint32_t(v + texbase), 4);
            }
        }
    }
    L["light"] = {blob.add(lightdata), lightdata.size()};
    L["vis"] = {blob.add(m.vis), m.vis.size()};
    {
        Buf b(big);
        for (auto &c : m.clipnodes) {
            b.i16(c.plane);
            b.i16(c.child[0]);
            b.i16(c.child[1]);
            b.i16(0);
        }
        L["clipnodes"] = {blob.add(b), m.clipnodes.size()};
    }
    // the lumps every level has the same of go in the cartridge's shared
    // library when there is one (their header entries then hold library offsets)
    std::vector<std::string> hdr_shared, ext_shared;
    auto share = [&](std::map<std::string, Lump> &T, std::vector<std::string> &names, const std::string &k,
                     const Bytes &b, size_t count, size_t a = 4) {
        if (opt.game.lib) {
            T[k] = {opt.game.lib->add(b, a), count};
            names.push_back(k);
        } else
            T[k] = {blob.add(b, a), count};
    };
    {   // palette as 32X CRAM words (BGR555)
        Buf b(big);
        for (int i = 0; i < 256; i++) {
            int r = pal.rgb[3 * i], g = pal.rgb[3 * i + 1], bb = pal.rgb[3 * i + 2];
            b.u16(((bb >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
        }
        share(L, hdr_shared, "palette", b.data(), 256);
    }
    share(L, hdr_shared, "colormap", colormap, colormap.size());
    share(L, hdr_shared, "conchars", Bytes(conchars.begin(), conchars.begin() + 128 * 128), 128 * 128);
    {   // Quake's border tile for reduced screen sizes (a 64x64 qpic in gfx.wad)
        check(wad.count("backtile"), "gfx.wad has no backtile");
        const Bytes &bt = wad["backtile"];
        Reader r(bt);
        check(r.i32(0) == 64 && r.i32(4) == 64, "backtile is not 64x64");
        share(L, hdr_shared, "backtile", r.slice(8, 64 * 64), 64 * 64);
    }

    // game data: entities, flat-coloured models, sounds
    Entity world;
    if (!ents.empty() && ents[0].classname() == "worldspawn") world = ents[0];
    std::optional<DemoStream> demo;
    if (!opt.demo.empty()) demo = convert_demo(pak.get(opt.demo), movers, log);   // big-endian on any host
    size_t before_game = blob.size();
    auto gd = add_game_data(blob, pak, pal, ents, m, movers, world, demo ? demo->models : std::vector<std::string>{},
                            demo ? demo->sounds : std::vector<std::string>{}, opt.game, log);
    for (auto &kv : gd) {
        if (starts_with(kv.first, "x:"))
            X[kv.first.substr(2)] = kv.second;
        else
            L[kv.first] = kv.second;
    }
    size_t game_bytes = blob.size() - before_game;
    L["demo"] = {0, 0};
    {
        Buf b(big);
        for (int x : flatof) b.u16(x);
        L["flatof"] = {blob.add(b), flatof.size()};
        Buf n(big);
        for (auto &f : flatnode) {
            n.u16(f.first);
            n.u8(f.nfront);
            n.u8(f.nback);
        }
        L["flatnode"] = {blob.add(n), flatnode.size()};
        Buf l(big);
        for (int x : flatlist) l.u16(x);
        L["flatlist"] = {blob.add(l), flatlist.size()};
    }
    if (demo) {
        blob.align(4);
        L["demo"] = {blob.add(demo->data), size_t(demo->count)};
    }
    {   // sky: the back (right) half of the sky texture, 128x128, as one layer
        const BspTexture *sky = nullptr;
        for (auto &t : textures)
            if (t && starts_with(t->name, "sky")) sky = &*t;
        Bytes b(128 * 128, 0);
        if (sky) {
            for (int y = 0; y < 128; y++)
                for (int x = 0; x < 128; x++) {
                    int c = sky->mips[0].at(size_t(y * 256 + x));   // front layer (clouds, 0 = transparent)
                    if (c == 0) c = sky->mips[0].at(size_t(y * 256 + 128 + x));   // back layer
                    b[size_t(y * 128 + x)] = uint8_t(c);
                }
        }
        // (none when the sky is drawn flat with textures on too)
        bool flat_sky = !E.flat_textures.empty();
        for (auto &t : textures)
            if (t && starts_with(t->name, "sky") && !always_flat(t->name)) flat_sky = false;
        if (flat_sky || !sky)
            L["skytex"] = {0, 0};
        else
            share(L, hdr_shared, "skytex", b, 128 * 128);
    }

    // ------------------------------------------------ extension lumps
    if (npieces) {
        Buf r(big), mk(big);
        int n = 0;
        for (size_t i = 0; i < nface_tex; i++) {
            check(flatref[i].size() < 256, "a face in 256 or more flat pieces");
            r.u16(n);
            r.u8(int64_t(flatref[i].size()));
            r.u8(0);
            for (int g : flatref[i]) {
                mk.u16(g);
                n++;
            }
        }
        check(n < 65536, "too many flat pieces");
        X["flatref"] = {blob.add(r), nface_tex};
        X["flatmark"] = {blob.add(mk), size_t(n)};
    }
    if (!lightgrid.empty()) X["lightgrid"] = {blob.add(lightgrid), lightgrid.size()};
    if (areas.count > 0 && !areas.doors.empty()) {
        Buf a(big), d(big);
        for (int v : areas.leaf_area) a.u8(v);
        X["leafarea"] = {blob.add(a), leafs.size()};
        // per door: mover, area count, up to four areas, pad
        for (auto &dr : areas.doors) {
            d.u8(dr.mover);
            d.u8(int64_t(dr.areas.size()));
            for (size_t k = 0; k < 4; k++) d.u8(k < dr.areas.size() ? dr.areas[k] : 0);
            d.u16(0);
        }
        X["doors"] = {blob.add(d), areas.doors.size()};
    }
    if (!E.areas.empty()) {
        std::vector<int> far(leafs.size(), 0);
        for (auto &ar : E.areas)
            for (int l : ar.leafs)
                if (l >= 0 && l < int(far.size())) far[size_t(l)] = std::clamp(ar.far, 0, 65535);
        Buf b(big);
        for (int v : far) b.u16(v);
        X["leaffar"] = {blob.add(b), leafs.size()};
    }
    if (!mover_model.empty()) {
        std::vector<size_t> ofs(movers.size(), 0);
        for (auto &kv : mover_model) {
            Bytes b = build_model(kv.second, big);
            blob.align(4);
            ofs[size_t(kv.first)] = blob.add(b);
        }
        Buf t(big);
        for (auto o : ofs) t.u32(int64_t(o));
        X["mvmodels"] = {blob.add(t), movers.size()};
    }
    if (UP) {   // the palette as 32X colours, Quake's colours as its, and the model colour table
        Buf b(big);
        for (auto c : fpt->cram) b.u16(c);
        for (auto c : fpt->remap) b.u8(c);
        b.bytes(fpt->cmap.data(), fpt->cmap.size());
        share(X, ext_shared, "flatpal", b.data(), 1, 16);
    }
    int64_t extofs = 0;
    if (!X.empty()) {
        Buf d(big);
        d.bytes(reinterpret_cast<const uint8_t *>("Q32X"), 4);
        d.u32(int64_t(LEVEL_EXT_LUMPS.size()));
        for (auto &k : LEVEL_EXT_LUMPS) {
            auto it = X.find(k);
            d.i32(it == X.end() ? 0 : int64_t(it->second.ofs));
            d.i32(it == X.end() ? 0 : int64_t(it->second.count));
        }
        extofs = int64_t(blob.add(d));
        for (auto &k : ext_shared)
            opt.game.relocs->push_back(uint32_t(size_t(extofs) + 8 + 8 * size_t(std::find(LEVEL_EXT_LUMPS.begin(), LEVEL_EXT_LUMPS.end(), k) - LEVEL_EXT_LUMPS.begin())));
    }
    for (auto &k : hdr_shared)
        opt.game.relocs->push_back(uint32_t(8 + 8 * size_t(std::find(LEVEL_LUMPS.begin(), LEVEL_LUMPS.end(), k) - LEVEL_LUMPS.begin())));

    // ------------------------------------------------ spawn point
    Vec3 spawn = {0.0, 0.0, 0.0};
    double yaw = 0.0;
    for (auto &e : ents)
        if (e.classname() == "info_player_start") {
            auto v = split_ws(e.get("origin"));
            check(v.size() == 3, "bad info_player_start origin");
            spawn = {pyfloat(v[0]), pyfloat(v[1]), pyfloat(v[2])};
            yaw = pyfloat(e.get("angle", "0"));
        }
    {   // header
        Buf h(big);
        h.bytes(reinterpret_cast<const uint8_t *>("Q32L"), 4);
        h.i32(int64_t(LEVEL_LUMPS.size()));
        for (auto &k : LEVEL_LUMPS) {
            check(L.count(k) != 0, "lump not written: " + k);
            h.i32(int64_t(L[k].ofs));
            h.i32(int64_t(L[k].count));
        }
        h.i32(fx(spawn[0]));
        h.i32(fx(spawn[1]));
        h.i32(fx(spawn[2]));
        h.i32(int64_t(std::trunc(yaw)));
        h.i16(models[0].headnode[0]);
        h.i16(models[0].headnode[1]);
        h.i16(int64_t(nodes.size()));
        h.i16(nworld);
        h.i16(models[0].visleafs);
        h.i16(models[0].headnode[2]);
        h.i16(pyintf(world.get("worldtype", "0")));
        h.i16(0);
        check(h.size() == size_t(LEVEL_HDR_EXTOFS), "header layout");
        h.i32(extofs);
        h.i32(E.flat_only ? LF_FLAT_ONLY : 0);
        check(h.size() <= size_t(LEVEL_HDR_SIZE), "header too large");
        std::copy(h.data().begin(), h.data().end(), blob.data().begin());
    }
    blob.align(16);
    say("wrote " + std::to_string(blob.size()) + " bytes");
    if (info) {
        info->lumps = L;
        info->ext = X;
        info->size = blob.size();
        info->faces = int(face_recs.size());
        info->world_faces = nworld;
        info->fragments = nfrag;
        info->merged = int(face_recs.size() - nface_tex);
        info->flat_faces = int(flatlist.size());
        info->nodes = int(nodes.size());
        info->leafs = int(leafs.size());
        info->verts = int(out_verts.size());
        info->movers = int(movers.size());
        info->triggers = int(L["triggers"].count);
        info->entities = int(L["ents"].count);
        info->textured_merged = textured_merged;
        info->pieces = npieces;
        info->removed = removed_faces;
        info->areas = areas.count;
        info->door_portals = int(areas.doors.size());
        info->texture_bytes = texture_bytes;
        info->light_bytes = lightdata.size();
        info->sound_bytes = 0;
        info->model_bytes = game_bytes;
        info->warnings = warnings;
        info->world_members.clear();
        for (auto &p : plan) info->world_members.push_back(p.members);
        info->bands = bandmaps;
        info->band_rows = band_rows;
    }
    return blob.data();
}

}  // namespace q32x
