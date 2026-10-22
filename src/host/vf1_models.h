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
struct Strand {
    uint8_t bone = 0;
    std::array<float, 3> anchor{};
    std::vector<Mesh> segments = std::vector<Mesh>(3);
    std::vector<float> lengths = std::vector<float>(3);
};
struct Assets {
    std::array<Mesh, size_t(Part::Count)> parts;
    std::array<std::array<Mesh, 32>, 2> hands; // A (+0x22), B (+0x2e), closed to open
    Mesh belt_knot;
    std::array<Strand, 3> strands; // headband ribbon, two belt tails
    std::vector<Strand> extra_head_ribbons; // VF2 has a second, five-segment ribbon
};
// Size/CRC-validated, no extracted files or cache; failure leaves out unchanged.
// Only the original P1 Akira neutral face is decoded here, with no VF2 fitting.
bool load(const std::string &zip, Assets &out, std::string &error);
}
