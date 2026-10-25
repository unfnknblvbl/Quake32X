// Light maps as hard light and shadow regions, and those regions as
// polygons on their faces (for flat shading without light maps).
#pragma once

#include <vector>

#include "q32x/quake.h"

namespace q32x {

// one face's light map and where it lies
struct LightFace {
    std::vector<uint8_t> L;      // smax x tmax samples: the level's light values (0 bright .. 255 dark)
    int smax = 0, tmax = 0;
    int64_t tmins[2] = {0, 0};
    double vecs[8] = {};         // its texture mapping (s and t vectors with offsets)
    Vec3 n{};                    // its plane
    double d = 0;
    std::vector<Vec3> poly;      // the face
    int forced_band = -1;
};

struct BandSet {
    std::vector<double> thresholds;   // in colormap rows (0..63), between bands
    std::vector<int> rows;            // each band's colormap row
    int band_of(double row) const
    {
        int b = 0;
        while (b < int(thresholds.size()) && row >= thresholds[size_t(b)]) b++;
        return b;
    }
};

// bands for a set of faces: their light levels split into `bands` groups
BandSet choose_bands(const std::vector<const LightFace *> &faces, int bands, double contrast);

struct LightPieces {
    std::vector<std::pair<std::vector<Vec3>, int>> pieces;   // convex polygons and their bands
    std::vector<uint8_t> bandmap;                            // per sample, after clean-up
    int uniform = -1;                                        // the face's one band, if it has one
};

LightPieces light_pieces(const LightFace &f, const BandSet &bands, double min_area);

// a point on the face's plane from its texture coordinates
Vec3 face_point(const LightFace &f, double s, double t);

}  // namespace q32x
