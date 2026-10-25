#include "q32x/demo.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "q32x/gamedata.h"

namespace q32x {

static const char *SVC[] = {"bad", "nop", "disconnect", "updatestat", "version", "setview", "sound", "time",
                            "print", "stufftext", "setangle", "serverinfo", "lightstyle", "updatename",
                            "updatefrags", "clientdata", "stopsound", "updatecolors", "particle", "damage",
                            "spawnstatic", "spawnbinary", "spawnbaseline", "temp_entity", "setpause",
                            "signonnum", "centerprint", "killedmonster", "foundsecret", "spawnstaticsound",
                            "intermission", "finale", "cdtrack", "sellscreen"};
static const int NSVC = int(sizeof SVC / sizeof SVC[0]);

static const char *te_name(int t)
{
    static const char *names[] = {"spike", "superspike", "gunshot", "explosion", "tarexplosion", "lightning1",
                                  "lightning2", "wizspike", "knightspike", "lightning3", "lavasplash", "teleport"};
    return t >= 0 && t < 12 ? names[t] : nullptr;
}

enum {
    U_MOREBITS = 1, U_ORIGIN1 = 2, U_ORIGIN2 = 4, U_ORIGIN3 = 8, U_ANGLE2 = 16, U_NOLERP = 32, U_FRAME = 64,
    U_SIGNAL = 128, U_ANGLE1 = 256, U_ANGLE3 = 512, U_MODEL = 1024, U_COLORMAP = 2048, U_SKIN = 4096,
    U_EFFECTS = 8192, U_LONGENTITY = 16384
};
enum {
    SU_VIEWHEIGHT = 1, SU_IDEALPITCH = 2, SU_PUNCH1 = 4, SU_VELOCITY1 = 32, SU_ITEMS = 512, SU_ONGROUND = 1024,
    SU_INWATER = 2048, SU_WEAPONFRAME = 4096, SU_ARMOR = 8192, SU_WEAPON = 16384
};

namespace {
struct Msg {
    const Bytes &d;
    size_t o = 0;
    bool more() const { return o < d.size(); }
    int byte()
    {
        check(o < d.size(), "demo message ends early");
        return d[o++];
    }
    int chr()
    {
        int v = byte();
        return v >= 128 ? v - 256 : v;
    }
    int shrt()
    {
        Reader r(d);
        int v = r.i16(o);
        o += 2;
        return v;
    }
    int lng()
    {
        Reader r(d);
        int v = r.i32(o);
        o += 4;
        return v;
    }
    double flt()
    {
        Reader r(d);
        double v = r.f32(o);
        o += 4;
        return v;
    }
    double coord() { return shrt() / 8.0; }
    double angle() { return chr() * 360.0 / 256; }
    std::string string()
    {
        size_t e = o;
        while (e < d.size() && d[e]) e++;
        check(e < d.size(), "unterminated string in a demo");
        std::string s(reinterpret_cast<const char *>(d.data() + o), e - o);
        o = e + 1;
        return s;
    }
};
}  // namespace

static std::vector<DemMsg> parse_message(Msg &m)
{
    std::vector<DemMsg> out;
    while (m.more()) {
        int cmd = m.byte();
        DemMsg r;
        auto &F = r.fields;
        if (cmd & U_SIGNAL) {
            int bits = cmd & 127;
            if (bits & U_MOREBITS) bits |= m.byte() << 8;
            int num = (bits & U_LONGENTITY) ? m.shrt() : m.byte();
            r.name = "update";
            r.num = {double(num)};
            if (bits & U_MODEL) F["model"] = m.byte();
            if (bits & U_FRAME) F["frame"] = m.byte();
            if (bits & U_COLORMAP) F["colormap"] = m.byte();
            if (bits & U_SKIN) F["skin"] = m.byte();
            if (bits & U_EFFECTS) F["effects"] = m.byte();
            if (bits & U_ORIGIN1) F["o0"] = m.coord();
            if (bits & U_ANGLE1) F["a0"] = m.angle();
            if (bits & U_ORIGIN2) F["o1"] = m.coord();
            if (bits & U_ANGLE2) F["a1"] = m.angle();
            if (bits & U_ORIGIN3) F["o2"] = m.coord();
            if (bits & U_ANGLE3) F["a2"] = m.angle();
            if (bits & U_NOLERP) F["nolerp"] = 1;
            out.push_back(std::move(r));
            continue;
        }
        r.name = cmd < NSVC ? SVC[cmd] : "cmd" + std::to_string(cmd);
        const std::string &n = r.name;
        if (n == "nop" || n == "disconnect" || n == "killedmonster" || n == "foundsecret" || n == "intermission" ||
            n == "sellscreen") {
        } else if (n == "updatestat") {
            int a = m.byte();
            int b = m.lng();
            r.num = {double(a), double(b)};
        } else if (n == "version") {
            r.num = {double(m.lng())};
        } else if (n == "setview") {
            r.num = {double(m.shrt())};
        } else if (n == "sound") {
            int mask = m.byte();
            int vol = (mask & 1) ? m.byte() : 255;
            double att = (mask & 2) ? m.byte() / 64.0 : 1.0;
            int ch = m.shrt();
            int snd = m.byte();
            double p0 = m.coord(), p1 = m.coord(), p2 = m.coord();
            r.num = {double(ch >> 3), double(ch & 7), double(snd), double(vol), att, p0, p1, p2};
        } else if (n == "time") {
            r.num = {m.flt()};
        } else if (n == "print" || n == "stufftext" || n == "centerprint" || n == "finale") {
            r.str = {m.string()};
        } else if (n == "setangle") {
            double a = m.angle(), b = m.angle(), c = m.angle();
            r.num = {a, b, c};
        } else if (n == "serverinfo") {
            int proto = m.lng();
            int maxc = m.byte();
            int gt = m.byte();
            r.num = {double(proto), double(maxc), double(gt)};
            r.str = {m.string()};
            r.models = {""};
            for (;;) {
                std::string s = m.string();
                if (s.empty()) break;
                r.models.push_back(s);
            }
            r.sounds = {""};
            for (;;) {
                std::string s = m.string();
                if (s.empty()) break;
                r.sounds.push_back(s);
            }
        } else if (n == "lightstyle" || n == "updatename") {
            int a = m.byte();
            r.num = {double(a)};
            r.str = {m.string()};
        } else if (n == "updatefrags") {
            int a = m.byte();
            int b = m.shrt();
            r.num = {double(a), double(b)};
        } else if (n == "clientdata") {
            int bits = m.shrt();
            F["bits"] = bits;
            F["viewheight"] = (bits & SU_VIEWHEIGHT) ? m.chr() : 22;
            F["idealpitch"] = (bits & SU_IDEALPITCH) ? m.chr() : 0;
            for (int i = 0; i < 3; i++) {
                double punch = 0, vel = 0;
                if (bits & (SU_PUNCH1 << i)) punch = m.chr();
                if (bits & (SU_VELOCITY1 << i)) vel = m.chr() * 16;
                F["punch" + std::to_string(i)] = punch;
                F["vel" + std::to_string(i)] = vel;
            }
            F["items"] = m.lng();
            F["onground"] = (bits & SU_ONGROUND) ? 1 : 0;
            F["inwater"] = (bits & SU_INWATER) ? 1 : 0;
            F["weaponframe"] = (bits & SU_WEAPONFRAME) ? m.byte() : 0;
            F["armor"] = (bits & SU_ARMOR) ? m.byte() : 0;
            F["weapon"] = (bits & SU_WEAPON) ? m.byte() : 0;
            F["health"] = m.shrt();
            F["ammo"] = m.byte();
            F["shells"] = m.byte();
            F["nails"] = m.byte();
            F["rockets"] = m.byte();
            F["cells"] = m.byte();
            F["activeweapon"] = m.byte();
        } else if (n == "stopsound") {
            int v = m.shrt();
            r.num = {double(v >> 3), double(v & 7)};
        } else if (n == "updatecolors") {
            int a = m.byte();
            int b = m.byte();
            r.num = {double(a), double(b)};
        } else if (n == "particle") {
            double p0 = m.coord(), p1 = m.coord(), p2 = m.coord();
            double d0 = m.chr() / 16.0, d1 = m.chr() / 16.0, d2 = m.chr() / 16.0;
            int count = m.byte();
            int colour = m.byte();
            r.num = {p0, p1, p2, d0, d1, d2, double(count), double(colour)};
        } else if (n == "damage") {
            int armor = m.byte();
            int blood = m.byte();
            double p0 = m.coord(), p1 = m.coord(), p2 = m.coord();
            r.num = {double(armor), double(blood), p0, p1, p2};
        } else if (n == "spawnstatic" || n == "spawnbaseline") {
            int num = 0;
            if (n == "spawnbaseline") {
                num = m.shrt();
                r.has_num2 = true;
            }
            F["num"] = num;
            F["model"] = m.byte();
            F["frame"] = m.byte();
            F["colormap"] = m.byte();
            F["skin"] = m.byte();
            for (int i = 0; i < 3; i++) {
                F["org" + std::to_string(i)] = m.coord();
                F["ang" + std::to_string(i)] = m.angle();
            }
        } else if (n == "temp_entity") {
            int t = m.byte();
            F["kind"] = t;
            if (t == 5 || t == 6 || t == 9 || t == 13) {
                F["ent"] = m.shrt();
                double s0 = m.coord(), s1 = m.coord(), s2 = m.coord();
                double e0 = m.coord(), e1 = m.coord(), e2 = m.coord();
                r.num = {s0, s1, s2, e0, e1, e2};
            } else {
                double p0 = m.coord(), p1 = m.coord(), p2 = m.coord();
                r.num = {p0, p1, p2};
            }
        } else if (n == "setpause" || n == "signonnum") {
            r.num = {double(m.byte())};
        } else if (n == "spawnstaticsound") {
            double p0 = m.coord(), p1 = m.coord(), p2 = m.coord();
            int snd = m.byte();
            int vol = m.byte();
            double att = m.byte() / 64.0;
            r.num = {p0, p1, p2, double(snd), double(vol), att};
        } else if (n == "cdtrack") {
            int a = m.byte();
            int b = m.byte();
            r.num = {double(a), double(b)};
        } else {
            fail("unknown svc " + std::to_string(cmd) + " at " + std::to_string(m.o));
        }
        out.push_back(std::move(r));
    }
    return out;
}

Dem Dem::parse(const Bytes &data)
{
    Dem dem;
    auto nl = std::find(data.begin(), data.end(), uint8_t('\n'));
    check(nl != data.end(), "not a Quake demo");
    size_t o = size_t(nl - data.begin()) + 1;
    dem.track.assign(data.begin(), nl);
    Reader r(data);
    while (o + 16 <= data.size()) {
        int32_t size = r.i32(o);
        DemFrame f;
        for (int k = 0; k < 3; k++) f.angles[k] = r.f32(o + 4 + 4 * size_t(k));
        Bytes body = r.slice(o + 16, size_t(size));
        o += 16 + size_t(size);
        Msg m{body};
        f.msgs = parse_message(m);
        dem.frames.push_back(std::move(f));
    }
    return dem;
}

// ------------------------------------------------------------ the stream

static const std::map<std::string, int> &te_keep()
{
    static const std::map<std::string, int> m = {{"spike", 0}, {"superspike", 1}, {"gunshot", 2},
                                                 {"explosion", 3}, {"tarexplosion", 4}, {"wizspike", 7},
                                                 {"knightspike", 8}, {"lavasplash", 10}, {"teleport", 11}};
    return m;
}
static const std::map<std::string, int> &view_weapons()
{
    static const std::map<std::string, int> m = {{"progs/v_axe.mdl", 0}, {"progs/v_shot.mdl", 1},
                                                 {"progs/v_shot2.mdl", 2}, {"progs/v_nail.mdl", 3},
                                                 {"progs/v_rock.mdl", 4}};
    return m;
}
constexpr int NOT_DRAWN = 255;
// the model mapping as one number: a model id, MOVER + mover, VIEW + weapon
constexpr int MAP_MOVER = 1000, MAP_VIEW = 2000;

static int64_t i16c(double v)
{
    int64_t r = pyround(v * 8);
    return std::max<int64_t>(-32768, std::min<int64_t>(32767, r));
}
static int ang8(double a) { return int(floormod(pyround(a * 256 / 360), 256)); }

DemoStream convert_demo(const Bytes &data, const std::vector<Mover> &movers, std::string *log)
{
    Dem dem = Dem::parse(data);
    std::map<int, int> mover_of;
    for (size_t i = 0; i < movers.size(); i++)
        if (!mover_of.count(movers[i].model)) mover_of[movers[i].model] = int(i);
    // (a Python dict comprehension keeps the last: do the same)
    for (size_t i = 0; i < movers.size(); i++) mover_of[movers[i].model] = int(i);
    std::map<int, int> mdl_map, snd_map;
    int view_ent = 1;
    std::set<std::string> need_m, need_s;
    struct Base {
        int model = 0, frame = 0, skin = 0;
        double org[3] = {0, 0, 0}, ang[3] = {0, 0, 0};
    };
    std::map<int, Base> baselines;
    Buf out(true);
    Bytes strings;
    int nframes = 0, max_ents = 0;
    std::string pending_print;
    std::map<std::string, size_t> printed;
    auto map_model = [&](int i) {
        auto it = mdl_map.find(i);
        return it == mdl_map.end() ? NOT_DRAWN : it->second;
    };
    auto string_ofs = [&](const std::string &t) {
        auto it = printed.find(t);
        if (it != printed.end()) return it->second;
        size_t o = strings.size();
        printed[t] = o;
        strings.insert(strings.end(), t.begin(), t.end());
        strings.push_back(0);
        return o;
    };
    double last_time = 0;
    Buf pending_rec(true);
    bool seen_view = false;
    for (auto &f : dem.frames) {
        Buf rec(true);
        std::optional<double> time;
        int nents = 0;
        bool has_view = false;
        for (auto &m : f.msgs) {
            const std::string &k = m.name;
            auto F = [&](const char *key) { return m.fields.at(key); };
            if (k == "serverinfo") {
                for (size_t i = 0; i < m.models.size(); i++) {
                    const std::string &p = m.models[i];
                    if (starts_with(p, "*")) {
                        int mi = std::stoi(p.substr(1));
                        if (mover_of.count(mi)) mdl_map[int(i)] = MAP_MOVER + mover_of[mi];
                    } else {
                        for (size_t j = 0; j < MODELS.size(); j++)
                            if (MODELS[j].path == p) mdl_map[int(i)] = int(j);
                    }
                    auto vw = view_weapons().find(p);
                    if (vw != view_weapons().end()) mdl_map[int(i)] = MAP_VIEW + vw->second;
                }
                for (size_t i = 0; i < m.sounds.size(); i++) {
                    std::string s = m.sounds[i];
                    if (ends_with(s, ".wav")) s = s.substr(0, s.size() - 4);
                    int si = sound_index(s);
                    if (si >= 0) snd_map[int(i)] = si;
                }
            } else if (k == "setview") {
                view_ent = int(m.num[0]);
            } else if (k == "spawnbaseline") {
                Base b;
                b.model = int(F("model"));
                b.frame = int(F("frame"));
                b.skin = int(F("skin"));
                for (int i = 0; i < 3; i++) {
                    b.org[i] = F(("org" + std::to_string(i)).c_str());
                    b.ang[i] = F(("ang" + std::to_string(i)).c_str());
                }
                baselines[int(F("num"))] = b;
            } else if (k == "time") {
                time = m.num[0];
            } else if (k == "update") {
                int num = int(m.num[0]);
                const auto &fl = m.fields;
                auto get = [&](const std::string &key, double def) {
                    auto it = fl.find(key);
                    return it == fl.end() ? def : it->second;
                };
                Base b;
                auto bit = baselines.find(num);
                if (bit != baselines.end()) b = bit->second;
                int model = int(get("model", b.model));
                double org[3];
                for (int i = 0; i < 3; i++) org[i] = get("o" + std::to_string(i), b.org[i]);
                auto mmit = mdl_map.find(model);
                bool has_mm = mmit != mdl_map.end();
                int mm = has_mm ? mmit->second : -1;
                if (num == view_ent) {
                    rec.u8(3);
                    for (double c : org) rec.i16(i16c(c));
                    has_view = true;
                } else if (has_mm && mm >= MAP_MOVER && mm < MAP_VIEW) {
                    rec.u8(2);
                    rec.u8(mm - MAP_MOVER);
                    for (double c : org) rec.i16(i16c(c));
                } else if (has_mm && mm < MAP_MOVER) {
                    int bits = 0;
                    Bytes body;
                    int bm = map_model(b.model);
                    if (map_model(model) != bm) {
                        bits |= 1;
                        int v = map_model(model);
                        check(v < 256, "demo model mapping");
                        body.push_back(uint8_t(v));
                    }
                    if (get("frame", b.frame) != b.frame) {
                        bits |= 2;
                        body.push_back(uint8_t(fl.at("frame")));
                    }
                    if (get("skin", b.skin) != b.skin) {
                        bits |= 4;
                        body.push_back(uint8_t(fl.at("skin")));
                    }
                    if (org[0] != b.org[0] || org[1] != b.org[1] || org[2] != b.org[2]) {
                        bits |= 8;
                        Buf t(true);
                        for (double c : org) t.i16(i16c(c));
                        body.insert(body.end(), t.data().begin(), t.data().end());
                    }
                    double yaw = get("a1", b.ang[1]);
                    double pitch = get("a0", b.ang[0]);
                    if (ang8(yaw) != ang8(b.ang[1])) {
                        bits |= 16;
                        body.push_back(uint8_t(ang8(yaw)));
                    }
                    if (ang8(pitch) != ang8(b.ang[0])) {
                        bits |= 32;
                        body.push_back(uint8_t(ang8(pitch)));
                    }
                    if (get("effects", 0)) {
                        bits |= 64;
                        body.push_back(uint8_t(fl.at("effects")));
                    }
                    rec.u8(1);
                    rec.u16(num);
                    rec.u8(bits);
                    rec.bytes(body);
                    need_m.insert(MODELS.at(size_t(mm)).name);
                    nents++;
                }
            } else if (k == "sound") {
                int snd = int(m.num[2]);
                auto it = snd_map.find(snd);
                if (it != snd_map.end()) {
                    rec.u8(4);
                    rec.u8(it->second);
                    rec.u8(int64_t(m.num[3]));
                    rec.u8(int64_t(std::trunc(m.num[4] * 64)));
                    for (int i = 0; i < 3; i++) rec.i16(i16c(m.num[size_t(5 + i)]));
                    need_s.insert(SOUNDS.at(size_t(it->second)));
                }
            } else if (k == "particle") {
                rec.u8(5);
                for (int i = 0; i < 3; i++) rec.i16(i16c(m.num[size_t(i)]));
                for (int i = 0; i < 3; i++)
                    rec.i8(std::max<int64_t>(-128, std::min<int64_t>(127, pyround(m.num[size_t(3 + i)] * 16))));
                rec.u8(int64_t(m.num[6]));
                rec.u8(int64_t(m.num[7]));
            } else if (k == "temp_entity") {
                const char *nm = te_name(int(F("kind")));
                if (nm) {
                    auto it = te_keep().find(nm);
                    if (it != te_keep().end()) {
                        rec.u8(6);
                        rec.u8(it->second);
                        for (int i = 0; i < 3; i++) rec.i16(i16c(m.num[size_t(i)]));
                    }
                }
            } else if (k == "clientdata") {
                auto wit = mdl_map.find(int(F("weapon")));
                int w = (wit != mdl_map.end() && wit->second >= MAP_VIEW) ? wit->second - MAP_VIEW : 255;
                int64_t qitems = int64_t(F("items"));
                int items = 0;
                if (qitems & 131072) items |= 1;
                if (qitems & 262144) items |= 2;
                if (qitems & 524288) items |= 4;
                if (qitems & 4194304) items |= 8;
                if (qitems & 2097152) items |= 16;
                rec.u8(7);
                rec.i16(int64_t(F("health")));
                rec.u8(int64_t(F("armor")));
                rec.u8(int64_t(F("ammo")));
                rec.u8(w);
                rec.u8(int64_t(F("weaponframe")));
                rec.u8(items);
                rec.i8(int64_t(F("viewheight")));
            } else if (k == "damage") {
                rec.u8(8);
                rec.u8(std::min<int64_t>(255, int64_t(m.num[0] + m.num[1])));
            } else if (k == "stufftext") {
                if (strip(m.str[0]) == "bf") rec.u8(9);
            } else if (k == "print" || k == "centerprint") {
                pending_print += m.str[0];
                size_t nlp;
                while ((nlp = pending_print.find('\n')) != std::string::npos) {
                    std::string line = strip(pending_print.substr(0, nlp));
                    pending_print = pending_print.substr(nlp + 1);
                    if (!line.empty()) {
                        rec.u8(10);
                        rec.u16(int64_t(string_ofs(line.substr(0, 38))));
                    }
                }
            } else if (k == "updatestat") {
                int which = -1;
                switch (int(m.num[0])) {
                case 14: which = 0; break;
                case 12: which = 1; break;
                case 13: which = 2; break;
                case 11: which = 3; break;
                }
                if (which >= 0) {
                    rec.u8(11);
                    rec.u8(which);
                    rec.u16(int64_t(m.num[1]));
                }
            } else if (k == "killedmonster") {
                rec.u8(11);
                rec.u8(4);
                rec.u16(0);   // one more kill
            } else if (k == "foundsecret") {
                rec.u8(11);
                rec.u8(5);
                rec.u16(0);
            }
        }
        if (!time || !(seen_view || has_view)) {
            // a block with no time of its own (reliable messages: kills,
            // prints), or one from before the view has a place (sign-on,
            // when Quake draws nothing): its records go with the next frame
            pending_rec.bytes(rec.data());
            continue;
        }
        seen_view = true;
        out.u16(pyround(*time * 100));
        out.u16(floormod(pyround(double(f.angles[0]) * 65536 / 360), 65536));
        out.u16(floormod(pyround(double(f.angles[1]) * 65536 / 360), 65536));
        out.bytes(pending_rec.data());
        out.bytes(rec.data());
        out.u8(0);
        pending_rec.data().clear();
        nframes++;
        last_time = *time;
        max_ents = std::max(max_ents, nents);
    }
    int nb = baselines.empty() ? 0 : baselines.rbegin()->first + 1;
    Buf base(true);
    for (int i = 0; i < nb; i++) {
        auto it = baselines.find(i);
        if (it == baselines.end() || i == view_ent) {
            base.u8(NOT_DRAWN);
            base.zeros(11);
            continue;
        }
        const Base &b = it->second;
        auto mit = mdl_map.find(b.model);
        int model = (mit != mdl_map.end() && mit->second < MAP_MOVER) ? mit->second : NOT_DRAWN;
        if (mit != mdl_map.end() && mit->second < MAP_MOVER) need_m.insert(MODELS.at(size_t(mit->second)).name);
        base.u8(model);
        base.u8(b.frame);
        base.u8(b.skin);
        base.u8(ang8(b.ang[0]));
        for (double c : b.org) base.i16(i16c(c));
        base.u8(ang8(b.ang[1]));
        base.u8(0);
    }
    const size_t hdr = 6 * 4;
    Bytes body = base.data();
    body.insert(body.end(), out.data().begin(), out.data().end());
    while (body.size() % 4) body.push_back(0);
    Buf blob(true);
    blob.u32(nframes);
    blob.u32(nb);
    blob.u32(int64_t(hdr));
    blob.u32(int64_t(hdr + base.size()));
    blob.u32(int64_t(hdr + body.size()));
    blob.u32(max_ents);
    blob.bytes(body);
    blob.bytes(strings);
    if (log) {
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "demo: %d frames, %.1f s, %d baselines, %zu KB (frames %zu KB), up to %d entities a frame\n",
                      nframes, last_time, nb, blob.size() / 1024, out.size() / 1024, max_ents);
        *log += buf;
    }
    DemoStream d;
    d.data = blob.data();
    d.count = nframes;
    d.models.assign(need_m.begin(), need_m.end());
    d.sounds.assign(need_s.begin(), need_s.end());
    d.seconds = last_time;
    return d;
}

}  // namespace q32x
