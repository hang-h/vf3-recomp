// Optional VF1 Akira appearance, applied only to the rendered scene.
#pragma once
#include "r3d_scene.h"
#include "../host/vf1_models.h"
#include <array>
#include <string>

namespace video {
// Shared retargeting for decoded Model 1/Model 2 parts onto the VF3 skeleton.
enum class AkiraSource { VF1, VF2 };
class Vf1Akira {
public:
    bool load(const std::string &zip, std::string &error);
    bool load_assets(const vf1_models::Assets &assets, AkiraSource source, std::string &error);
    bool ready() const { return bool(parts[0]); }
    bool enabled = false;
    // Number of fully recognised Akira skeletons replaced. Unsupported layouts
    // are left intact; this never writes emulated memory or changes gameplay.
    // Bits select the original/alternate outfit; the host resolves player ownership.
    size_t apply(Scene &scene, unsigned outfit_mask = 3) const;
private:
    std::array<Meshes, size_t(vf1_models::Part::Count)> parts;
    std::array<std::array<Meshes,32>,2> hands;
    std::vector<vf1_models::Strand> ribbons;
    float unit_scale=2000, head_z_scale=-1800, head_z_offset=-13;
    uint64_t mesh_key_base=0x5646310000000000ull;
    std::array<bool,2> replace_hands{};
    bool replace_ribbon=false, replace_belt=false;
    bool model2_source=false;
};
}
