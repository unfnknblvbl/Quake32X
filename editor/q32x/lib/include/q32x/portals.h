// Portals between the leaves of a BSP's world tree (the openings one leaf
// sees another through), and the areas closed doors divide a level into.
#pragma once

#include <vector>

#include "q32x/movers.h"
#include "q32x/quake.h"

namespace q32x {

struct Portal {
    int leaf[2];              // the leaves on its two sides (front of the plane first)
    std::vector<Vec3> w;      // its polygon
    Vec3 normal;              // pointing into leaf[0]
    double dist;
};

// every portal between two leaves of the world model (outside leaves left out)
std::vector<Portal> build_portals(const Bsp &bsp);

// leaves the player can stand in or see through (not solid, not sky)
bool leaf_open(const Bsp &bsp, int leaf);

// is p inside the solid part of brush model `model` placed at offset `off`?
bool model_solid_at(const Bsp &bsp, int model, const Vec3 &off, const Vec3 &p);

struct DoorArea {
    int mover;                // index into the movers (the leader of linked doors)
    std::vector<int> areas;   // the areas it separates (2 to 4)
};

struct AreaMap {
    std::vector<int> leaf_area;   // per leaf: its area (0: solid or unreached)
    int count = 0;                // areas 1..count
    std::vector<DoorArea> doors;  // the doors that divide them
    std::vector<int> leaky;       // doors that do not close off their opening (movers)
};

// the areas a level's doors divide it into when they are all closed; doors
// listed in `exclude` (mover indices) never divide
AreaMap build_areas(const Bsp &bsp, const std::vector<Mover> &movers, const std::vector<Portal> &portals,
                    const std::vector<int> &exclude);

}  // namespace q32x
