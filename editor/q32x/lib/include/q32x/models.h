// Model tools: fewer animation frames, simpler meshes, distance versions.
#pragma once

#include <vector>

#include "q32x/gamedata.h"

namespace q32x {

// keep only the listed frames; the others show the nearest kept frame
// before them (needs game 0.7.0)
FlatModel trim_frames(const FlatModel &m, const std::vector<int> &keep);
// keep every `step`th frame of each animation (a model's animations are
// runs of frames with the same name stem)
std::vector<int> every_nth_frame(const std::vector<std::string> &frame_names, int step);
// a simpler mesh: vertices merged until about `ratio` of the triangles are left
FlatModel simplify_model(const FlatModel &m, double ratio);
std::vector<std::string> model_frame_names(const Bytes &mdl);

}  // namespace q32x
