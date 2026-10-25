#include "q32x/quake.h"

namespace q32x {

Pak Pak::load(const std::string &path) { return from_bytes(read_file(path)); }

Pak Pak::from_bytes(Bytes d)
{
    Pak p;
    check(d.size() >= 12 && std::memcmp(d.data(), "PACK", 4) == 0, "not a pak file");
    Reader r(d);
    int32_t off = r.i32(4), ln = r.i32(8);
    for (int i = 0; i < ln / 64; i++) {
        size_t e = size_t(off) + size_t(i) * 64;
        std::string name = lower(r.name(e, 56));
        int32_t fp = r.i32(e + 56), fl = r.i32(e + 60);
        p.files_[name] = r.slice(size_t(fp), size_t(fl));
    }
    return p;
}

const Bytes &Pak::get(const std::string &name) const
{
    auto it = files_.find(lower(name));
    if (it == files_.end()) fail("not in the pak: " + name);
    return it->second;
}

std::vector<std::string> Pak::names() const
{
    std::vector<std::string> v;
    for (auto &kv : files_) v.push_back(kv.first);
    return v;
}

std::map<std::string, Bytes> read_wad(const Bytes &d)
{
    check(d.size() >= 12 && std::memcmp(d.data(), "WAD2", 4) == 0, "not a WAD2 file");
    Reader r(d);
    int32_t n = r.i32(4), ofs = r.i32(8);
    std::map<std::string, Bytes> lumps;
    for (int i = 0; i < n; i++) {
        size_t e = size_t(ofs) + size_t(i) * 32;
        int32_t fp = r.i32(e), dsz = r.i32(e + 4);
        std::string name = lower(r.name(e + 16, 16));
        lumps[name] = r.slice(size_t(fp), size_t(dsz));
    }
    return lumps;
}

// ------------------------------------------------------------ bsp

Bsp Bsp::parse(const Bytes &b)
{
    Reader r(b);
    check(r.i32(0) == 29, "BSP version " + std::to_string(r.i32(0)));
    Bytes L[15];
    for (int i = 0; i < 15; i++) {
        int32_t o = r.i32(4 + i * 8), l = r.i32(8 + i * 8);
        L[i] = r.slice(size_t(o), size_t(l));
    }
    enum { ENT, PLANES, TEX, VERTS, VIS, NODES, TEXINFO, FACES, LIGHT, CLIP, LEAFS, MARKS, EDGES,
           SURFEDGES, MODELS };
    Bsp m;
    {
        Reader p(L[PLANES]);
        for (size_t i = 0; i + 20 <= L[PLANES].size(); i += 20)
            m.planes.push_back({{p.f32(i), p.f32(i + 4), p.f32(i + 8)}, p.f32(i + 12), p.i32(i + 16)});
    }
    {
        Reader p(L[VERTS]);
        for (size_t i = 0; i + 12 <= L[VERTS].size(); i += 12)
            m.verts.push_back({p.f32(i), p.f32(i + 4), p.f32(i + 8)});
    }
    {
        Reader p(L[NODES]);
        for (size_t i = 0; i + 24 <= L[NODES].size(); i += 24) {
            BspNode n;
            n.plane = p.i32(i);
            n.child[0] = p.i16(i + 4);
            n.child[1] = p.i16(i + 6);
            for (int k = 0; k < 3; k++) {
                n.mins[k] = p.i16(i + 8 + 2 * k);
                n.maxs[k] = p.i16(i + 14 + 2 * k);
            }
            n.firstface = p.u16(i + 20);
            n.numfaces = p.u16(i + 22);
            m.nodes.push_back(n);
        }
    }
    {
        Reader p(L[TEXINFO]);
        for (size_t i = 0; i + 40 <= L[TEXINFO].size(); i += 40) {
            BspTexinfo t;
            for (int k = 0; k < 8; k++) t.vecs[k] = p.f32(i + 4 * k);
            t.miptex = p.i32(i + 32);
            t.flags = p.i32(i + 36);
            m.texinfo.push_back(t);
        }
    }
    {
        Reader p(L[FACES]);
        for (size_t i = 0; i + 20 <= L[FACES].size(); i += 20) {
            BspFace f;
            f.plane = p.i16(i);
            f.side = p.i16(i + 2);
            f.firstedge = p.i32(i + 4);
            f.numedges = p.i16(i + 8);
            f.texinfo = p.i16(i + 10);
            for (int k = 0; k < 4; k++) f.styles[k] = p.u8(i + 12 + k);
            f.lightofs = p.i32(i + 16);
            m.faces.push_back(f);
        }
    }
    m.lighting = L[LIGHT];
    {
        Reader p(L[CLIP]);
        for (size_t i = 0; i + 8 <= L[CLIP].size(); i += 8)
            m.clipnodes.push_back({p.i32(i), {p.i16(i + 4), p.i16(i + 6)}});
    }
    {
        Reader p(L[LEAFS]);
        for (size_t i = 0; i + 28 <= L[LEAFS].size(); i += 28) {
            BspLeaf lf;
            lf.contents = p.i32(i);
            lf.visofs = p.i32(i + 4);
            for (int k = 0; k < 3; k++) {
                lf.mins[k] = p.i16(i + 8 + 2 * k);
                lf.maxs[k] = p.i16(i + 14 + 2 * k);
            }
            lf.firstmark = p.u16(i + 20);
            lf.nummark = p.u16(i + 22);
            for (int k = 0; k < 4; k++) lf.ambient[k] = p.u8(i + 24 + k);
            m.leafs.push_back(lf);
        }
    }
    {
        Reader p(L[MARKS]);
        for (size_t i = 0; i + 2 <= L[MARKS].size(); i += 2) m.marksurfaces.push_back(p.u16(i));
    }
    {
        Reader p(L[EDGES]);
        for (size_t i = 0; i + 2 <= L[EDGES].size(); i += 2) m.edges.push_back(p.u16(i));
    }
    {
        Reader p(L[SURFEDGES]);
        for (size_t i = 0; i + 4 <= L[SURFEDGES].size(); i += 4) m.surfedges.push_back(p.i32(i));
    }
    {
        Reader p(L[MODELS]);
        for (size_t i = 0; i + 64 <= L[MODELS].size(); i += 64) {
            BspModel mo;
            for (int k = 0; k < 3; k++) {
                mo.mins[k] = p.f32(i + 4 * k);
                mo.maxs[k] = p.f32(i + 12 + 4 * k);
                mo.origin[k] = p.f32(i + 24 + 4 * k);
            }
            for (int k = 0; k < 4; k++) mo.headnode[k] = p.i32(i + 36 + 4 * k);
            mo.visleafs = p.i32(i + 52);
            mo.firstface = p.i32(i + 56);
            mo.numfaces = p.i32(i + 60);
            m.models.push_back(mo);
        }
    }
    m.vis = L[VIS];
    {
        auto &e = L[ENT];
        size_t k = 0;
        while (k < e.size() && e[k]) k++;
        m.entities.assign(reinterpret_cast<const char *>(e.data()), k);
    }
    {
        auto &T = L[TEX];
        Reader p(T);
        if (T.size() >= 4) {
            int32_t n = p.i32(0);
            for (int i = 0; i < n; i++) {
                int32_t o = p.i32(4 + 4 * i);
                if (o < 0) {
                    m.textures.push_back(std::nullopt);
                    continue;
                }
                BspTexture t;
                t.name = lower(p.name(size_t(o), 16));
                t.w = int(p.u32(size_t(o) + 16));
                t.h = int(p.u32(size_t(o) + 20));
                for (int k = 0; k < 4; k++) {
                    uint32_t mo = p.u32(size_t(o) + 24 + 4 * k);
                    t.mips[k] = p.slice(size_t(o) + mo, size_t(t.w >> k) * size_t(t.h >> k));
                }
                m.textures.push_back(std::move(t));
            }
        }
    }
    return m;
}

std::vector<Vec3> Bsp::face_polygon(const BspFace &f) const
{
    std::vector<Vec3> pts;
    for (int k = 0; k < f.numedges; k++) {
        int e = surfedges.at(size_t(f.firstedge + k));
        int vi = e >= 0 ? edges.at(size_t(2 * e)) : edges.at(size_t(2 * -e + 1));
        pts.push_back(verts.at(size_t(vi)));
    }
    return pts;
}

// ------------------------------------------------------------ entities

Vec3 Entity::vec(const std::string &k) const
{
    auto v = split_ws(get(k));
    check(v.size() >= 3, "entity key '" + k + "' is not a vector");
    return {pyfloat(v[0]), pyfloat(v[1]), pyfloat(v[2])};
}

std::vector<Entity> parse_entities(const std::string &s)
{
    std::vector<std::string> toks;
    std::vector<bool> quoted;
    size_t i = 0;
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (i < s.size()) {
        char c = s[i];
        if (ws(c))
            i++;
        else if (c == '{' || c == '}') {
            toks.emplace_back(1, c);
            quoted.push_back(false);
            i++;
        } else if (c == '"') {
            size_t j = s.find('"', i + 1);
            check(j != std::string::npos, "unterminated string in the entities");
            toks.push_back(s.substr(i + 1, j - i - 1));
            quoted.push_back(true);
            i = j + 1;
        } else {
            size_t j = i;
            while (j < s.size() && !ws(s[j]) && s[j] != '{' && s[j] != '}' && s[j] != '"') j++;
            toks.push_back(s.substr(i, j - i));
            quoted.push_back(false);
            i = j;
        }
    }
    std::vector<Entity> ents;
    Entity cur;
    size_t k = 0;
    while (k < toks.size()) {
        const auto &t = toks[k];
        // (as in the Python tokenizer, a quoted "{" counts as a brace)
        if (t == "{") {
            cur = Entity();
            k++;
        } else if (t == "}") {
            ents.push_back(cur);
            k++;
        } else {
            check(k + 1 < toks.size(), "entity key without a value");
            cur.kv[t] = toks[k + 1];
            k += 2;
        }
    }
    return ents;
}

std::string write_entities(const std::vector<Entity> &ents)
{
    std::string out;
    for (auto &e : ents) {
        out += "{\n";
        for (auto &kv : e.kv) out += "\"" + kv.first + "\" \"" + kv.second + "\"\n";
        out += "}\n";
    }
    return out;
}

Palette Palette::from(const Bytes &b)
{
    check(b.size() >= 768, "palette too short");
    Palette p;
    std::memcpy(p.rgb, b.data(), 768);
    return p;
}

int Palette::nearest(double r, double g, double b, int limit) const
{
    double best = 1e18;
    int bi = 0;
    for (int i = 0; i < limit; i++) {
        double dr = rgb[3 * i] - r, dg = rgb[3 * i + 1] - g, db = rgb[3 * i + 2] - b;
        double dd = dr * dr + dg * dg + db * db;
        if (dd < best) {
            best = dd;
            bi = i;
        }
    }
    return bi;
}

}  // namespace q32x
