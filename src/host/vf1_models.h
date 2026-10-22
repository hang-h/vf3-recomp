// VF1 Akira source meshes, decoded from the user's own vf.zip.
// Coordinates remain in VF1 part-local space; destination fitting belongs to the renderer.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vf1_models {
enum class Part {
    Torso, Pelvis, UpperArmA, LowerArmA, UpperArmB, LowerArmB,
    UpperLegA, LowerLegA, FootA, UpperLegB, LowerLegB, FootB, Head, Count
};
struct Triangle {
    uint32_t colour;
    std::array<std::array<float, 3>, 3> points;
};
using Mesh = std::vector<Triangle>;
struct Assets {
    std::array<Mesh, size_t(Part::Count)> parts;
};
// Size/CRC-validated, no extracted files or cache; failure leaves out unchanged.
// Only the original P1 Akira neutral face is decoded here, with no VF2 fitting.
bool load(const std::string &zip, Assets &out, std::string &error);
}
