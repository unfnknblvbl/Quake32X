// Simpler collision: the world's clip hulls (1: player-sized, 2: large
// monsters) rebuilt as balanced trees over the same solid space.
//
// Quake's clip hulls follow the order qbsp met the brushes in, so a point's
// contents take 15-30 plane tests (65 at worst in E1M2). The rebuild cuts
// the hull's space into its convex cells, merges neighbours of the same kind
// and builds a new tree choosing planes that balance it and split few cells.
// The solid space is exactly the same (the same planes bound it); only the
// order of the tests changes.
#pragma once

#include <vector>

#include "q32x/quake.h"

namespace q32x {

struct CollisionStats {
    int hull = 0;
    int nodes_before = 0, nodes_after = 0;
    int leaves_before = 0, leaves_after = 0;
    double depth_before = 0, depth_after = 0;   // average plane tests for a point, weighted by cell volume
    int max_before = 0, max_after = 0;
    long mismatches = 0;                        // sample points classified differently (should be 0)
};

// world hull `hull` (1 or 2) rebuilt: its nodes (indices from 0) and head
std::vector<BspClipnode> rebuild_hull(const Bsp &bsp, int hull, int &head, CollisionStats *stats = nullptr);

// both clip hulls of the world rebuilt in place (submodels keep theirs); the
// clip node list is compacted and every model's head renumbered
std::vector<CollisionStats> simplify_collision(Bsp &bsp);

// the contents of point p in clip hull with head `head` (for checks)
int hull_contents(const Bsp &bsp, const std::vector<BspClipnode> &nodes, int head, const Vec3 &p);

}  // namespace q32x
