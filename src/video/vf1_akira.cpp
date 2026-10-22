#include "vf1_akira.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace video {
namespace {
using Part = vf1_models::Part;
struct Binding { Part part; int slot; uint32_t node; float sx, sy, sz, z; uint32_t alternate; };
// VF3 revision D: offsets from Akira's head matrix, NOT polygon-RAM
// addresses (the game reallocates those every frame). The source-node check
// also guards the layout. Lengths measured between the game's joint origins:
// torso 757.76, arms 512/512, legs 798.72/901.12 Real3D units.
constexpr Binding bindings[] = {
    {Part::Torso,     -2, 0x42648, 1894.4f, 2000, -2000, 0, 0x43460},
    {Part::Pelvis,    74, 0x42618, 2100, 2000, -2000, 0, 0x433f0},
    {Part::UpperArmA, 27, 0x42608, 512/.26f, 2000, -2000, 0, 0x433e0},
    {Part::LowerArmA, 30, 0x42668, 512/.21f, 2000, -2000, 0, 0x434c0},
    {Part::UpperArmB, 51, 0x425f8, 512/.26f, 2000, -2000, 0, 0x433d0},
    {Part::LowerArmB, 54, 0x42658, 512/.21f, 2000, -2000, 0, 0x434b0},
    {Part::UpperLegA, 76, 0x42638, 798.72f/.45f, 2000, -2000, 0, 0x43450},
    {Part::LowerLegA, 79, 0x42368, 901.12f/.45f, 2000, -2000, 0, 0x43140},
    {Part::FootA,     82, 0x42340, 2000, 2000, -2000, 0, 0x430b8},
    {Part::UpperLegB, 84, 0x42628, 798.72f/.45f, 2000, -2000, 0, 0x43440},
    {Part::LowerLegB, 87, 0x42350, 901.12f/.45f, 2000, -2000, 0, 0x43128},
    {Part::FootB,     90, 0x42330, 2000, 2000, -2000, 0, 0x430a8},
    {Part::Head,       0, 0x509a0, 2000, 2000, -1800, -13, 0x51e58},
};
Meshes convert(const vf1_models::Mesh &source, const Binding &b)
{
    std::map<uint32_t, Mesh> groups;
    for (const auto &t : source) {
        Vertex v[3] = {};
        for (int i = 0; i < 3; ++i) {
            v[i].pos[0] = t.points[i][0] * b.sx;
            v[i].pos[1] = t.points[i][1] * b.sy;
            v[i].pos[2] = t.points[i][2] * b.sz + b.z;
            v[i].colour[0] = t.colour >> 16;
            v[i].colour[1] = t.colour >> 8;
            v[i].colour[2] = t.colour;
            v[i].colour[3] = 255;
        }
        float u[3], w[3];
        for (int k=0; k<3; ++k) { u[k]=v[1].pos[k]-v[0].pos[k]; w[k]=v[2].pos[k]-v[0].pos[k]; }
        float n[3] = {u[1]*w[2]-u[2]*w[1], u[2]*w[0]-u[0]*w[2], u[0]*w[1]-u[1]*w[0]};
        const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (!(length > 1e-8f)) continue;
        auto &mesh = groups[t.colour];
        mesh.lighting = true;
        mesh.key = 0x5646310000000000ull | (uint64_t(b.part) << 32) | t.colour;
        // Model 1's strips have mixed winding. Explicit opposite face normals
        // preserve both sides through the common CPU/GPU back-face test.
        for (int side : {1,-1}) for (auto x : v) {
            for (int k=0; k<3; ++k) x.normal[k]=x.face_normal[k]=side*n[k]/length;
            mesh.verts.push_back(x);
        }
    }
    auto result=std::make_shared<std::vector<Mesh>>();
    for (auto &[colour,mesh] : groups) result->push_back(std::move(mesh));
    return result;
}
// Skin/cloth bridges generated between VF3 limbs. VF1's parts already extend
// across their joint origins; keeping the VF3 bridges would cover the new gi.
constexpr uint32_t bridges[2][30] = {{
    0x42688,0x43058,0x43038,0x42f88,0x42f78,
    0x42678,0x43048,0x43028,0x43540,0x43530,
    0x43018,0x43010,0x42fc8,0x42fa8,0x43008,0x42fe8,
    0x42fb8,0x42f98,0x42ff8,0x42fd8,
    0x42370,0x42358,
}, {
    0x43500,0x43520,0x434e0,0x43118,0x430f8,
    0x434f0,0x43510,0x434d0,0x43108,0x430e8,
    0x51788,0x51780,0x43430,0x43410,0x434a0,0x43480,
    0x43420,0x43400,0x43490,0x43470,0x43148,0x43130,
    // The alternate outfit has separate sleeve/ankle cloth.
    0x5a868,0x5a860,0x5a878,0x5a870,0x5a8b8,0x5a898,0x5a8a8,0x5a888,
}};
}

bool Vf1Akira::load(const std::string &zip, std::string &error)
{
    vf1_models::Assets assets;
    if (!vf1_models::load(zip, assets, error)) return false;
    decltype(parts) next;
    for (const auto &b : bindings) next[size_t(b.part)] = convert(assets.parts[size_t(b.part)], b);
    parts = std::move(next);
    return true;
}

size_t Vf1Akira::apply(Scene &scene) const
{
    if (!enabled || !ready()) return 0;
    static const Meshes empty = std::make_shared<std::vector<Mesh>>();
    size_t replaced=0;
    // Some stages split each fighter across a dozen lighting viewports. Keep
    // their matrix-table/camera identity rather than assuming one viewport.
    for (auto &head_vp : scene.viewports) for (auto &head : head_vp.models) {
        const int outfit = head.addr==0x9aaccf && head.source_node==0x509a0 ? 0 :
            head.addr==0xa47386 && head.source_node==0x51e58 ? 1 : -1;
        if (outfit < 0) continue;
        const int base=head.matrix_index;
        auto same_camera = [&](const Viewport &vp) {
            return vp.matrix_table==head_vp.matrix_table && vp.root_matrix==head_vp.root_matrix &&
                !std::memcmp(vp.root.m,head_vp.root.m,sizeof vp.root.m);
        };
        std::array<Model *,size_t(Part::Count)> found{};
        bool ambiguous=false;
        for (auto &vp : scene.viewports) if (same_camera(vp))
            for (auto &m : vp.models) for (const auto &b : bindings)
                if (m.source_node==(outfit ? b.alternate : b.node) &&
                    int(m.matrix_index)==base+(outfit && b.part==Part::Torso ? -4 : b.slot)) {
                    auto &p=found[size_t(b.part)]; if (p) ambiguous=true; p=&m;
                }
        if (ambiguous || std::any_of(found.begin(),found.end(),[](const auto *p){return !p;})) continue;
        for (auto &vp : scene.viewports) if (same_camera(vp)) for (auto &m : vp.models) {
            const int slot=int(m.matrix_index)-base;
            // Original face, eye spheres and headband tails. Shadow meshes
            // use separate slots (+1 etc.) and remain the game's shadows.
            if (slot==0 || (slot>=2 && slot<=25) ||
                (m.source_node && std::find(std::begin(bridges[outfit]),std::end(bridges[outfit]),m.source_node)!=std::end(bridges[outfit])))
                m.meshes=empty;
        }
        for (size_t i=0;i<found.size();++i) found[i]->meshes=parts[i];
        ++replaced;
    }
    return replaced;
}
}
