// q32x-convert: the Quake 32X conversions from the command line, the same
// code the editor runs. The game's build uses it in place of the original
// Python tools.
#include <cmath>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "q32x/demo.h"
#include "q32x/gamedata.h"
#include "q32x/json.h"
#include "q32x/level.h"
#include "q32x/mdgfx.h"
#include "q32x/project.h"
#include "q32x/rom.h"

using namespace q32x;

static const char *USAGE =
    "usage: q32x-convert <command> ...\n"
    "\n"
    "  level [--le] [--demo demo1.dem] [--flat-tol N] [--project file.q32x] pak0.pak maps/e1m1.bsp out.lvl\n"
    "        a level blob (--le: the little-endian copy for the host build)\n"
    "  mdgfx [--project file.q32x] [--packed] [--flat-weapons] [--weapon-sizes 0,2,2,3,4,5]\n"
    "        pak0.pak out.bin [preview_dir]\n"
    "        the Mega Drive tiles, palettes and view weapons\n"
    "  rom [--version X.Y.Z] program.bin out.32x level.lvl...\n"
    "        the cartridge\n"
    "  headers pak0.pak g_ids.h g_frames.h\n"
    "        the engine's id and animation headers\n"
    "  demo-info pak0.pak demo1.dem\n"
    "        a summary of a Quake demo\n"
    "  botroute route.json out.c\n"
    "        the demo bot's waypoints as C (from tools/plan_route.py's route)\n"
    "  new-project pak0.pak out.q32x\n"
    "        a project with the cartridge's three levels as they are\n"
    "  flat-palette project.q32x level palette.gpl [key=value...] out.q32x\n"
    "        flat mode in a palette of your own for a level (.pal .gpl .hex .act .lmp .png, or grey);\n"
    "        keys: vividness variety special_kinds neutral_surfaces levels occlusion reach rays up_bias ambient lights lightmaps highlight shade enabled\n"
    "  maps [--no-textures] [--palette file] [--sounds full|half|packed|both] [--template project.q32x] pak0.pak\n"
    "        every map in the pak converted on its own: name, bytes (its sounds included) and title\n"
    "  make-project [--no-textures] [--palette file] [--sounds full|half|packed|both] --template project.q32x\n"
    "        pak0.pak out.q32x map...\n"
    "        a project of these maps with the template's settings (its first level's edits for each)\n"
    "  export project.q32x out_dir\n"
    "        every level, the Mega Drive data and (given the program) the cartridge of a project\n"
    "  auto project.q32x [level]\n"
    "        auto-optimise every level (or one), saving the project\n"
    "  check project.q32x\n"
    "        the budgets and limits of a project's levels\n";

// the build script's choices on a level's edits: no textures (flat shading
// only, smaller), and flat mode in a palette of greys
static void cartridge_edits(LevelEdits &e, bool no_textures, const std::string &palette)
{
    e.flat_only = no_textures;
    if (palette.empty() || palette == "none") return;
    FlatPalOptions &f = e.flatpal;
    std::string err;
    if (!builtin_palette(palette, f.rgb) && !load_palette_file(palette, f.rgb, &err))
        throw std::runtime_error(palette + ": " + err);
    f.enabled = true;
    f.source = std::filesystem::path(palette).filename().string();
    f.vividness = 0;
    f.variety = 0;
    f.special_kinds = true;
    f.neutral_surfaces = true;
}

static std::string abs_path(const std::string &p) { return std::filesystem::absolute(p).lexically_normal().string(); }

static std::string take(std::vector<std::string> &a, const std::string &flag)
{
    for (size_t i = 0; i + 1 < a.size(); i++)
        if (a[i] == flag) {
            std::string v = a[i + 1];
            a.erase(a.begin() + std::ptrdiff_t(i), a.begin() + std::ptrdiff_t(i) + 2);
            return v;
        }
    return "";
}

static bool flag(std::vector<std::string> &a, const std::string &f)
{
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] == f) {
            a.erase(a.begin() + std::ptrdiff_t(i));
            return true;
        }
    return false;
}

static void write_text(const std::string &path, const std::string &s)
{
    write_file(path, Bytes(s.begin(), s.end()));
}

static int run(std::vector<std::string> a)
{
    if (a.empty()) {
        std::fputs(USAGE, stderr);
        return 1;
    }
    std::string cmd = a[0];
    a.erase(a.begin());
    std::string log;
    if (cmd == "level") {
        LevelOptions opt;
        opt.big_endian = !flag(a, "--le");
        opt.demo = take(a, "--demo");
        std::string tol = take(a, "--flat-tol");
        if (tol.empty() && std::getenv("FLAT_TOL")) tol = std::getenv("FLAT_TOL");
        if (!tol.empty()) opt.flat_tol = std::stoi(tol);
        std::string proj = take(a, "--project");
        if (a.size() != 3) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Pak pak = Pak::load(a[0]);
        Bytes lvl;
        if (!proj.empty()) {
            Project p = Project::load(proj);
            p.attach_pak(&pak);
            lvl = p.build_level(lower(a[1]), opt, nullptr, &log);
        } else
            lvl = build_level(pak, lower(a[1]), opt, nullptr, &log);
        write_file(a[2], lvl);
        std::fputs(log.c_str(), stdout);
        return 0;
    }
    if (cmd == "mdgfx") {
        std::string proj = take(a, "--project");
        bool packed = flag(a, "--packed"), flatw = flag(a, "--flat-weapons");
        std::string sizes = take(a, "--weapon-sizes");
        if (a.size() < 2 || a.size() > 3) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Pak pak = Pak::load(a[0]);
        MdGfxOptions opt;
        if (!proj.empty()) opt = Project::load(proj).mdgfx_options();
        if (packed) opt.compress = true;
        if (flatw) opt.flat_weapons = true;
        if (!sizes.empty()) {
            size_t k = 0, pos = 0;
            while (k < 6 && pos <= sizes.size()) {
                size_t c = sizes.find(',', pos);
                opt.weapon_size[k++] = std::stoi(sizes.substr(pos, c - pos));
                if (c == std::string::npos) break;
                pos = c + 1;
            }
        }
        if (a.size() == 3) opt.preview_dir = a[2];
        MdGfx g = build_mdgfx(pak, opt, &log);
        write_file(a[1], g.data);
        std::fputs(log.c_str(), stdout);
        return 0;
    }
    if (cmd == "flat-palette") {
        if (a.size() < 4) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Project p = Project::load(a[0]);
        LevelProject *lp = nullptr;
        for (auto &l : p.levels)
            if (l.name == a[1]) lp = &l;
        if (!lp) {
            std::fprintf(stderr, "no level %s in the project\n", a[1].c_str());
            return 1;
        }
        FlatPalOptions &f = lp->edits.flatpal;
        std::string err;
        if (!builtin_palette(a[2], f.rgb) && !load_palette_file(a[2], f.rgb, &err)) {
            std::fprintf(stderr, "%s: %s\n", a[2].c_str(), err.c_str());
            return 1;
        }
        f.enabled = true;
        f.source = a[2];
        for (size_t i = 3; i + 1 < a.size(); i++) {
            auto eq = a[i].find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "not key=value: %s\n", a[i].c_str());
                return 1;
            }
            std::string k = a[i].substr(0, eq);
            double v = pyfloat(a[i].substr(eq + 1));
            if (k == "vividness") f.vividness = v;
            else if (k == "variety") f.variety = v;
            else if (k == "special_kinds") f.special_kinds = v != 0;
            else if (k == "neutral_surfaces") f.neutral_surfaces = v != 0;
            else if (k == "levels") f.levels = int(v);
            else if (k == "occlusion") f.occlusion = v;
            else if (k == "reach") f.reach = v;
            else if (k == "rays") f.rays = int(v);
            else if (k == "up_bias") f.up_bias = v;
            else if (k == "ambient") f.ambient = v;
            else if (k == "lights") f.lights = v;
            else if (k == "lightmaps") f.lightmaps = v;
            else if (k == "highlight") f.highlight = v;
            else if (k == "shade") f.shade = v != 0;
            else if (k == "enabled") f.enabled = v != 0;
            else {
                std::fprintf(stderr, "unknown key %s\n", k.c_str());
                return 1;
            }
        }
        p.save(a.back());
        return 0;
    }
    if (cmd == "maps" || cmd == "make-project") {
        bool notex = flag(a, "--no-textures");
        std::string pal = take(a, "--palette"), tpl = take(a, "--template"), snd = take(a, "--sounds");
        int sfmt = snd == "half" ? SF_HALF : snd == "packed" ? SF_PACK : snd == "both" ? SF_HALF | SF_PACK : 0;
        check(snd.empty() || snd == "full" || sfmt, "--sounds: full, half, packed or both");
        if ((cmd == "maps" && a.size() != 1) || (cmd == "make-project" && (a.size() < 3 || tpl.empty()))) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Pak pak = Pak::load(a[0]);
        Project t = tpl.empty() ? Project::quake_default(a[0]) : Project::load(tpl);
        LevelEdits base = t.levels.empty() ? LevelEdits{} : t.levels[0].edits;
        cartridge_edits(base, notex, pal);
        t.pak_path = abs_path(a[0]);
        t.program_path = t.program_path.empty() ? "" : abs_path(t.resolve(t.program_path));
        t.attach_pak(&pak);
        t.sound_format = sfmt;
        // the maps' demos: the first of demo1..3 recorded on each
        std::map<std::string, std::string> demo_of;
        for (int k = 1; k <= 3; k++) {
            std::string dn = "demo" + std::to_string(k) + ".dem";
            if (!pak.has(dn)) continue;
            try {
                Dem d = Dem::parse(pak.get(dn));
                for (auto &f : d.frames)
                    for (auto &m : f.msgs)
                        if (m.name == "serverinfo" && m.models.size() > 1 && !demo_of.count(lower(m.models[1])))
                            demo_of[lower(m.models[1])] = dn;
            } catch (const std::exception &) {
            }
        }
        if (cmd == "maps") {
            std::vector<std::string> names;
            for (auto &n : pak.names())
                if (starts_with(n, "maps/") && ends_with(n, ".bsp") && !starts_with(n, "maps/b_")) names.push_back(n);
            std::sort(names.begin(), names.end());
            t.shared_bank = false;   // (each level with all its sounds: the most it can take)
            for (auto &n : names) {
                std::string stem = n.substr(5, n.size() - 9), title;
                try {
                    for (auto &e : parse_entities(Bsp::parse(pak.get(n)).entities))
                        if (e.classname() == "worldspawn") title = e.get("message");
                    for (auto &c : title)
                        if (c == '\n' || c == '\t') c = ' ';
                    LevelProject lp;
                    lp.name = stem;
                    lp.bsp = n;
                    lp.edits = base;
                    auto dm = demo_of.find(n);
                    if (dm != demo_of.end()) lp.demo = dm->second;   // (with its attract demo, if it has one)
                    std::string lg;
                    Bytes b = t.build_level(lp, true, nullptr, &lg);
                    std::printf("%s\t%zu\t%s\n", stem.c_str(), b.size(), title.c_str());
                } catch (const std::exception &e) {
                    std::printf("%s\t-\t%s\n", stem.c_str(), e.what());
                }
                std::fflush(stdout);
            }
            return 0;
        }
        check(a.size() - 2 <= size_t(ROM_MAX_LEVELS), "at most " + std::to_string(ROM_MAX_LEVELS) + " levels");
        t.levels.clear();
        for (size_t i = 2; i < a.size(); i++) {
            LevelProject lp;
            lp.name = lower(a[i]);
            lp.bsp = "maps/" + lp.name + ".bsp";
            check(pak.has(lp.bsp), "no " + lp.bsp + " in the pak");
            lp.edits = base;
            // the attract demo: the first chosen map that has one (the game plays only that)
            auto it = demo_of.find(lp.bsp);
            bool have_demo = false;
            for (auto &l : t.levels) have_demo = have_demo || !l.demo.empty();
            if (it != demo_of.end() && !have_demo) lp.demo = it->second;
            t.levels.push_back(lp);
        }
        t.save(a[1]);
        return 0;
    }
    if (cmd == "rom") {
        std::string version = take(a, "--version");
        if (a.size() < 3) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        std::vector<RomLevel> levels;
        for (size_t i = 2; i < a.size(); i++) {
            std::string base = a[i];
            size_t sl = base.find_last_of("/\\");
            if (sl != std::string::npos) base = base.substr(sl + 1);
            base = base.substr(0, base.find('.'));
            levels.push_back({base, read_file(a[i])});
        }
        RomInfo info;
        Bytes rom = build_rom(read_file(a[0]), levels, version, &info);
        write_file(a[1], rom);
        for (auto &l : info.levels)
            std::printf("  %-6s pages %2d..%2d  %7zu bytes (%zu KB free in the window)%s\n", l.name.c_str(), l.first,
                        l.first + l.pages - 1, l.bytes, (ROM_WINDOW_PAGES * ROM_PAGE - l.bytes) / 1024,
                        l.demo ? ", with a demo" : "");
        std::printf("%s: %zu KB (program %zu KB), checksum %04X\n", a[1].c_str(), info.size / 1024,
                    info.program / 1024, info.checksum);
        return 0;
    }
    if (cmd == "headers") {
        if (a.size() != 3) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        write_text(a[1], ids_header());
        write_text(a[2], frames_header(Pak::load(a[0])));
        return 0;
    }
    if (cmd == "demo-info") {
        if (a.size() != 2) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Pak pak = Pak::load(a[0]);
        Dem d = Dem::parse(pak.get(a[1]));
        std::map<std::string, int> counts;
        double t0 = -1, t1 = -1;
        for (auto &f : d.frames)
            for (auto &m : f.msgs) {
                counts[m.name]++;
                if (m.name == "time") {
                    if (t0 < 0) t0 = m.num[0];
                    t1 = m.num[0];
                }
                if (m.name == "serverinfo") std::printf("level %s protocol %d\n", m.str[0].c_str(), int(m.num[0]));
            }
        std::printf("track %s blocks %zu time %.2f .. %.2f\n", d.track.c_str(), d.frames.size(), t0, t1);
        for (auto &kv : counts) std::printf("  %-18s %d\n", kv.first.c_str(), kv.second);
        return 0;
    }
    if (cmd == "botroute") {
        if (a.size() != 2) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Bytes jb = read_file(a[0]);
        Json route = Json::parse(std::string(jb.begin(), jb.end()));
        using P = std::vector<double>;
        std::vector<P> pts, keep;
        for (auto &p : route["path"].items()) {
            P q;
            for (auto &c : p.items()) q.push_back(c.as_number());
            pts.push_back(q);
        }
        std::vector<P> jumps;
        for (auto &p : route["jumps"].items()) {
            P q;
            for (auto &c : p.items()) q.push_back(c.as_number());
            jumps.push_back(q);
        }
        // Ramer-Douglas-Peucker in x/y, keeping changes of floor height
        std::function<std::vector<P>(const std::vector<P> &, double)> rdp = [&](const std::vector<P> &v, double eps) {
            if (v.size() < 3) return v;
            const P &A = v.front(), &B = v.back();
            double dx = B[0] - A[0], dy = B[1] - A[1], n = std::hypot(dx, dy);
            if (n == 0) n = 1.0;
            double best = -1;
            size_t bi = 0;
            for (size_t i = 1; i + 1 < v.size(); i++) {
                const P &q = v[i];
                double d = std::fabs(dy * (q[0] - A[0]) - dx * (q[1] - A[1])) / n;
                if (std::fabs(q[2] - A[2]) > 20 && std::fabs(q[2] - B[2]) > 20) d = std::max(d, eps * 2);
                if (d > best) {
                    best = d;
                    bi = i;
                }
            }
            if (best <= eps) return std::vector<P>{A, B};
            auto l = rdp(std::vector<P>(v.begin(), v.begin() + std::ptrdiff_t(bi) + 1), eps);
            auto r = rdp(std::vector<P>(v.begin() + std::ptrdiff_t(bi), v.end()), eps);
            l.pop_back();
            l.insert(l.end(), r.begin(), r.end());
            return l;
        };
        if (route.has("waypoints"))
            for (auto &p : route["waypoints"].items()) {
                P q;
                for (auto &c : p.items()) q.push_back(c.as_number());
                keep.push_back(q);
            }
        else
            keep = rdp(pts, 6.0);
        std::string out = "/* generated by q32x-convert botroute from " + a[0] + " */\n";
        out += "#include \"bot.h\"\n\nconst bot_wp_t bot_route[] = {\n";
        for (size_t i = 1; i < keep.size(); i++) {
            const P &p = keep[i];
            int flags = 0;
            if (p.size() > 3)
                flags = int(p[3]);
            else
                for (auto &j : jumps)
                    if (j == p) flags = 1;
            char line[96];
            std::snprintf(line, sizeof line, "    { %lld, %lld, %lld, %d },\n", (long long)pyround(p[0]),
                          (long long)pyround(p[1]), (long long)pyround(p[2]), flags);
            out += line;
        }
        out += "};\nconst int bot_nroute = " + std::to_string(keep.size() - 1) + ";\n";
        write_text(a[1], out);
        std::printf("%zu route points -> %zu waypoints\n", pts.size(), keep.size() - 1);
        return 0;
    }
    if (cmd == "new-project") {
        if (a.size() != 2) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Project p = Project::quake_default(a[0]);
        p.save(a[1]);
        return 0;
    }
    if (cmd == "export") {
        if (a.size() != 2) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Project p = Project::load(a[0]);
        ExportResult r = p.export_all(a[1], &log);
        std::fputs(log.c_str(), stdout);
        return r.ok ? 0 : 2;
    }
    if (cmd == "auto") {
        if (a.empty() || a.size() > 2) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Project p = Project::load(a[0]);
        for (size_t i = 0; i < p.levels.size(); i++)
            if (a.size() == 1 || p.levels[i].name == a[1]) p.auto_optimise(i, &log);
        p.save(a[0]);
        std::fputs(log.c_str(), stdout);
        return 0;
    }
    if (cmd == "check") {
        if (a.size() != 1) {
            std::fputs(USAGE, stderr);
            return 1;
        }
        Project p = Project::load(a[0]);
        auto reports = p.check_all(&log);
        std::fputs(log.c_str(), stdout);
        for (auto &r : reports)
            if (!r.ok()) return 2;
        return 0;
    }
    std::fputs(USAGE, stderr);
    return 1;
}

int main(int argc, char **argv)
{
    std::vector<std::string> a(argv + 1, argv + argc);
    try {
        return run(a);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "q32x-convert: %s\n", e.what());
        return 2;
    }
}
