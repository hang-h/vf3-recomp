// Optional VF1 Akira appearance, applied only to the rendered scene.
#pragma once
#include "r3d_scene.h"
#include "../host/vf1_models.h"
#include <array>
#include <string>

namespace video {
class Vf1Akira {
public:
    bool load(const std::string &zip, std::string &error);
    bool ready() const { return bool(parts[0]); }
    bool enabled = false;
    // Number of fully recognised Akira skeletons replaced. Unsupported layouts
    // are left intact; this never writes emulated memory or changes gameplay.
    // Bits select the original/alternate outfit; the host resolves player ownership.
    size_t apply(Scene &scene, unsigned outfit_mask = 3) const;
private:
    std::array<Meshes, size_t(vf1_models::Part::Count)> parts;
    std::array<std::array<Meshes,32>,2> hands;
    vf1_models::Strand ribbon;
};
}
