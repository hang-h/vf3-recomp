#include "vf1_akira.h"
#include "fighter_layouts.h"
#include "vf2_body_fit.h"
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
Meshes convert(const vf1_models::Mesh &source, const Binding &b, uint64_t key_base=0x5646310000000000ull)
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
        mesh.two_sided = true;
        mesh.key = key_base | (uint64_t(b.part) << 32) | t.colour;
        // Source strips have mixed winding. The common CPU/GPU preparation
        // supplies the reverse normal without storing and transforming every
        // triangle twice.
        for (auto x : v) {
            for (int k=0; k<3; ++k) x.normal[k]=x.face_normal[k]=n[k]/length;
            mesh.verts.push_back(x);
        }
    }
    auto result=std::make_shared<std::vector<Mesh>>();
    for (auto &[colour,mesh] : groups) result->push_back(std::move(mesh));
    return result;
}
using Point = std::array<float,3>;
Point transform(const Mat4 &m, const Point &p)
{
    Point result;
    for (int r=0;r<3;++r) result[r]=m.m[r]*p[0]+m.m[4+r]*p[1]+m.m[8+r]*p[2]+m.m[12+r];
    return result;
}
bool inverse(const Mat4 &m, Mat4 &out)
{
    double rows[3][6]{};
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) { rows[r][c]=m.m[4*c+r]; rows[r][3+c]=r==c; }
    for(int c=0;c<3;++c) {
        int pivot=c;for(int r=c+1;r<3;++r)if(std::abs(rows[r][c])>std::abs(rows[pivot][c]))pivot=r;
        if(std::abs(rows[pivot][c])<1e-10)return false;
        for(int j=0;j<6;++j)std::swap(rows[c][j],rows[pivot][j]);
        const double scale=rows[c][c];for(double &x:rows[c])x/=scale;
        for(int r=0;r<3;++r)if(r!=c) { const double f=rows[r][c];for(int j=0;j<6;++j)rows[r][j]-=f*rows[c][j]; }
    }
    out=Mat4::identity();
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)out.m[c*4+r]=float(rows[r][3+c]);
    const auto t=transform(out,{m.m[12],m.m[13],m.m[14]});
    for(int r=0;r<3;++r)out.m[12+r]=-t[r];
    return true;
}
Point axis(const Mat4 &m,int col)
{
    Point p={m.m[col*4],m.m[col*4+1],m.m[col*4+2]};
    const float n=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if(n>1e-8f)for(float &x:p)x/=n;
    return p;
}
void append(Meshes &destination, const vf1_models::Mesh &mesh, unsigned identity, uint64_t key_base=0x5646310000000000ull)
{
    const Binding b={Part(identity),0,0,1,1,1,0,0};
    const auto converted=convert(mesh,b,key_base);
    destination->insert(destination->end(),converted->begin(),converted->end());
}
// Skin/cloth bridges generated between VF3 limbs. VF1's parts already extend
// across their joint origins; keeping the VF3 bridges would cover the new gi.
constexpr uint32_t bridges[2][32] = {{
    0x42688,0x43058,0x43038,0x42f88,0x42f78,
    0x42678,0x43048,0x43028,0x43540,0x43530,
    0x43018,0x43010,0x42fc8,0x42fa8,0x43008,0x42fe8,
    0x42fb8,0x42f98,0x42ff8,0x42fd8,
    0x42370,0x42358,0x42f58,0x42f48, // original waist hem and red belt
}, {
    0x43500,0x43520,0x434e0,0x43118,0x430f8,
    0x434f0,0x43510,0x434d0,0x43108,0x430e8,
    0x51788,0x51780,0x43430,0x43410,0x434a0,0x43480,
    0x43420,0x43400,0x43490,0x43470,0x43148,0x43130,
    // The alternate outfit has separate sleeve/ankle cloth.
    0x5a868,0x5a860,0x5a878,0x5a870,0x5a8b8,0x5a898,0x5a8a8,0x5a888,0x430d8,0x430c8,
}};
}

bool Vf1Akira::load(const std::string &zip, std::string &error)
{
    vf1_models::Assets assets;
    if (!vf1_models::load(zip, assets, error)) return false;
    return load_assets(assets,AkiraSource::VF1,error);
}

bool Vf1Akira::load_assets(const vf1_models::Assets &assets, AkiraSource source, std::string &error, unsigned fighter)
{
    if(fighter) return load_fighter_assets(assets,source,error,fighter);
    if(std::any_of(assets.parts.begin(),assets.parts.end(),[](const auto &p){return p.empty();})) {
        error="Akira source is missing a body part";return false;
    }
    const bool model2=source==AkiraSource::VF2;
    // VF2's .25/.25 arms and .39/.44 legs match VF3's joint distances at
    // exactly 2048 units per source unit. VF1 needs its per-part length fitting.
    const float units=model2 ? 2048.0f : 2000.0f;
    const float pelvis_x=model2 ? 2048.0f : 2100.0f;
    const uint64_t key_base=model2 ? 0x5646320000000000ull : 0x5646310000000000ull;
    decltype(parts) next;
    for (auto b : bindings) {
        if(model2) { b.sx=b.sy=units;b.sz=-units;b.z=0; }
        if(model2 && b.part==Part::Torso) {
            auto torso=assets.parts[size_t(b.part)];
            fit_vf2_waist(torso,assets.parts[size_t(Part::Pelvis)],assets.source_joints[2][0]);
            next[size_t(b.part)]=convert(torso,b,key_base);
        } else next[size_t(b.part)] = convert(assets.parts[size_t(b.part)], b,key_base);
    }
    decltype(hands) next_hands;
    std::array<bool,2> has_hands{};
    for(size_t side=0;side<2;++side) {
        has_hands[side]=std::all_of(assets.hands[side].begin(),assets.hands[side].end(),[](const auto &m){return !m.empty();});
        if(has_hands[side])for(size_t pose=0;pose<32;++pose) {
            const Binding b={Part(32+side*32+pose),0,0,units,units,-units,0,0};
            next_hands[side][pose]=convert(assets.hands[side][pose],b,key_base);
        }
    }
    const auto valid_strand=[](const vf1_models::Strand &strand) {
        return !strand.segments.empty() && strand.segments.size()==strand.lengths.size() &&
            std::all_of(strand.segments.begin(),strand.segments.end(),[](const auto &m){return !m.empty();}) &&
            std::all_of(strand.lengths.begin(),strand.lengths.end(),[](float n){return std::isfinite(n) && n>0 && n<1;});
    };
    std::vector<vf1_models::Strand> next_ribbons{assets.strands[0]};
    next_ribbons.insert(next_ribbons.end(),assets.extra_head_ribbons.begin(),assets.extra_head_ribbons.end());
    const bool has_ribbon=std::all_of(next_ribbons.begin(),next_ribbons.end(),valid_strand);
    const bool has_belt=(model2 || !assets.belt_knot.empty()) && valid_strand(assets.strands[1]) && valid_strand(assets.strands[2]);
    // Decoded belt knot and tails follow their source attachment. Their cloth solver
    // is not transplanted into the VF3 simulation.
    if(has_belt) {
        vf1_models::Mesh belt;
        const auto &anchor_a=assets.strands[1].anchor,&anchor_b=assets.strands[2].anchor;
        const Point knot={(anchor_a[0]+anchor_b[0])*pelvis_x/2,(anchor_a[1]+anchor_b[1])*units/2,
            -(anchor_a[2]+anchor_b[2])*units/2};
        for(auto t:assets.belt_knot) {
            for(auto &p:t.points)for(int k=0;k<3;++k)p[k]=p[k]*units+knot[k];
            belt.push_back(t);
        }
        for(size_t chain=1;chain<3;++chain) {
            const auto &strand=assets.strands[chain];float distance=0;
            for(size_t segment=0;segment<strand.segments.size();++segment) {
                for(auto t:strand.segments[segment]) {
                    for(auto &p:t.points)p={strand.anchor[0]*pelvis_x+(model2 ? -distance : distance)-p[1]*units,
                        strand.anchor[1]*units+p[0]*units,-strand.anchor[2]*units-p[2]*units};
                    belt.push_back(t);
                }
                distance+=strand.lengths[segment]*units;
            }
        }
        append(next[size_t(model2 ? Part::Torso : Part::Pelvis)],belt,120,key_base);
    }
    parts=std::move(next);hands=std::move(next_hands);ribbons=std::move(next_ribbons);
    unit_scale=units;head_z_scale=model2 ? -units : -1800;head_z_offset=model2 ? 0 : -13;
    mesh_key_base=key_base;replace_hands=has_hands;replace_ribbon=has_ribbon;replace_belt=has_belt;
    model2_source=model2;
    source_portrait=assets.portrait;
    roster_fighter=0;
    load_faces(assets);
    error.clear();return true;
}

void Vf1Akira::load_faces(const vf1_models::Assets &assets)
{
    source_faces=assets.face_frames;
    face_frame_count=std::max(unsigned(source_faces.size()),assets.face_frame_count);
    decode_face=assets.decode_face;
    face_animations=assets.face_animations;
    face_states=assets.face_states;
    face_speech=assets.face_speech;
    face_cache={};face_clock=0;
    static_head_accessory.reset();
    if(!assets.head_accessory.empty()) {
        // Authored head-local attachments (such as VF2 Jacky's static hair)
        // share the head transform. Convert once and retain them in each cached
        // expression, avoiding per-frame tessellation and full-head copies.
        const Binding binding={Part(122),0,0,unit_scale,unit_scale,head_z_scale,head_z_offset,0};
        static_head_accessory=convert(assets.head_accessory,binding,mesh_key_base);
        auto &head=parts[size_t(Part::Head)];
        head->insert(head->end(),static_head_accessory->begin(),static_head_accessory->end());
    }
}

uint16_t Vf1Akira::face_animation_frame(unsigned animation,unsigned step) const
{
    if(animation>=face_animations.size())return 0;
    const auto &timeline=face_animations[animation];
    const auto &frames=timeline.frames;
    if(frames.empty())return 0;
    if(step>=frames.size() && timeline.loop_start>=0 && size_t(timeline.loop_start)<frames.size())
        step=unsigned(timeline.loop_start+(step-timeline.loop_start)%(frames.size()-timeline.loop_start));
    return frames[std::min(size_t(step),frames.size()-1)];
}

uint16_t Vf1Akira::face_state_frame(unsigned state) const
{
    return state<face_states.size() ? face_states[state] : 0;
}

uint16_t Vf1Akira::expression_frame(const fighter_expression::Pose &pose) const
{
    if(!pose.valid)return 0;
    using Kind=fighter_expression::Kind;
    if(pose.speaking && std::isfinite(pose.mouth_open)) {
        const float open=std::clamp(pose.mouth_open,0.f,1.f);
        if(open<.02f)return 0;
        if(model2_source && !face_speech.empty())
            return face_speech[size_t(std::lround(open*(face_speech.size()-1)))];
        if(!model2_source)return face_animation_frame(5,unsigned(std::lround(open*15)));
    }
    // Source script IDs are game-specific. These are visual retargeting
    // choices, not an assumption that the two games share animation numbers.
    unsigned animation=0;
    switch(pose.kind) {
    case Kind::Selection: animation=7;break;
    case Kind::Hurt: case Kind::Defeat: animation=model2_source?8:3;break;
    case Kind::Victory: {
        constexpr unsigned vf1_victory[]={1,1,5,5,1,5,5,5};
        constexpr unsigned vf2_victory[]={1,3,1,1,3,3,1,3,6,3};
        animation=model2_source?vf2_victory[roster_fighter]:vf1_victory[roster_fighter];break;
    }
    case Kind::HurtRecovery: animation=model2_source?9:4;break;
    case Kind::VictoryRecovery: animation=model2_source?2:(roster_fighter==0 || roster_fighter==1 || roster_fighter==4)?2:6;break;
    case Kind::Neutral: return 0;
    }
    return face_animation_frame(animation,pose.tick);
}

Meshes Vf1Akira::face_meshes(uint16_t frame) const
{
    if(!frame || frame>=face_frame_count)return parts[size_t(Part::Head)];
    for(auto &entry:face_cache)if(entry.meshes && entry.frame==frame) {
        entry.used=++face_clock;return entry.meshes;
    }
    // Each expression has a separate topology identity. Retained scenes own
    // their meshes even after this small conversion cache evicts a frame.
    const uint64_t key=0x5800000000000000ull | (uint64_t(model2_source?2:1)<<52) |
        (uint64_t(roster_fighter)<<48);
    const Binding binding={Part(frame),0,0,unit_scale,unit_scale,head_z_scale,head_z_offset,0};
    vf1_models::Mesh decoded;
    const vf1_models::Mesh *geometry=frame<source_faces.size() ? &source_faces[frame] : nullptr;
    if(!geometry || geometry->empty()) {
        if(!decode_face)return parts[size_t(Part::Head)];
        try {decoded=decode_face(frame);} catch(const std::exception &) {return parts[size_t(Part::Head)];}
        if(decoded.empty())return parts[size_t(Part::Head)];
        geometry=&decoded;
    }
    auto &entry=*std::min_element(face_cache.begin(),face_cache.end(),[](const auto &a,const auto &b){return a.used<b.used;});
    auto meshes=convert(*geometry,binding,key);
    if(static_head_accessory)
        meshes->insert(meshes->end(),static_head_accessory->begin(),static_head_accessory->end());
    entry={frame,++face_clock,std::move(meshes)};
    return entry.meshes;
}

size_t Vf1Akira::apply(Scene &scene, unsigned outfit_mask, unsigned preview_mask,
                      const std::array<uint16_t,2> &face_frames) const
{
    if (!enabled || !ready() || !((outfit_mask|preview_mask) & 3)) return 0;
    if(roster_fighter) return apply_fighter(scene,outfit_mask,preview_mask,face_frames);
    static const Meshes empty = std::make_shared<std::vector<Mesh>>();
    size_t replaced=0;
    // Some stages split each fighter across a dozen lighting viewports. Keep
    // their matrix-table/camera identity rather than assuming one viewport.
    for (auto &head_vp : scene.viewports) for (auto &head : head_vp.models) {
        const int outfit = head.addr==0x9aaccf && head.source_node==0x509a0 ? 0 :
            head.addr==0xa47386 && head.source_node==0x51e58 ? 1 : -1;
        if (outfit < 0 || !((outfit_mask|preview_mask) & (1u << outfit))) continue;
        const int base=head.matrix_index;
        auto same_camera = [&](const Viewport &vp) {
            return vp.matrix_table==head_vp.matrix_table && vp.root_matrix==head_vp.root_matrix &&
                !std::memcmp(vp.root.m,head_vp.root.m,sizeof vp.root.m);
        };
        std::array<Model *,size_t(Part::Count)> found{};
        bool ambiguous=false, misplaced=false;
        for (auto &vp : scene.viewports) if (same_camera(vp))
            for (auto &m : vp.models) for (const auto &b : bindings)
                if (m.source_node==(outfit ? b.alternate : b.node)) {
                    if(int(m.matrix_index)!=base+(outfit && b.part==Part::Torso ? -4 : b.slot)) { misplaced=true;continue; }
                    auto &p=found[size_t(b.part)]; if (p) ambiguous=true; p=&m;
                }
        if (ambiguous || misplaced || found[size_t(Part::Head)]!=&head) continue;
        const size_t present=std::count_if(found.begin(),found.end(),[](const auto *p){return p;});
        const bool preview=present==1 && (preview_mask & (1u<<outfit));
        // Off-screen feet/limbs are absent from the scene, including the victory
        // close-up. Validate every visible binding rather than requiring all 13.
        if ((!preview && (!(outfit_mask & (1u<<outfit)) || present<2))) continue;
        std::array<Model *,2> wrists{};
        std::array<std::array<Model *,4>,2> fingers{};
        std::array<Model *,3> tail{};
        const uint32_t wrist_nodes[2][2]={{0x49ad8,0x49ad0},{0x499e0,0x499d8}};
        for(auto &vp:scene.viewports)if(same_camera(vp))for(auto &m:vp.models) {
            const int slot=int(m.matrix_index)-base;
            for(int side=0;side<2;++side) {
                if(slot==33+side*24 && m.source_node==wrist_nodes[outfit][side])wrists[side]=&m;
                for(int finger=0;finger<4;++finger)if(slot==39+side*24+finger*3)fingers[side][finger]=&m;
            }
            for(int segment=0;segment<3;++segment)if(slot==(preview ? 4 : 11)+segment*2 &&
                m.source_node==(outfit ? 0x76fb0u+segment*8 : 0x512b0u+segment*8))tail[segment]=&m;
        }
        Mat4 head_inverse;
        const bool ribbon_visible=std::all_of(tail.begin(),tail.end(),[](const auto *p){return p;});
        if((preview && !ribbon_visible) || !inverse(head.matrix,head_inverse))continue;
        std::array<unsigned,2> poses{};
        for(int side=0;side<2;++side) {
            if(!wrists[side])continue;
            const auto direction=axis(wrists[side]->matrix,0);float openness=0;unsigned count=0;
            for(const auto *finger:fingers[side])if(finger) {
                const auto tip=axis(finger->matrix,0);
                for(int k=0;k<3;++k)openness+=direction[k]*tip[k];
                ++count;
            }
            // A visual retargeting of VF3's current finger curl, not shared
            // animation indices. VF1's endpoints are closed and extended.
            poses[side]=count ? unsigned(std::lround(31*std::clamp((openness/count+.65f)/1.65f,0.0f,1.0f))) : 0;
        }
        vf1_models::Mesh ribbon_geometry;
        // Work in head-local units: normalising directions in view space
        // would lose the scene's model scale and enlarge close-up/LOD ribbons.
        const auto local_axis=[&](const Mat4 &matrix,int column) {
            const Point direction=axis(matrix,column);Point local{};
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)local[r]+=head_inverse.m[c*4+r]*direction[c];
            const float length=std::sqrt(local[0]*local[0]+local[1]*local[1]+local[2]*local[2]);
            if(length>1e-8f)for(float &x:local)x/=length;
            return local;
        };
        if(replace_ribbon && ribbon_visible)for(size_t chain=0;chain<ribbons.size();++chain) {
          const auto &ribbon=ribbons[chain];
          Point anchor={ribbon.anchor[0]*unit_scale,ribbon.anchor[1]*unit_scale,
              ribbon.anchor[2]*head_z_scale+head_z_offset};
          // VF2 has two five-link ribbons at one anchor. Both follow VF3's
          // three-link motion, with a small separation instead of overlapping.
          const float splay=model2_source && ribbons.size()>1 ? (chain ? -.12f : .12f) : 0;
          for(size_t segment=0;segment<ribbon.segments.size();++segment) {
            const size_t bone=std::min(size_t(2),segment*3/ribbon.segments.size());
            const auto down=local_axis(tail[bone]->matrix,0),thickness=local_axis(tail[bone]->matrix,1),
                width=local_axis(tail[bone]->matrix,2);
            for(auto t:ribbon.segments[segment]) {
                for(auto &p:t.points) {
                    Point point=anchor;
                    const float length=model2_source ? p[1] : -p[1];
                    for(int k=0;k<3;++k)point[k]+=unit_scale*(length*down[k]+p[0]*thickness[k]+(p[2]+length*splay)*width[k]);
                    p=point;
                }
                ribbon_geometry.push_back(t);
            }
            for(int k=0;k<3;++k)anchor[k]+=ribbon.lengths[segment]*unit_scale*(down[k]+splay*width[k]);
          }
        }
        auto head_with_ribbon=std::make_shared<std::vector<Mesh>>(*face_meshes(face_frames[outfit]));
        append(head_with_ribbon,ribbon_geometry,121,mesh_key_base);
        for (auto &vp : scene.viewports) if (same_camera(vp)) for (auto &m : vp.models) {
            const int slot=int(m.matrix_index)-base;
            // The animated face occupies +1 in the selection head-only layout,
            // but +25 in fights. Fight +1 is a shadow, so identify the face by
            // both its source node and the layout-specific matrix offset.
            // Reactions can replace the animated face with four static pieces.
            // These are separate nodes, including when source ribbon data is absent.
            constexpr uint32_t reaction_faces[2][4]={{0x5bf18,0x5bf28,0x5bf30,0x5bf38},
                                                     {0x5bee8,0x5bef8,0x5bf00,0x5bf08}};
            const bool face_node=m.source_node==(outfit ? 0x51e48u : 0x50990u) ||
                std::find(std::begin(reaction_faces[outfit]),std::end(reaction_faces[outfit]),m.source_node)!=std::end(reaction_faces[outfit]);
            const bool original_face=face_node && slot==(preview ? 1 : 25);
            const bool original_belt=m.source_node==(outfit ? 0x430d8u : 0x42f58u) || m.source_node==(outfit ? 0x430c8u : 0x42f48u);
            if (original_face || slot==0 || (slot>=2 && slot<=(preview ? 3 : 8)) ||
                (replace_ribbon && ribbon_visible && slot>=(preview ? 4 : 9) && slot<=(preview ? 17 : 25)) ||
                (!preview && replace_hands[0] && wrists[0] && slot>=34 && slot<=48) ||
                (!preview && replace_hands[1] && wrists[1] && slot>=58 && slot<=72) ||
                (m.source_node && (!original_belt || replace_belt) &&
                    std::find(std::begin(bridges[outfit]),std::end(bridges[outfit]),m.source_node)!=std::end(bridges[outfit])))
                m.meshes=empty;
        }
        for (size_t i=0;i<found.size();++i) if(found[i])found[i]->meshes=parts[i];
        found[size_t(Part::Head)]->meshes=std::move(head_with_ribbon);
        for(size_t side=0;side<2;++side)if(replace_hands[side] && wrists[side])wrists[side]->meshes=hands[side][poses[side]];
        ++replaced;
    }
    return replaced;
}

bool Vf1Akira::load_fighter_assets(const vf1_models::Assets &assets,AkiraSource source,
                                 std::string &error,unsigned fighter)
{
    if(fighter>=fighter_layouts::fighters.size() || (source==AkiraSource::VF1 && fighter>=8) ||
       std::any_of(assets.parts.begin(),assets.parts.end(),[](const auto &p){return p.empty();})) {
        error="Missing fighter body parts";return false;
    }
    const auto &layout=fighter_layouts::fighters[fighter].outfits[0];
    if(!layout.head_node) {error="Fighter skeleton has not been verified";return false;}
    constexpr float units=2048;
    const bool model2=source==AkiraSource::VF2;
    const uint64_t key=0x5700000000000000ull | (uint64_t(model2?2:1)<<48) | (uint64_t(fighter)<<40);
    constexpr unsigned bone[]={0,0,3,4,6,7,10,11,0,13,14,0,0};
    decltype(parts) next;
    for(size_t i=0;i<next.size();++i) {
        auto mesh=assets.parts[i];
        Binding b={Part(i),0,0,units,units,-units,0,0};
        const float length=assets.source_joints[bone[i]][0];
        if(bone[i] && length>.01f && layout.parts[i].joint_length>0)
            b.sx=layout.parts[i].joint_length/length;
        if(i==size_t(Part::Torso)) {
            const float neck=assets.source_joints[1][0],shoulder=assets.source_joints[2][0];
            const float width=std::abs(assets.source_joints[2][2]);
            if(neck>shoulder && shoulder>.01f && width>.01f && layout.neck_length>layout.shoulder_height &&
               layout.shoulder_height>0 && layout.shoulder_half_width>0) {
                for(auto &t:mesh)for(auto &p:t.points) {
                    p[0]=p[0]<=shoulder ? p[0]*layout.shoulder_height/(shoulder*units) :
                        (layout.shoulder_height+(p[0]-shoulder)*(layout.neck_length-layout.shoulder_height)/(neck-shoulder))/units;
                    p[2]*=layout.shoulder_half_width/(width*units);
                }
            }
            if(model2)fit_vf2_waist(mesh,assets.parts[size_t(Part::Pelvis)],layout.shoulder_height/units);
        }
        next[i]=convert(mesh,b,key);
    }
    decltype(hands) next_hands;
    std::array<bool,2> has_hands{};
    for(unsigned side=0;side<2;++side) {
        has_hands[side]=std::all_of(assets.hands[side].begin(),assets.hands[side].end(),[](const auto &m){return !m.empty();});
        if(has_hands[side])for(unsigned pose=0;pose<32;++pose)
            next_hands[side][pose]=convert(assets.hands[side][pose],{Part(32+side*32+pose),0,0,units,units,-units,0,0},key);
    }
    std::vector<vf1_models::Strand> head_chains;
    unsigned accessory_identity=128;
    auto add_chain=[&](const vf1_models::Strand &chain) {
        if(chain.segments.empty() || chain.segments.size()!=chain.lengths.size() ||
           std::any_of(chain.segments.begin(),chain.segments.end(),[](const auto &m){return m.empty();}))return;
        if(chain.bone==2) {head_chains.push_back(chain);return;}
        if(chain.bone!=1 && chain.bone!=9)return;
        vf1_models::Mesh geometry;
        float distance=0;
        for(size_t s=0;s<chain.segments.size();++s) {
            for(auto t:chain.segments[s]) {
                for(auto &p:t.points)p={chain.anchor[0]*units+(model2?-distance:distance)-p[1]*units,
                    (chain.anchor[1]+p[0])*units,-(chain.anchor[2]+p[2])*units};
                geometry.push_back(t);
            }
            distance+=chain.lengths[s]*units;
        }
        append(next[size_t(chain.bone==1 ? Part::Torso : Part::Pelvis)],geometry,accessory_identity++,key);
    };
    for(const auto &chain:assets.strands)add_chain(chain);
    for(const auto &chain:assets.extra_head_ribbons)add_chain(chain);
    for(const auto &chain:assets.extra_strands)add_chain(chain);
    if(!assets.belt_knot.empty()) {
        auto knot=assets.belt_knot;
        const auto &a=assets.strands[1].anchor,&b=assets.strands[2].anchor;
        for(auto &t:knot)for(auto &p:t.points)for(unsigned k=0;k<3;++k)p[k]=(p[k]+(a[k]+b[k])/2)*units*(k==2?-1:1);
        append(next[size_t(Part::Pelvis)],knot,119,key);
    }
    parts=std::move(next);hands=std::move(next_hands);ribbons=std::move(head_chains);
    replace_hands=has_hands;roster_fighter=fighter;model2_source=model2;mesh_key_base=key;
    unit_scale=units;head_z_scale=-units;head_z_offset=0;
    load_faces(assets);
    source_portrait=assets.portrait;error.clear();return true;
}

size_t Vf1Akira::apply_fighter(Scene &scene,unsigned outfit_mask,unsigned preview_mask,
                              const std::array<uint16_t,2> &face_frames) const
{
    static const Meshes empty=std::make_shared<std::vector<Mesh>>();
    size_t replaced=0;
    const auto &fighter=fighter_layouts::fighters[roster_fighter];
    for(unsigned outfit=0;outfit<2;++outfit) {
        if(!((outfit_mask|preview_mask)&(1u<<outfit)))continue;
        const auto &layout=fighter.outfits[outfit];
        if(!layout.head_node)continue;
        for(auto &head_vp:scene.viewports)for(auto &head:head_vp.models) {
            if(head.source_node!=layout.head_node || (layout.head_addr && head.addr!=layout.head_addr))continue;
            const int base=head.matrix_index;
            auto same_camera=[&](const Viewport &v) {
                return v.matrix_table==head_vp.matrix_table && v.root_matrix==head_vp.root_matrix &&
                    !std::memcmp(v.root.m,head_vp.root.m,sizeof v.root.m);
            };
            // Native detail culling changes the number of matrices allocated before
            // and between limbs (notably Pai's alternate outfit). Source nodes are
            // outfit-specific; use their identities and physical joint distances,
            // retaining fixed torso/head anchors and rejecting ambiguous instances.
            std::array<Model *,size_t(Part::Count)> found{};
            bool invalid=false;
            for(auto &v:scene.viewports)if(same_camera(v))for(auto &m:v.models)
                for(size_t i=0;i<found.size();++i)if(layout.parts[i].node && m.source_node==layout.parts[i].node) {
                    const int slot=int(m.matrix_index)-base;
                    // Shun's coat also draws the pelvis mesh at a torso attachment.
                    if(roster_fighter==8 && outfit==0 && i==size_t(Part::Pelvis) && slot==-2)continue;
                    const bool anchor=i==size_t(Part::Head) || i==size_t(Part::Torso);
                    if(found[i] || (anchor ? slot!=layout.parts[i].slot : slot<=0))invalid=true;
                    else found[i]=&m;
                }
            if(invalid || found[size_t(Part::Head)]!=&head)continue;
            const size_t count=std::count_if(found.begin(),found.end(),[](auto p){return p;});
            const bool preview=count==1 && (preview_mask&(1u<<outfit));
            if(!preview && (count<2 || !(outfit_mask&(1u<<outfit))))continue;
            std::array<Model *,2> wrists{};
            for(auto &v:scene.viewports)if(same_camera(v))for(auto &m:v.models)
                for(unsigned side=0;side<2;++side)
                    if(layout.wrist_nodes[side] && m.source_node==layout.wrist_nodes[side]) {
                        if(wrists[side] || int(m.matrix_index)<=base)invalid=true;
                        else wrists[side]=&m;
                    }
            auto joint_matches=[&](const Model *parent,const Model *child,float expected) {
                if(!parent || !child || expected<=0)return true; // a culled limb is valid
                Mat4 inv;
                if(!inverse(parent->matrix,inv))return false;
                const auto p=transform(inv,{child->matrix.m[12],child->matrix.m[13],child->matrix.m[14]});
                const float length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
                return std::isfinite(length) && std::abs(length-expected)<=2.f+expected*.05f;
            };
            if(!preview) {
                invalid |= !joint_matches(found[size_t(Part::Torso)],&head,layout.neck_length);
                constexpr unsigned parents[]={2,4,6,7,9,10};
                constexpr unsigned children[]={3,5,7,8,10,11};
                for(unsigned i=0;i<6;++i)
                    invalid |= !joint_matches(found[parents[i]],found[children[i]],layout.parts[parents[i]].joint_length);
                invalid |= !joint_matches(found[3],wrists[0],layout.parts[3].joint_length);
                invalid |= !joint_matches(found[5],wrists[1],layout.parts[5].joint_length);
            }
            if(invalid)continue;
            Mat4 inverse_head;
            if(!inverse(head.matrix,inverse_head))continue;
            auto with_hair=face_meshes(face_frames[outfit]);
            if(!ribbons.empty()) {
                with_hair=std::make_shared<std::vector<Mesh>>(*with_hair);
                vf1_models::Mesh hair;
                for(size_t chain_index=0;chain_index<ribbons.size();++chain_index) {
                    const auto &chain=ribbons[chain_index];
                    Point anchor={chain.anchor[0]*2048,chain.anchor[1]*2048,-chain.anchor[2]*2048};
                    for(size_t segment=0;segment<chain.segments.size();++segment) {
                        Point down=chain.rest_direction,thickness={1,0,0},width={0,1,0};
                        if(!layout.head_cloth.empty()) {
                            const auto &binding=layout.head_cloth[std::min(layout.head_cloth.size()-1,
                                (chain_index*chain.segments.size()+segment)*layout.head_cloth.size()/(ribbons.size()*chain.segments.size()))];
                            for(auto &v:scene.viewports)if(same_camera(v))for(auto &m:v.models)if(m.source_node==binding.node) {
                                auto local_axis=[&](unsigned col) {
                                    Point p{};const auto direction=axis(m.matrix,int(col));
                                    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)p[r]+=inverse_head.m[c*4+r]*direction[c];
                                    const float length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
                                    if(length>1e-8f)for(auto &x:p)x/=length;
                                    return p;
                                };
                                down=local_axis(0);thickness=local_axis(1);width=local_axis(2);
                            }
                        }
                        for(auto t:chain.segments[segment]) {
                            for(auto &p:t.points) {
                                Point q=anchor;
                                for(unsigned k=0;k<3;++k)q[k]+=2048*((model2_source?p[1]:-p[1])*down[k]+p[0]*thickness[k]+p[2]*width[k]);
                                p=q;
                            }
                            hair.push_back(t);
                        }
                        for(unsigned k=0;k<3;++k)anchor[k]+=chain.lengths[segment]*2048*down[k];
                    }
                }
                append(with_hair,hair,121,mesh_key_base);
            }
            for(auto &v:scene.viewports)if(same_camera(v))for(auto &m:v.models) {
                const bool owned=std::find(layout.owned_nodes.begin(),layout.owned_nodes.end(),m.source_node)!=layout.owned_nodes.end();
                if(owned || m.source_node==layout.face_node)m.meshes=empty;
            }
            for(size_t i=0;i<found.size();++i)if(found[i])found[i]->meshes=parts[i];
            head.meshes=std::move(with_hair);
            for(unsigned side=0;side<2;++side)if(wrists[side] && replace_hands[side]) {
                float openness=0;unsigned fingers=0;
                const auto direction=axis(wrists[side]->matrix,0);
                for(auto &v:scene.viewports)if(same_camera(v))for(auto &m:v.models) {
                    const int relative=int(m.matrix_index)-int(wrists[side]->matrix_index);
                    if(relative>=6 && relative<=15 && relative%3==0) {
                        const auto tip=axis(m.matrix,0);
                        for(unsigned k=0;k<3;++k)openness+=direction[k]*tip[k];
                        ++fingers;
                    }
                }
                const unsigned pose=fingers ? unsigned(std::lround(31*std::clamp((openness/fingers+.65f)/1.65f,0.f,1.f))) : 0;
                wrists[side]->meshes=hands[side][pose];
            }
            ++replaced;
        }
    }
    return replaced;
}
}
