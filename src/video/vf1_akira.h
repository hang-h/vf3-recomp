// Optional VF1/VF2 roster appearance, applied only to the rendered scene.
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
    bool load_assets(const vf1_models::Assets &assets, AkiraSource source, std::string &error, unsigned fighter=0);
    bool ready() const { return bool(parts[0]); }
    const vf1_models::Portrait &portrait() const { return source_portrait; }
    bool enabled = false;
    // Number of recognised visible fighters replaced. Unsupported layouts
    // are left intact; this never writes emulated memory or changes gameplay.
    // Bits select the original/alternate outfit; the host resolves player ownership.
    // Head-only selection scenes require preview_mask from the selection state.
    size_t apply(Scene &scene, unsigned outfit_mask = 3, unsigned preview_mask = 0) const;
private:
    bool load_fighter_assets(const vf1_models::Assets &, AkiraSource, std::string &, unsigned);
    size_t apply_fighter(Scene &, unsigned outfit_mask, unsigned preview_mask) const;
    unsigned roster_fighter=0;
    std::array<Meshes, size_t(vf1_models::Part::Count)> parts;
    std::array<std::array<Meshes,32>,2> hands;
    std::vector<vf1_models::Strand> ribbons;
    float unit_scale=2000, head_z_scale=-1800, head_z_offset=-13;
    uint64_t mesh_key_base=0x5646310000000000ull;
    std::array<bool,2> replace_hands{};
    bool replace_ribbon=false, replace_belt=false;
    bool model2_source=false;
    vf1_models::Portrait source_portrait;
};
}
