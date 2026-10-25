// Quake's files: pak archives, WAD2 lumps, BSP29 maps, the entity lump and
// alias models, read the way the original tools/mkmap.py read them.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "q32x/bytes.h"

namespace q32x {

// ------------------------------------------------------------ pak / wad

class Pak {
public:
    Pak() = default;
    static Pak load(const std::string &path);
    static Pak from_bytes(Bytes data);
    bool has(const std::string &name) const { return files_.count(lower(name)) != 0; }
    const Bytes &get(const std::string &name) const;
    std::vector<std::string> names() const;
    // add or replace a file (the editor layers its own files over the pak)
    void put(const std::string &name, Bytes data) { files_[lower(name)] = std::move(data); }

private:
    std::map<std::string, Bytes> files_;
};

std::map<std::string, Bytes> read_wad(const Bytes &d);

// ------------------------------------------------------------ bsp

using Vec3 = std::array<double, 3>;

inline double dot(const Vec3 &a, const Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

struct BspPlane {
    double n[3];
    double d;
    int type;
};
struct BspNode {
    int plane;
    int child[2];
    int mins[3], maxs[3];
    int firstface, numfaces;
};
struct BspTexinfo {
    double vecs[8];
    int miptex;
    int flags;
};
struct BspFace {
    int plane, side, firstedge, numedges, texinfo;
    int styles[4];
    int lightofs;
};
struct BspClipnode {
    int plane;
    int child[2];
};
struct BspLeaf {
    int contents, visofs;
    int mins[3], maxs[3];
    int firstmark, nummark;
    int ambient[4];
};
struct BspModel {
    double mins[3], maxs[3], origin[3];
    int headnode[4];
    int visleafs, firstface, numfaces;
};
struct BspTexture {
    std::string name;     // lower case
    int w = 0, h = 0;
    Bytes mips[4];
};

struct Bsp {
    std::vector<BspPlane> planes;
    std::vector<Vec3> verts;
    std::vector<BspNode> nodes;
    std::vector<BspTexinfo> texinfo;
    std::vector<BspFace> faces;
    Bytes lighting;
    std::vector<BspClipnode> clipnodes;
    std::vector<BspLeaf> leafs;
    std::vector<int> marksurfaces;
    std::vector<int> edges;       // two vertex indices per edge
    std::vector<int> surfedges;
    std::vector<BspModel> models;
    Bytes vis;
    std::string entities;
    std::vector<std::optional<BspTexture>> textures;

    static Bsp parse(const Bytes &b);
    // a face's polygon (its vertices in edge order)
    std::vector<Vec3> face_polygon(const BspFace &f) const;
    const BspTexture *face_texture(const BspFace &f) const
    {
        int t = texinfo.at(f.texinfo).miptex;
        return textures.at(t) ? &*textures.at(t) : nullptr;
    }
};

enum { CONTENTS_EMPTY = -1, CONTENTS_SOLID = -2, CONTENTS_WATER = -3, CONTENTS_SLIME = -4,
       CONTENTS_LAVA = -5, CONTENTS_SKY = -6 };

// ------------------------------------------------------------ entities

// one entity's keys; a key given twice keeps its last value, as in Python
struct Entity {
    std::map<std::string, std::string> kv;
    bool has(const std::string &k) const { return kv.count(k) != 0; }
    std::string get(const std::string &k, const std::string &def = "") const
    {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    // e.get(k) in Python returns None when absent: an empty optional here
    std::optional<std::string> opt(const std::string &k) const
    {
        auto it = kv.find(k);
        if (it == kv.end()) return std::nullopt;
        return it->second;
    }
    double num(const std::string &k, double def) const { return has(k) ? pyfloat(get(k)) : def; }
    Vec3 vec(const std::string &k) const;
    std::string classname() const { return get("classname"); }
};

std::vector<Entity> parse_entities(const std::string &s);
std::string write_entities(const std::vector<Entity> &ents);

// ------------------------------------------------------------ palette

struct Palette {
    uint8_t rgb[768];
    static Palette from(const Bytes &b);
    // the nearest of the first `limit` colours (squared RGB distance)
    int nearest(double r, double g, double b, int limit = 224) const;
};

}  // namespace q32x
