#include "q32x/portals.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <numeric>
#include <set>

#include "q32x/geom.h"

namespace q32x {

bool leaf_open(const Bsp &bsp, int leaf)
{
    // (leaves past the world's belong to the brush models' own trees)
    if (leaf <= 0 || leaf >= int(bsp.leafs.size()) || leaf > bsp.models.at(0).visleafs) return false;
    int c = bsp.leafs[size_t(leaf)].contents;
    return c != CONTENTS_SOLID && c != CONTENTS_SKY;
}

static Vec3 plane_n(const BspPlane &p) { return {p.n[0], p.n[1], p.n[2]}; }

namespace {
struct Builder {
    const Bsp &bsp;
    std::vector<Portal> out;

    // push a polygon on a node's plane down a subtree: the leaves it touches
    // from the side `dir` points into
    void push(int node, const Winding &w, const Vec3 &dir, std::vector<std::pair<int, Winding>> &res)
    {
        if (w.empty()) return;
        if (node < 0) {
            res.push_back({-1 - node, w});
            return;
        }
        const BspNode &n = bsp.nodes.at(size_t(node));
        const BspPlane &pl = bsp.planes.at(size_t(n.plane));
        Vec3 pn = plane_n(pl);
        Winding f, b;
        split_winding(w, pn, pl.d, f, b, 0.01);
        bool coplanar = true;
        for (auto &p : w)
            if (std::fabs(dot(p, pn) - pl.d) > 0.01) coplanar = false;
        if (coplanar) {
            push(n.child[dot(pn, dir) > 0 ? 0 : 1], w, dir, res);
            return;
        }
        if (!f.empty()) push(n.child[0], f, dir, res);
        if (!b.empty()) push(n.child[1], b, dir, res);
    }

    void node(int num, std::vector<std::pair<Vec3, double>> &bounds)
    {
        if (num < 0) return;
        const BspNode &n = bsp.nodes.at(size_t(num));
        const BspPlane &pl = bsp.planes.at(size_t(n.plane));
        Vec3 pn = plane_n(pl);
        Winding w = base_winding(pn, pl.d);
        for (auto &b : bounds) w = clip_front(w, b.first, b.second, 0.001);
        if (!w.empty()) {
            std::vector<std::pair<int, Winding>> fronts;
            push(n.child[0], w, pn, fronts);
            Vec3 neg = scale3(pn, -1);
            for (auto &fr : fronts) {
                std::vector<std::pair<int, Winding>> backs;
                push(n.child[1], fr.second, neg, backs);
                for (auto &bk : backs)
                    if (fr.first != bk.first && winding_area(bk.second) > 0.01)
                        out.push_back({{fr.first, bk.first}, bk.second, pn, pl.d});
            }
        }
        bounds.push_back({pn, pl.d});
        node(n.child[0], bounds);
        bounds.back() = {scale3(pn, -1), -pl.d};
        node(n.child[1], bounds);
        bounds.pop_back();
    }
};
}  // namespace

std::vector<Portal> build_portals(const Bsp &bsp)
{
    Builder b{bsp, {}};
    const BspModel &m = bsp.models.at(0);
    std::vector<std::pair<Vec3, double>> bounds;
    for (int k = 0; k < 3; k++) {
        Vec3 n = {0, 0, 0};
        n[size_t(k)] = 1;
        bounds.push_back({n, m.mins[k] - 64});
        n[size_t(k)] = -1;
        bounds.push_back({n, -(m.maxs[k] + 64)});
    }
    b.node(m.headnode[0], bounds);
    return b.out;
}

bool model_solid_at(const Bsp &bsp, int model, const Vec3 &off, const Vec3 &p0)
{
    Vec3 p = sub(p0, off);
    int n = bsp.models.at(size_t(model)).headnode[0];
    while (n >= 0) {
        const BspNode &nd = bsp.nodes.at(size_t(n));
        const BspPlane &pl = bsp.planes.at(size_t(nd.plane));
        n = nd.child[dot(p, plane_n(pl)) - pl.d < 0 ? 1 : 0];
    }
    return bsp.leafs.at(size_t(-1 - n)).contents == CONTENTS_SOLID;
}

// the world's leaf at a point
static int world_leaf(const Bsp &bsp, const Vec3 &p)
{
    int n = bsp.models[0].headnode[0];
    while (n >= 0) {
        const BspNode &nd = bsp.nodes.at(size_t(n));
        const BspPlane &pl = bsp.planes.at(size_t(nd.plane));
        n = nd.child[dot(p, plane_n(pl)) - pl.d < 0 ? 1 : 0];
    }
    return -1 - n;
}

// the leaves a closed door fills: open leaves at least half inside the
// door's brushes (sampled on a grid); a leaf the door only touches is not one
static std::set<int> model_leaves(const Bsp &bsp, int mi)
{
    std::set<int> out;
    const BspModel &mo = bsp.models.at(size_t(mi));
    std::map<int, std::pair<int, int>> count;   // leaf -> (inside the door, all)
    double step = 4;
    for (int k = 0; k < 3; k++) step = std::max(step, (mo.maxs[k] - mo.mins[k] + 16) / 24);
    for (double x = mo.mins[0] - 8 + step / 2; x < mo.maxs[0] + 8; x += step)
        for (double y = mo.mins[1] - 8 + step / 2; y < mo.maxs[1] + 8; y += step)
            for (double z = mo.mins[2] - 8 + step / 2; z < mo.maxs[2] + 8; z += step) {
                Vec3 p = {x, y, z};
                int l = world_leaf(bsp, p);
                if (!leaf_open(bsp, l)) continue;
                auto &c = count[l];
                c.second++;
                if (model_solid_at(bsp, mi, {0, 0, 0}, p)) c.first++;
            }
    for (auto &kv : count) {
        // the whole leaf, not only the part near the door: its size from its box
        const BspLeaf &lf = bsp.leafs[size_t(kv.first)];
        double vol = 1;
        for (int k = 0; k < 3; k++) vol *= std::max(1, lf.maxs[k] - lf.mins[k]);
        double inside = kv.second.first * step * step * step;
        if (inside >= 0.5 * vol || (kv.second.second > 0 && kv.second.first == kv.second.second && inside >= 0.25 * vol))
            out.insert(kv.first);
    }
    return out;
}

AreaMap build_areas(const Bsp &bsp, const std::vector<Mover> &movers, const std::vector<Portal> &portals,
                    const std::vector<int> &exclude)
{
    AreaMap am;
    const int nleafs = int(bsp.leafs.size());
    std::vector<std::vector<int>> adj(static_cast<size_t>(nleafs));
    for (auto &p : portals)
        if (leaf_open(bsp, p.leaf[0]) && leaf_open(bsp, p.leaf[1])) {
            adj[size_t(p.leaf[0])].push_back(p.leaf[1]);
            adj[size_t(p.leaf[1])].push_back(p.leaf[0]);
        }
    // the candidate doors and the leaves each fills; doors that open
    // together (linked doors) count as one
    std::vector<int> doors;
    std::vector<std::set<int>> dleaves;
    std::map<int, size_t> by_group;
    for (size_t i = 0; i < movers.size(); i++) {
        const Mover &m = movers[i];
        if (m.cls != "func_door" && m.cls != "func_door_secret") continue;
        if (std::find(exclude.begin(), exclude.end(), int(i)) != exclude.end()) continue;
        auto ls = model_leaves(bsp, m.model);
        if (ls.empty()) continue;
        int g = m.group >= 0 ? m.group : int(i);
        auto it = by_group.find(g);
        if (it == by_group.end()) {
            by_group[g] = doors.size();
            doors.push_back(g);
            dleaves.push_back(ls);
        } else
            dleaves[it->second].insert(ls.begin(), ls.end());
    }
    std::vector<bool> active(doors.size(), true);
    std::vector<int> comp(static_cast<size_t>(nleafs), 0);
    int ncomp = 0;
    for (int iter = 0; iter < 64; iter++) {
        std::vector<int> owner(static_cast<size_t>(nleafs), -1);   // the door whose doorway a leaf is in
        for (size_t d = 0; d < doors.size(); d++)
            if (active[d])
                for (int l : dleaves[d])
                    if (owner[size_t(l)] < 0) owner[size_t(l)] = int(d);
        std::fill(comp.begin(), comp.end(), 0);
        ncomp = 0;
        for (int l = 1; l < nleafs; l++) {
            if (!leaf_open(bsp, l) || owner[size_t(l)] >= 0 || comp[size_t(l)]) continue;
            ncomp++;
            std::vector<int> st = {l};
            comp[size_t(l)] = ncomp;
            while (!st.empty()) {
                int a = st.back();
                st.pop_back();
                for (int b : adj[size_t(a)])
                    if (!comp[size_t(b)] && owner[size_t(b)] < 0) {
                        comp[size_t(b)] = ncomp;
                        st.push_back(b);
                    }
            }
        }
        // each door's neighbouring areas
        bool changed = false;
        am.doors.clear();
        for (size_t d = 0; d < doors.size(); d++) {
            if (!active[d]) continue;
            std::set<int> near;
            for (int l : dleaves[d])
                for (int b : adj[size_t(l)])
                    if (comp[size_t(b)]) near.insert(comp[size_t(b)]);
            if (std::getenv("Q32X_DEBUG_AREAS"))
                std::fprintf(stderr, "iter %d door %d (*%d) leaves %zu near %zu\n", iter, doors[d],
                             movers[size_t(doors[d])].model, dleaves[d].size(), near.size());
            if (near.size() < 2 || near.size() > 4) {
                active[d] = false;   // it does not divide two areas: an ordinary leaf set again
                changed = true;
                continue;
            }
            am.doors.push_back({doors[d], std::vector<int>(near.begin(), near.end())});
        }
        if (!changed) break;
    }
    am.leaky.clear();
    for (size_t d = 0; d < doors.size(); d++)
        if (!active[d]) am.leaky.push_back(doors[d]);
    // areas become 1..n; door leaves get 128 + the door's index in am.doors
    am.count = ncomp;
    am.leaf_area.assign(size_t(nleafs), 0);
    for (int l = 0; l < nleafs; l++) am.leaf_area[size_t(l)] = comp[size_t(l)];
    std::map<int, int> door_index;
    for (size_t k = 0; k < am.doors.size(); k++) door_index[am.doors[k].mover] = int(k);
    for (size_t d = 0; d < doors.size(); d++) {
        if (!active[d]) continue;
        int k = door_index[doors[d]];
        for (int l : dleaves[d])
            if (!am.leaf_area[size_t(l)]) am.leaf_area[size_t(l)] = 128 + k;
    }
    return am;
}

}  // namespace q32x
