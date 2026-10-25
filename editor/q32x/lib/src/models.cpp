#include "q32x/models.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace q32x {

FlatModel trim_frames(const FlatModel &m, const std::vector<int> &keep_in)
{
    int nf = m.logical_frames();
    std::set<int> keep;
    for (int k : keep_in)
        if (k >= 0 && k < nf) keep.insert(k);
    if (keep.empty() || int(keep.size()) == nf) return m;
    keep.insert(0);
    FlatModel o = m;
    o.frames.clear();
    std::map<int, int> stored;   // logical frame kept -> stored index
    for (int k : keep) {
        int src = m.frame_map.empty() ? k : m.frame_map[size_t(k)];
        stored[k] = int(o.frames.size());
        o.frames.push_back(m.frames.at(size_t(src)));
    }
    o.frame_map.assign(size_t(nf), 0);
    int cur = 0;
    for (int f = 0; f < nf; f++) {
        if (stored.count(f)) cur = stored[f];
        o.frame_map[size_t(f)] = cur;
    }
    return o;
}

std::vector<int> every_nth_frame(const std::vector<std::string> &names, int step)
{
    std::vector<int> keep;
    step = std::max(1, step);
    std::string prev;
    int run = 0;
    for (size_t i = 0; i < names.size(); i++) {
        std::string base = names[i];
        while (!base.empty() && base.back() >= '0' && base.back() <= '9') base.pop_back();
        if (i == 0 || base != prev) run = 0;
        if (run % step == 0) keep.push_back(int(i));
        run++;
        prev = base;
    }
    return keep;
}

std::vector<std::string> model_frame_names(const Bytes &d)
{
    Reader r(d);
    std::vector<std::string> names;
    if (d.size() < 84) return names;
    int nskins = r.i32(48), sw = r.i32(52), sh = r.i32(56), nverts = r.i32(60), ntris = r.i32(64), nframes = r.i32(68);
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

// Edge collapse: the pair of vertices whose merging moves the shape least
// over all the frames goes first; triangles that become lines drop out.
FlatModel simplify_model(const FlatModel &m, double ratio)
{
    if (ratio >= 1.0 || m.tris.empty()) return m;
    int nv = m.nverts;
    size_t target = std::max<size_t>(4, size_t(std::lround(m.tris.size() * std::clamp(ratio, 0.05, 1.0))));
    std::vector<int> repl(static_cast<size_t>(nv));
    for (int i = 0; i < nv; i++) repl[size_t(i)] = i;
    auto find = [&](int v) {
        while (repl[size_t(v)] != v) v = repl[size_t(v)];
        return v;
    };
    auto pos = [&](const Bytes &f, int v, int k) { return double(f[size_t(4 * v + k)]) * m.scale[k]; };
    std::vector<std::array<int, 3>> tris = m.tris;
    std::vector<int> tri_alive(tris.size(), 1);
    size_t alive = tris.size();
    while (alive > target) {
        // the cheapest edge: the largest move of either end over the frames
        double best = 1e300;
        int ba = -1, bb = -1;
        std::set<std::pair<int, int>> edges;
        for (size_t t = 0; t < tris.size(); t++) {
            if (!tri_alive[t]) continue;
            for (int k = 0; k < 3; k++) {
                int a = find(tris[t][size_t(k)]), b = find(tris[t][size_t((k + 1) % 3)]);
                if (a == b) continue;
                edges.insert({std::min(a, b), std::max(a, b)});
            }
        }
        for (auto &e : edges) {
            double cost = 0;
            for (auto &f : m.frames) {
                double d = 0;
                for (int k = 0; k < 3; k++) {
                    double x = pos(f, e.first, k) - pos(f, e.second, k);
                    d += x * x;
                }
                cost = std::max(cost, d);
            }
            if (cost < best) {
                best = cost;
                ba = e.first;
                bb = e.second;
            }
        }
        if (ba < 0) break;
        repl[size_t(bb)] = ba;
        for (size_t t = 0; t < tris.size(); t++) {
            if (!tri_alive[t]) continue;
            int a = find(tris[t][0]), b = find(tris[t][1]), c = find(tris[t][2]);
            if (a == b || b == c || a == c) {
                tri_alive[t] = 0;
                alive--;
            }
        }
    }
    // renumber what is left
    FlatModel o = m;
    std::map<int, int> nidx;
    std::vector<int> order;
    o.tris.clear();
    std::vector<size_t> kept;
    for (size_t t = 0; t < tris.size(); t++) {
        if (!tri_alive[t]) continue;
        std::array<int, 3> nt;
        for (int k = 0; k < 3; k++) {
            int v = find(tris[t][size_t(k)]);
            auto it = nidx.find(v);
            if (it == nidx.end()) {
                it = nidx.emplace(v, int(order.size())).first;
                order.push_back(v);
            }
            nt[size_t(k)] = it->second;
        }
        o.tris.push_back(nt);
        kept.push_back(t);
    }
    o.nverts = int(order.size());
    o.frames.clear();
    for (auto &f : m.frames) {
        Bytes nf;
        for (int v : order) nf.insert(nf.end(), f.begin() + 4 * v, f.begin() + 4 * v + 4);
        o.frames.push_back(nf);
    }
    o.colours.clear();
    for (auto &c : m.colours) {
        Bytes nc;
        for (size_t t : kept) nc.push_back(c.at(t));
        o.colours.push_back(nc);
    }
    return o;
}

}  // namespace q32x
