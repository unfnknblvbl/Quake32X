#include "q32x/collision.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace q32x {

namespace {

constexpr double EPS = 0.01;

// a convex cell: its faces with outward normals (inside: n.p - d <= 0)
struct Face {
    Vec3 n;
    double d;
    int plane;                 // the BSP plane it lies on (-1: the bounding box)
    std::vector<Vec3> w;
};
struct Cell {
    std::vector<Face> faces;
    int contents = 0;
    int depth = 0;
    double volume = 0;
    double lo = 0, hi = 0;     // scratch: extent along a candidate plane
    // what a point here is worth: the game asks about open space far more
    // often than about the inside of walls
    double weight() const { return contents == CONTENTS_SOLID ? volume * 0.02 : volume; }
};

Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// a large square on the plane n.p = d
std::vector<Vec3> base_winding(const Vec3 &n, double d)
{
    int ax = 0;
    for (int k = 1; k < 3; k++)
        if (std::fabs(n[size_t(k)]) > std::fabs(n[size_t(ax)])) ax = k;
    Vec3 up = ax == 2 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
    double t = dot(up, n);
    up = {up[0] - n[0] * t, up[1] - n[1] * t, up[2] - n[2] * t};
    double l = std::sqrt(dot(up, up));
    up = {up[0] / l, up[1] / l, up[2] / l};
    Vec3 rt = cross(up, n);
    Vec3 o = {n[0] * d, n[1] * d, n[2] * d};
    const double S = 262144;
    std::vector<Vec3> w(4);
    for (int i = 0; i < 4; i++) {
        double su = (i == 0 || i == 3) ? S : -S, sr = (i < 2) ? -S : S;
        for (int k = 0; k < 3; k++) w[size_t(i)][size_t(k)] = o[size_t(k)] + up[size_t(k)] * su + rt[size_t(k)] * sr;
    }
    return w;
}

// the part of w where n.p - d <= 0
std::vector<Vec3> clip_winding(const std::vector<Vec3> &w, const Vec3 &n, double d)
{
    std::vector<Vec3> out;
    size_t c = w.size();
    for (size_t i = 0; i < c; i++) {
        const Vec3 &a = w[i], &b = w[(i + 1) % c];
        double da = dot(n, a) - d, db = dot(n, b) - d;
        if (da <= EPS) out.push_back(a);
        if ((da > EPS && db < -EPS) || (da < -EPS && db > EPS)) {
            double t = da / (da - db);
            out.push_back({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t});
        }
    }
    return out.size() >= 3 ? out : std::vector<Vec3>{};
}

double cell_volume(const Cell &c)
{
    double v = 0;
    for (auto &f : c.faces) {
        Vec3 area{0, 0, 0};
        for (size_t i = 1; i + 1 < f.w.size(); i++) {
            Vec3 x = cross(sub(f.w[i], f.w[0]), sub(f.w[i + 1], f.w[0]));
            area = {area[0] + x[0], area[1] + x[1], area[2] + x[2]};
        }
        v += dot(f.w[0], area);
    }
    return std::fabs(v) / 6.0;
}

// the part of c on one side of plane (n, d): keep_front keeps n.p - d >= 0
bool clip_cell(const Cell &c, const Vec3 &n, double d, int plane, bool keep_front, Cell &out)
{
    Vec3 on = keep_front ? Vec3{-n[0], -n[1], -n[2]} : n;   // the new face's outward normal
    double od = keep_front ? -d : d;
    out.faces.clear();
    out.contents = c.contents;
    out.depth = c.depth;
    for (auto &f : c.faces) {
        auto w = clip_winding(f.w, on, od);
        if (!w.empty()) out.faces.push_back({f.n, f.d, f.plane, std::move(w)});
    }
    if (out.faces.size() < 3) return false;
    auto cap = base_winding(on, od);
    for (auto &f : c.faces) {
        cap = clip_winding(cap, f.n, f.d);
        if (cap.empty()) break;
    }
    if (!cap.empty()) out.faces.push_back({on, od, plane, std::move(cap)});
    if (out.faces.size() < 4) return false;
    out.volume = cell_volume(out);
    return out.volume > 0.001;
}

Cell box_cell(const Vec3 &mn, const Vec3 &mx)
{
    Cell c;
    for (int k = 0; k < 3; k++)
        for (int s = 0; s < 2; s++) {
            Vec3 n{0, 0, 0};
            n[size_t(k)] = s ? 1 : -1;
            double d = s ? mx[size_t(k)] : -mn[size_t(k)];
            Face f{n, d, -1, base_winding(n, d)};
            c.faces.push_back(f);
        }
    for (auto &f : c.faces)
        for (auto &g : c.faces)
            if (&f != &g) f.w = clip_winding(f.w, g.n, g.d);
    c.volume = cell_volume(c);
    return c;
}

Vec3 plane_n(const Bsp &b, int p) { return {b.planes[size_t(p)].n[0], b.planes[size_t(p)].n[1], b.planes[size_t(p)].n[2]}; }

void extract(const Bsp &b, int node, const Cell &c, std::vector<Cell> &out, int &maxd)
{
    if (node < 0) {
        Cell x = c;
        x.contents = node;
        maxd = std::max(maxd, c.depth);
        out.push_back(std::move(x));
        return;
    }
    const BspClipnode &cn = b.clipnodes[size_t(node)];
    Vec3 n = plane_n(b, cn.plane);
    double d = b.planes[size_t(cn.plane)].d;
    Cell f, k;
    if (clip_cell(c, n, d, cn.plane, true, f)) {
        f.depth = c.depth + 1;
        extract(b, cn.child[0], f, out, maxd);
    }
    if (clip_cell(c, n, d, cn.plane, false, k)) {
        k.depth = c.depth + 1;
        extract(b, cn.child[1], k, out, maxd);
    }
}

struct Builder {
    const Bsp &b;
    std::vector<BspClipnode> nodes;
    double depth_sum = 0;
    int maxd = 0;
    int leaves = 0;

    int build(std::vector<Cell> cells, int depth)
    {
        int c0 = cells.empty() ? CONTENTS_SOLID : cells[0].contents;
        bool uniform = true;
        for (auto &c : cells)
            if (c.contents != c0) uniform = false;
        if (uniform) {
            for (auto &c : cells) depth_sum += c.weight() * depth;
            maxd = std::max(maxd, depth);
            leaves++;
            return c0;
        }
        // candidate planes: those bounding the cells (sampled evenly when many)
        std::vector<int> cand;
        {
            std::set<int> s;
            for (auto &c : cells)
                for (auto &f : c.faces)
                    if (f.plane >= 0) s.insert(f.plane);
            cand.assign(s.begin(), s.end());
            const size_t MAXC = 96;
            if (cand.size() > MAXC) {
                std::vector<int> pick;
                for (size_t i = 0; i < MAXC; i++) pick.push_back(cand[i * cand.size() / MAXC]);
                cand = pick;
            }
        }
        double best = 1e300;
        int bp = -1;
        double total = 0;
        for (auto &c : cells) total += c.weight();
        for (int p : cand) {
            Vec3 n = plane_n(b, p);
            double d = b.planes[size_t(p)].d;
            int fr = 0, bk = 0, sp = 0;
            double fv = 0;
            std::map<int, int> fcont, bcont;
            for (auto &c : cells) {
                double lo = 1e300, hi = -1e300;
                for (auto &f : c.faces)
                    for (auto &v : f.w) {
                        double x = dot(n, v) - d;
                        lo = std::min(lo, x);
                        hi = std::max(hi, x);
                    }
                if (lo >= -EPS) {
                    fr++;
                    fv += c.weight();
                    fcont[c.contents]++;
                } else if (hi <= EPS) {
                    bk++;
                    bcont[c.contents]++;
                } else {
                    sp++;
                    fv += c.weight() / 2;
                    fcont[c.contents]++;
                    bcont[c.contents]++;
                }
            }
            if (fr + sp == 0 || bk + sp == 0) continue;
            // few cells split, the volume halved (a point's tests are what the
            // game pays), and a side of one kind finished at once
            double bal = std::fabs(fv / total - 0.5);
            double score = sp * 2.0 + bal * double(cells.size()) - (fcont.size() == 1 ? 0.5 * fr : 0) -
                           (bcont.size() == 1 ? 0.5 * bk : 0);
            if (score < best) {
                best = score;
                bp = p;
            }
        }
        if (bp < 0) {
            // no plane separates them: the cells are slivers; the larger kind wins
            std::map<int, double> vol;
            for (auto &c : cells) vol[c.contents] += c.volume;
            int k = c0;
            double bv = -1;
            for (auto &kv : vol)
                if (kv.second > bv) {
                    bv = kv.second;
                    k = kv.first;
                }
            for (auto &c : cells) depth_sum += c.weight() * depth;
            leaves++;
            return k;
        }
        Vec3 n = plane_n(b, bp);
        double d = b.planes[size_t(bp)].d;
        std::vector<Cell> front, back;
        for (auto &c : cells) {
            double lo = 1e300, hi = -1e300;
            for (auto &f : c.faces)
                for (auto &v : f.w) {
                    double x = dot(n, v) - d;
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            if (lo >= -EPS)
                front.push_back(std::move(c));
            else if (hi <= EPS)
                back.push_back(std::move(c));
            else {
                Cell f, k;
                if (clip_cell(c, n, d, bp, true, f)) front.push_back(std::move(f));
                if (clip_cell(c, n, d, bp, false, k)) back.push_back(std::move(k));
            }
        }
        cells.clear();
        cells.shrink_to_fit();
        int idx = int(nodes.size());
        nodes.push_back({bp, {0, 0}});
        int f = build(std::move(front), depth + 1);
        int k = build(std::move(back), depth + 1);
        nodes[size_t(idx)].child[0] = f;
        nodes[size_t(idx)].child[1] = k;
        return idx;
    }
};

}  // namespace

int hull_contents(const Bsp &b, const std::vector<BspClipnode> &nodes, int head, const Vec3 &p)
{
    int n = head;
    while (n >= 0) {
        const BspClipnode &c = nodes[size_t(n)];
        const BspPlane &pl = b.planes[size_t(c.plane)];
        double d = pl.n[0] * p[0] + pl.n[1] * p[1] + pl.n[2] * p[2] - pl.d;
        n = d >= 0 ? c.child[0] : c.child[1];
    }
    return n;
}

std::vector<BspClipnode> rebuild_hull(const Bsp &b, int hull, int &head, CollisionStats *stats)
{
    const BspModel &m = b.models.at(0);
    Vec3 mn, mx;
    for (int k = 0; k < 3; k++) {
        mn[size_t(k)] = m.mins[k] - 128;
        mx[size_t(k)] = m.maxs[k] + 128;
    }
    std::vector<Cell> cells;
    int maxd = 0;
    extract(b, m.headnode[hull], box_cell(mn, mx), cells, maxd);
    Builder bl{b, {}, 0, 0, 0};
    double total = 0, before = 0;
    for (auto &c : cells) {
        total += c.weight();
        before += c.weight() * c.depth;
    }
    size_t ncells = cells.size();
    head = bl.build(std::move(cells), 0);
    if (stats) {
        stats->hull = hull;
        // the original's reachable nodes
        std::set<int> seen;
        std::function<void(int)> walk = [&](int n) {
            if (n < 0 || !seen.insert(n).second) return;
            walk(b.clipnodes[size_t(n)].child[0]);
            walk(b.clipnodes[size_t(n)].child[1]);
        };
        walk(m.headnode[hull]);
        stats->nodes_before = int(seen.size());
        stats->leaves_before = int(ncells);
        stats->nodes_after = int(bl.nodes.size());
        stats->leaves_after = bl.leaves;
        stats->depth_before = before / total;
        stats->depth_after = bl.depth_sum / total;
        stats->max_before = maxd;
        stats->max_after = bl.maxd;
        // the same contents everywhere: a lattice of points through the world
        long bad = 0;
        const int N = 48;
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++)
                for (int k = 0; k < N; k++) {
                    Vec3 p{m.mins[0] + (m.maxs[0] - m.mins[0]) * (i + 0.37) / N,
                           m.mins[1] + (m.maxs[1] - m.mins[1]) * (j + 0.61) / N,
                           m.mins[2] + (m.maxs[2] - m.mins[2]) * (k + 0.29) / N};
                    if (hull_contents(b, b.clipnodes, m.headnode[hull], p) != hull_contents(b, bl.nodes, head, p)) bad++;
                }
        stats->mismatches = bad;
    }
    return bl.nodes;
}

std::vector<CollisionStats> simplify_collision(Bsp &b)
{
    std::vector<CollisionStats> st(2);
    int head[3] = {0, 0, 0};
    std::vector<BspClipnode> rebuilt[3];
    for (int h = 1; h <= 2; h++) rebuilt[h] = rebuild_hull(b, h, head[h], &st[size_t(h - 1)]);
    // the submodels' hulls, compacted, then the world's new ones
    std::vector<BspClipnode> out;
    std::map<int, int> remap;
    std::function<int(int)> copy = [&](int n) -> int {
        if (n < 0) return n;
        auto it = remap.find(n);
        if (it != remap.end()) return it->second;
        int idx = int(out.size());
        remap[n] = idx;
        out.push_back(b.clipnodes[size_t(n)]);
        int c0 = copy(b.clipnodes[size_t(n)].child[0]);
        int c1 = copy(b.clipnodes[size_t(n)].child[1]);
        out[size_t(idx)].child[0] = c0;
        out[size_t(idx)].child[1] = c1;
        return idx;
    };
    for (size_t m = 1; m < b.models.size(); m++)
        for (int h = 1; h <= 2; h++) b.models[m].headnode[h] = copy(b.models[m].headnode[h]);
    for (int h = 1; h <= 2; h++) {
        int base = int(out.size());
        for (auto c : rebuilt[h]) {
            for (int k = 0; k < 2; k++)
                if (c.child[k] >= 0) c.child[k] += base;
            out.push_back(c);
        }
        b.models[0].headnode[h] = head[h] >= 0 ? head[h] + base : head[h];
    }
    for (auto &m : b.models) m.headnode[3] = 0;
    check(out.size() < 32768, "too many clip nodes after the rebuild");
    b.clipnodes = std::move(out);
    return st;
}

}  // namespace q32x
