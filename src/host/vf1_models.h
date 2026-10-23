// VF1 roster source meshes, decoded from the user's own vf.zip.
// Coordinates remain in VF1 part-local space; destination fitting belongs to the renderer.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include "../core/model2_texture.h"

namespace vf1_models {
enum class Part {
    Torso, Pelvis, UpperArmA, LowerArmA, UpperArmB, LowerArmB,
    UpperLegA, LowerLegA, FootA, UpperLegB, LowerLegB, FootB, Head, Count
};
struct Triangle {
    uint32_t colour;
    std::array<std::array<float, 3>, 3> points;
    unsigned material=0; // one-based index into Assets::textures, zero is flat colour
    std::array<std::array<float,2>,3> uv{}; // source texel coordinates
};
using Mesh = std::vector<Triangle>;
struct Strand {
    uint8_t bone = 0;
    std::array<float, 3> anchor{};
    // Rigid rest direction in the destination's head-local coordinates.
    // Most hair hangs down; source-driven upright tufts override this basis.
    std::array<float, 3> rest_direction{0,0,-1};
    std::vector<Mesh> segments = std::vector<Mesh>(3);
    std::vector<float> lengths = std::vector<float>(3);
};
struct Portrait {
    unsigned width = 0, height = 0;
    std::vector<uint32_t> pixels; // opaque 0xAARRGGBB, native artwork dimensions
    bool available() const { return width && height && pixels.size() == size_t(width) * height; }
};
struct FaceAnimation {
    // Native source timeline; entries address the eager/lazy facial frame bank.
    std::vector<uint16_t> frames;
    int loop_start=-1; // native next-node cycle; otherwise hold the final frame
};
struct Assets {
    std::vector<std::shared_ptr<const Model2Texture>> textures;
    // Original skeleton offsets (slots 0..15), used to fit each fighter's limbs.
    std::array<std::array<float,3>,16> source_joints{};
    std::array<Mesh, size_t(Part::Count)> parts;
    std::array<std::array<Mesh, 32>, 2> hands; // A (+0x22), B (+0x2e), closed to open
    Mesh belt_knot;
    Mesh head_accessory; // authored rigid geometry in the source head's local space
    std::array<Strand, 3> strands; // headband ribbon, two belt tails
    std::vector<Strand> extra_head_ribbons; // VF2 has a second, five-segment ribbon
    std::vector<Strand> extra_strands; // Other torso/pelvis accessories in the roster
    Portrait portrait;
    // Immutable source head variants. Frame zero preserves the neutral head.
    // Renderers convert only the currently displayed variants on demand.
    std::vector<Mesh> face_frames;
    // Texture-animated sources keep compact immutable recipes instead of
    // baking every combination. The callback owns no emulator or file handle.
    unsigned face_frame_count=0;
    std::function<Mesh(unsigned)> decode_face;
    std::array<FaceAnimation,10> face_animations; // original source script IDs (VF1: 1..7, VF2: 0..9)
    std::array<uint16_t,3> face_states{}; // native neutral, expression, closed-eye heads
    std::vector<uint16_t> face_speech; // original mouth sequence, closed to open
};
// Size/CRC-validated, no extracted files or cache; failure leaves out unchanged.
// Original P1 source geometry and facial frames, with no destination fitting.
bool load(const std::string &zip, Assets &out, std::string &error, unsigned fighter=0);
}
