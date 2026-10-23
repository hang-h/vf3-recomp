// Local-ROM expression retargeting checks. No source assets are stored here.
#include "../src/video/vf1_akira.h"
#include "../src/video/fighter_layouts.h"
#include "../src/host/vf2_models.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
video::Viewport preview(unsigned fighter,unsigned outfit)
{
    const auto &layout=fighter_layouts::fighters[fighter].outfits[outfit];
    video::Viewport view;view.matrix_table=outfit+1;view.root_matrix=0;
    for(const auto [node,address,slot]:{std::array<unsigned,3>{layout.head_node,layout.head_addr,100},
        {layout.face_node,1,101},{0xdead00u+outfit,2,102}}) {
        video::Model model;model.source_node=node;model.addr=address;model.matrix_index=uint16_t(slot);
        model.matrix=video::Mat4::identity();model.meshes=std::make_shared<std::vector<video::Mesh>>(1);
        view.models.push_back(std::move(model));
    }
    return view;
}
uint64_t hash(const video::Meshes &meshes)
{
    uint64_t result=1469598103934665603ull;
    auto byte=[&](uint8_t b){result=(result^b)*1099511628211ull;};
    for(const auto &mesh:*meshes) {
        for(const auto &v:mesh.verts) {
            for(float coordinate:v.pos) {
                uint32_t bits;std::memcpy(&bits,&coordinate,4);
                for(unsigned i=0;i<4;++i)byte(uint8_t(bits>>(8*i)));
            }
            for(auto c:v.colour)byte(c);
        }
    }
    return result;
}
void check(const char *zip,video::AkiraSource source,unsigned fighter)
{
    vf1_models::Assets assets;std::string error;
    const bool decoded=source==video::AkiraSource::VF1 ? vf1_models::load(zip,assets,error,fighter) :
        vf2_models::load(zip,assets,error,fighter);
    require(decoded,error.c_str());
    const unsigned count=std::max(unsigned(assets.face_frames.size()),assets.face_frame_count);
    require(count>2,"missing expression bank");
    video::Vf1Akira model;require(model.load_assets(assets,source,error,fighter),error.c_str());model.enabled=true;
    const auto animations=assets.face_animations;
    assets={}; // the renderer must own its source frames and lazy recipe
    video::Meshes retained;uint64_t retained_hash=0,neutral=0;std::set<uint64_t> variants;
    for(unsigned frame=0;frame<count;++frame) {
        video::Scene scene;scene.viewports={preview(fighter,0),preview(fighter,1)};
        const auto untouched=scene.viewports[0].models[2].meshes;
        const auto transforms=scene;
        require(model.apply(scene,0,3,{uint16_t(frame),0})==2,"mirror expression preview failed");
        const auto &left=scene.viewports[0].models[0].meshes,&right=scene.viewports[1].models[0].meshes;
        if(!frame)neutral=hash(left);
        require(hash(right)==neutral,"one player's expression changed the other player");
        variants.insert(hash(left));
        if(frame==1){retained=left;retained_hash=hash(left);}
        for(unsigned player=0;player<2;++player) {
            require(scene.viewports[player].models[1].meshes->empty(),"VF3 face remains over source expression");
            require(!std::memcmp(scene.viewports[player].models[0].matrix.m,
                    transforms.viewports[player].models[0].matrix.m,sizeof(video::Mat4)),"expression moved skeleton");
        }
        require(scene.viewports[0].models[2].meshes==untouched,"expression changed unrelated geometry");
    }
    require(variants.size()>2,"facial frames collapsed");
    require(retained && hash(retained)==retained_hash,"face cache eviction invalidated a retained scene");
    video::Scene invalid;invalid.viewports={preview(fighter,0)};
    require(model.apply(invalid,0,1,{65535,0})==1 && hash(invalid.viewports[0].models[0].meshes)==neutral,
            "invalid facial frame did not fall back to neutral");
    for(unsigned animation=0;animation<animations.size();++animation) {
        const auto &timeline=animations[animation].frames;
        for(unsigned step=0;step<timeline.size();++step)
            require(model.face_animation_frame(animation,step)==timeline[step],"native timeline mapping changed");
        if(!timeline.empty()) {
            const int loop=animations[animation].loop_start;
            const size_t index=loop<0 ? timeline.size()-1 : loop+(999-loop)%(timeline.size()-loop);
            require(model.face_animation_frame(animation,999)==timeline[index],"native timeline end/loop changed");
        }
    }
    require(model.face_animation_frame(999,0)==0 && model.face_state_frame(999)==0,"unknown facial state accepted");
    std::printf("%s fighter %u: %zu source frames, %zu rendered variants; mirror isolation and cache lifetime passed\n",
                source==video::AkiraSource::VF1?"VF1":"VF2",fighter,size_t(count),variants.size());
}
}
int main(int argc,char **argv)
{
    if(argc!=3 && argc!=4){std::fprintf(stderr,"usage: fighter-expressions-test vf.zip vf2.zip [1|2]\n");return 2;}
    try {
        if(argc==3 || std::string(argv[3])=="1")check(argv[1],video::AkiraSource::VF1,1);
        if(argc==3 || std::string(argv[3])=="2")check(argv[2],video::AkiraSource::VF2,2);
    }
    catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
