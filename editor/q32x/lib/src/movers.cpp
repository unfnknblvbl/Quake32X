#include "q32x/movers.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace q32x {

const std::vector<std::string> MOVER_CLASSES = {"func_door", "func_door_secret", "func_plat", "func_button",
                                                "func_train"};
const std::vector<std::string> TRIGGER_CLASSES = {"trigger_once", "trigger_multiple", "trigger_secret",
                                                  "trigger_counter", "trigger_teleport"};
// (func_episodegate is not here: it appears only once its episode is done,
// and the cartridge keeps no runes)
const std::vector<std::string> STATIC_CLASSES = {"func_wall", "func_illusionary", "func_bossgate"};

static bool in(const std::vector<std::string> &v, const std::string &s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

// math.radians, as CPython computes it
static double radians(double x) { return x * (3.141592653589793 / 180.0); }

static bool truthy(const OptStr &s) { return s && !s->empty(); }

bool present(const Entity &e)
{
    return !(pyintf(e.get("spawnflags", "0")) & NOT_MEDIUM);
}

Vec3 movedir(const Entity &e)
{
    double ang = e.num("angle", 0.0);
    if (ang == -1) return {0.0, 0.0, 1.0};
    if (ang == -2) return {0.0, 0.0, -1.0};
    double a = radians(ang);
    return {pyround_digits(std::cos(a), 6), pyround_digits(std::sin(a), 6), 0.0};
}

static Vec3 add(const Vec3 &a, const Vec3 &b, double s = 1.0)
{
    return {a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s};
}

std::vector<Vec3> keys_along(const std::vector<Vec3> &points, int step)
{
    std::vector<Vec3> out = {points.at(0)};
    for (size_t p = 0; p + 1 < points.size(); p++) {
        const Vec3 &a = points[p], &b = points[p + 1];
        Vec3 d = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        double ln = std::sqrt(dot(d, d));
        int64_t n = std::max<int64_t>(1, pyceil(ln / step));
        for (int64_t i = 1; i <= n; i++)
            out.push_back({a[0] + d[0] * double(i) / double(n), a[1] + d[1] * double(i) / double(n),
                           a[2] + d[2] * double(i) / double(n)});
    }
    for (auto &p : out)
        for (auto &c : p) c = double(pyround(c));
    return out;
}

static void absbox(const Mover &m, const Vec3 &off, Vec3 &mn, Vec3 &mx)
{
    for (int k = 0; k < 3; k++) {
        mn[k] = m.mins[k] + off[k];
        mx[k] = m.maxs[k] + off[k];
    }
}

static bool touching(const Mover &a, const Mover &b)
{
    Vec3 amin, amax, bmin, bmax;
    absbox(a, a.start, amin, amax);
    absbox(b, b.start, bmin, bmax);
    for (int k = 0; k < 3; k++)
        if (!(amin[k] <= bmax[k] && amax[k] >= bmin[k])) return false;
    return true;
}

static void link_doors(std::vector<Mover> &movers)
{
    std::vector<int> doors;
    for (size_t i = 0; i < movers.size(); i++)
        if (movers[i].cls == "func_door" && !(movers[i].flags & 4)) doors.push_back(int(i));
    std::set<int> seen;
    for (int i : doors) {
        if (seen.count(i)) continue;
        std::vector<int> group = {i}, todo = {i};
        seen.insert(i);
        while (!todo.empty()) {
            int j = todo.back();
            todo.pop_back();
            for (int k : doors)
                if (!seen.count(k) && touching(movers[size_t(j)], movers[size_t(k)])) {
                    seen.insert(k);
                    group.push_back(k);
                    todo.push_back(k);
                }
        }
        std::sort(group.begin(), group.end());
        for (int k : group) movers[size_t(k)].group = group[0];
        Mover &lead = movers[size_t(group[0])];
        for (int k : group) {
            Mover &o = movers[size_t(k)];
            if (o.health && !lead.health) lead.health = o.health;
            if (truthy(o.targetname) && !truthy(lead.targetname)) lead.targetname = o.targetname;
            if (truthy(o.message) && !truthy(lead.message)) lead.message = o.message;
            if (o.items && !lead.items) lead.items = o.items;
        }
        // shootable, fired and key doors spawn no touch field
        if (!lead.health && !truthy(lead.targetname) && !lead.items) {
            Vec3 mins, maxs;
            for (int a = 0; a < 3; a++) {
                double lo = 1e300, hi = -1e300;
                for (int k : group) {
                    Vec3 mn, mx;
                    absbox(movers[size_t(k)], movers[size_t(k)].start, mn, mx);
                    lo = std::min(lo, mn[a]);
                    hi = std::max(hi, mx[a]);
                }
                mins[a] = lo;
                maxs[a] = hi;
            }
            lead.has_field = true;
            lead.field_min = {mins[0] - 60, mins[1] - 60, mins[2] - 8};
            lead.field_max = {maxs[0] + 60, maxs[1] + 60, maxs[2] + 8};
        }
    }
    for (size_t i = 0; i < movers.size(); i++)
        if (movers[i].group < 0) movers[i].group = int(i);
}

std::vector<Mover> build_movers(const std::vector<Entity> &ents, const Bsp &bsp)
{
    std::vector<Mover> movers;
    const Vec3 zero = {0.0, 0.0, 0.0};
    for (const auto &e : ents) {
        std::string cls = e.get("classname");
        std::string mdl = e.get("model");
        if (!in(MOVER_CLASSES, cls) || !starts_with(mdl, "*") || !present(e)) continue;
        int mi = std::stoi(mdl.substr(1));
        const BspModel &mo = bsp.models.at(size_t(mi));
        Mover m;
        m.ent = &e;
        m.cls = cls;
        m.model = mi;
        m.mins = {mo.mins[0], mo.mins[1], mo.mins[2]};
        m.maxs = {mo.maxs[0], mo.maxs[1], mo.maxs[2]};
        Vec3 size = {m.maxs[0] - m.mins[0], m.maxs[1] - m.mins[1], m.maxs[2] - m.mins[2]};
        int sf = pyintf(e.get("spawnflags", "0"));
        m.flags = sf;
        m.hull1 = mo.headnode[1];
        m.hull2 = mo.headnode[2];
        m.sounds = int(std::trunc(e.num("sounds", 0)));
        m.targetname = e.opt("targetname");
        m.target = e.opt("target");
        m.killtarget = e.opt("killtarget");
        m.message = e.opt("message");
        m.health = int(std::trunc(e.num("health", 0)));
        auto orv = [](double v, double d) { return v != 0 ? v : d; };
        if (cls == "func_door") {
            Vec3 md = movedir(e);
            double lip = e.num("lip", 8);
            Vec3 pos1 = zero, pos2 = add(zero, md, std::fabs(dot(md, size)) - lip);
            if (sf & 1) std::swap(pos1, pos2);     // DOOR_START_OPEN
            m.speed = orv(e.num("speed", 100), 100);
            m.wait = orv(e.num("wait", 3), 3);
            m.dmg = orv(e.num("dmg", 2), 2);
            m.pos = {pos1, pos2};
            m.start = pos1;
            // key doors (DOOR_GOLD_KEY 8, DOOR_SILVER_KEY 16) stay open
            m.items = (sf & 8) ? 2 : (sf & 16) ? 1 : 0;
            if (m.items) m.wait = -1;
            m.keys = keys_along({pos1, pos2});
        } else if (cls == "func_door_secret") {
            double a = radians(e.num("angle", 0));
            Vec3 fwd = {std::cos(a), std::sin(a), 0.0};
            Vec3 right = {std::sin(a), -std::cos(a), 0.0};
            Vec3 up = {0.0, 0.0, 1.0};
            Vec3 dest1;
            if (sf & 4) {                         // SECRET_1ST_DOWN
                double width = std::fabs(dot(up, size));
                dest1 = add(zero, up, -width);
            } else {
                double width = std::fabs(dot(right, size));
                int temp = 1 - (sf & 2);           // SECRET_1ST_LEFT
                dest1 = add(zero, right, width * temp);
            }
            double length = std::fabs(dot(fwd, size));
            Vec3 dest2 = add(dest1, fwd, length);
            if (!m.sounds) m.sounds = 3;
            bool shoot = !truthy(m.targetname) || (sf & 16);
            m.speed = 50;
            m.wait = orv(e.num("wait", 5), 5);
            m.dmg = e.num("dmg", 2);
            m.pos = {zero, dest1, dest2};
            m.start = zero;
            m.health = shoot ? 10000 : 0;
            m.keys = keys_along({zero, dest1, dest2});
        } else if (cls == "func_plat") {
            double height = e.num("height", size[2] - 8);
            Vec3 pos1 = zero, pos2 = {0.0, 0.0, -height};
            if (!m.sounds) m.sounds = 2;
            m.speed = orv(e.num("speed", 150), 150);
            m.wait = 3;
            m.dmg = 1;
            m.pos = {pos1, pos2};
            m.start = truthy(m.targetname) ? pos1 : pos2;
            m.keys = keys_along({pos1, pos2});
        } else if (cls == "func_train") {
            // the path_corners from its target, as offsets that put the
            // model's mins on each corner; it stops at a corner with wait -1
            std::map<std::string, const Entity *> byname;
            for (const auto &c : ents)
                if (c.get("classname") == "path_corner" && c.has("targetname")) byname[c.get("targetname")] = &c;
            std::vector<Vec3> pts;
            std::set<std::string> seen;
            OptStr t = e.opt("target");
            while (truthy(t) && byname.count(*t) && !seen.count(*t)) {
                seen.insert(*t);
                const Entity &c = *byname[*t];
                Vec3 o = c.vec("origin");
                pts.push_back({o[0] - m.mins[0], o[1] - m.mins[1], o[2] - m.mins[2]});
                if (c.num("wait", 0) == -1 && pts.size() > 1) break;
                t = c.opt("target");
            }
            check(pts.size() >= 2 && pts.size() <= 3, "func_train *" + std::to_string(mi) + ": unsupported path");
            m.speed = orv(e.num("speed", 100), 100);
            m.wait = -1;
            m.dmg = orv(e.num("dmg", 2), 2);
            m.pos = pts;
            m.start = pts[0];
            m.keys = keys_along(pts);
        } else {                                  // func_button
            Vec3 md = movedir(e);
            double lip = e.num("lip", 4);
            Vec3 pos2 = add(zero, md, std::fabs(dot(md, size)) - lip);
            m.speed = orv(e.num("speed", 40), 40);
            m.wait = orv(e.num("wait", 1), 1);
            m.dmg = 0;
            m.pos = {zero, pos2};
            m.start = zero;
            m.keys = keys_along({zero, pos2});
        }
        movers.push_back(std::move(m));
    }
    link_doors(movers);
    return movers;
}

static void plat_field(const Mover &m, Vec3 &tmin, Vec3 &tmax)
{
    const Vec3 &mins = m.mins, &maxs = m.maxs;
    tmin = {mins[0] + 25, mins[1] + 25, 0};
    tmax = {maxs[0] - 25, maxs[1] - 25, maxs[2] + 8};
    tmin[2] = tmax[2] - (m.pos[0][2] - m.pos[1][2] + 8);
    if (m.flags & 1) tmax[2] = tmin[2] + 8;      // PLAT_LOW_TRIGGER
    for (int k = 0; k < 2; k++)
        if (maxs[k] - mins[k] <= 50) {
            tmin[k] = (mins[k] + maxs[k]) * 0.5;
            tmax[k] = tmin[k] + 1;
        }
}

std::vector<Trigger> build_triggers(const std::vector<Entity> &ents, const Bsp &bsp,
                                    const std::vector<Mover> &movers)
{
    std::vector<Trigger> trigs;
    for (const auto &e : ents) {
        std::string cls = e.get("classname");
        std::string mdl = e.get("model");
        if (!in(TRIGGER_CLASSES, cls) || !starts_with(mdl, "*") || !present(e)) continue;
        const BspModel &mo = bsp.models.at(size_t(std::stoi(mdl.substr(1))));
        Trigger t;
        t.cls = cls;
        t.mins = {mo.mins[0], mo.mins[1], mo.mins[2]};
        t.maxs = {mo.maxs[0], mo.maxs[1], mo.maxs[2]};
        t.flags = pyintf(e.get("spawnflags", "0"));
        t.sounds = int(std::trunc(e.num("sounds", 0)));
        t.targetname = e.opt("targetname");
        t.target = e.opt("target");
        t.killtarget = e.opt("killtarget");
        t.message = e.opt("message");
        t.health = int(std::trunc(e.num("health", 0)));
        t.wait = e.num("wait", 0.2);
        t.count = int(std::trunc(e.num("count", 2)));
        if (e.has("angle") && e.num("angle", 0) != 0) t.facing = movedir(e);
        if (cls == "trigger_once")
            t.wait = -1;
        else if (cls == "trigger_secret") {
            t.wait = -1;
            if (!truthy(t.message)) t.message = std::string("You found a secret area!");
            if (!t.sounds) t.sounds = 1;
        } else if (cls == "trigger_counter")
            t.wait = -1;
        else if (cls == "trigger_teleport") {
            const Entity *dest = nullptr;
            for (const auto &d : ents)
                if (d.get("classname") == "info_teleport_destination" && d.opt("targetname") == e.opt("target"))
                    dest = &d;
            // (the start map has teleporters to places only deathmatch or the
            // registered game has: left out)
            if (!dest) continue;
            Vec3 o = dest->vec("origin");
            t.has_dest = true;
            t.dest = {o[0], o[1], o[2] + 27};
            t.dest_yaw = dest->num("angle", 0);
        }
        trigs.push_back(std::move(t));
    }
    for (size_t i = 0; i < movers.size(); i++) {
        const Mover &m = movers[i];
        if (m.has_field) {
            Trigger t;
            t.cls = "door_field";
            t.mins = m.field_min;
            t.maxs = m.field_max;
            t.owner = int(i);
            t.wait = 0;
            t.count = 0;
            trigs.push_back(t);
        }
        if (m.cls == "func_plat") {
            Trigger t;
            t.cls = "plat_field";
            plat_field(m, t.mins, t.maxs);
            t.owner = int(i);
            t.wait = 0;
            t.count = 0;
            trigs.push_back(t);
        }
    }
    return trigs;
}

}  // namespace q32x
