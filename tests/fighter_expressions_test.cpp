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
    if(fighter==0)view.models[2].matrix_index=200;
    if(fighter==0)for(unsigned segment=0;segment<3;++segment) {
        video::Model ribbon;ribbon.source_node=(outfit?0x76fb0u:0x512b0u)+segment*8;
        ribbon.matrix_index=104+segment*2;ribbon.matrix=video::Mat4::identity();
        ribbon.meshes=std::make_shared<std::vector<video::Mesh>>();view.models.push_back(ribbon);
    }
    return view;
}
uint64_t hash(const video::Meshes &meshes)
{
    uint64_t result=1469598103934665603ull;
    auto byte=[&](uint8_t b){result=(result^b)*1099511628211ull;};
    for(const auto &mesh:*meshes) {
        if(mesh.model2_texture)for(auto value:mesh.model2_texture->words)
            for(unsigned i=0;i<4;++i)byte(uint8_t(value>>(8*i)));

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
    const bool rigid_hair=!assets.head_accessory.empty();
    assets={}; // the renderer must own its source frames and lazy recipe
    video::Meshes retained;uint64_t retained_hash=0,neutral=0,hair_hash=0;std::set<uint64_t> variants;
    for(unsigned frame=0;frame<count;++frame) {
        video::Scene scene;scene.viewports={preview(fighter,0),preview(fighter,1)};
        const auto untouched=scene.viewports[0].models[2].meshes;
        const auto transforms=scene;
        require(model.apply(scene,0,3,{uint16_t(frame),0})==2,"mirror expression preview failed");
        const auto &left=scene.viewports[0].models[0].meshes,&right=scene.viewports[1].models[0].meshes;
        if(!frame)neutral=hash(left);
        require(hash(right)==neutral,"one player's expression changed the other player");
        variants.insert(hash(left));
        if(rigid_hair) {
            auto hair=std::make_shared<std::vector<video::Mesh>>();
            for(const auto &mesh:*left)if(((mesh.key>>32)&255)==122)hair->push_back(mesh);
            require(!hair->empty(),"authored hair disappeared from a facial frame");
            if(!frame)hair_hash=hash(hair);
            require(hash(hair)==hair_hash,"expression distorted authored hair");
            video::Scene repeated;repeated.viewports={preview(fighter,0)};
            require(model.apply(repeated,0,1,{uint16_t(frame),0})==1 &&
                    repeated.viewports[0].models[0].meshes==left,
                    "rigid head was rebuilt instead of reusing its cached geometry");
        }
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
void check_blink(const char *zip)
{
    using namespace vf1_models;
    Assets a;std::string error;require(vf2_models::load(zip,a,error),error.c_str());
    require(a.independent_blink && a.face_frame_count==79,"Akira native bank changed");
    const auto neutral=a.decode_face(0);
    auto same=[](const Triangle &x,const Triangle &y) {
        return x.material==y.material && x.points==y.points && x.uv==y.uv && x.colour==y.colour;
    };
    std::vector<bool> animated(neutral.size());
    for(unsigned f=1;f<a.face_frame_count;++f) {
        const auto frame=a.decode_face(f);
        for(size_t i=0;i<frame.size();++i)animated[i]=animated[i] ||
            frame[i].material!=neutral[i].material || frame[i].uv!=neutral[i].uv;
    }
    unsigned decals=0;
    for(unsigned f=0;f<a.face_frame_count;++f) {
        const auto frame=a.decode_face(f);
        for(size_t i=0;i<frame.size();++i) {
            require((frame[i].depth_bias>0)==animated[i],"depth bias missing or applied to skin/hair");
            if(!f)decals+=animated[i];
        }
    }
    require(decals>0 && decals<neutral.size(),"empty or whole-head decal mask");
    for(size_t part=0;part<size_t(Part::Head);++part)
        for(const auto &t:a.parts[part])require(t.depth_bias==0,"face bias leaked into body");
    video::Vf1Akira model;require(model.load_assets(a,video::AkiraSource::VF2,error),error.c_str());model.enabled=true;
    unsigned comparisons=0;
    for(unsigned state:{1u,2u}) {
        const auto hurt=a.decode_face(a.face_states[state]);
        for(auto base:a.face_speech) {
            const auto speaking=a.decode_face(base),blink=a.decode_face(face_variant::blink(base,state));
            require(blink.size()==speaking.size(),"blink changed triangle count");
            unsigned eyes=0;
            for(size_t i=0;i<blink.size();++i) {
                if(!same(speaking[i],neutral[i]))require(same(blink[i],speaking[i]),"blink overwrote speech geometry or texture");
                if(same(blink[i],speaking[i]))continue;
                ++eyes;require(!same(hurt[i],neutral[i]),"blink replaced a non-eye triangle");
                const auto &b=*a.textures[neutral[i].material-1],&e=*a.textures[hurt[i].material-1],&t=*a.textures[blink[i].material-1];
                // Each combined pixel must be the neutral brow, the source
                // eyelid, or transparent, while keeping palette and lighting.
                bool changed_brow=false,kept_eye=false;
                for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x) {
                    const size_t at=128+y*t.width+x;const auto v=t.words[at];
                    require(v==15 || v==b.words[at] || v==e.words[at],"blink invented a texel");
                    changed_brow|=v!=15 && v==b.words[at] && v!=e.words[at];
                    kept_eye|=v!=15 && v==e.words[at] && v!=b.words[at];
                }
                require(changed_brow && kept_eye,"blink failed to combine neutral brow and eyelid");
            }
            for(size_t i=0;i<blink.size();++i)require(blink[i].depth_bias==speaking[i].depth_bias,"blink lost decal depth");
            require(eyes>0,"blink changed no eyes");++comparisons;
            fighter_expression::Pose pose;pose.valid=true;pose.speaking=true;pose.blink=state;
            for(unsigned opening=0;opening<=16;++opening) {
                pose.mouth_open=opening/16.f;
                const auto selected=model.expression_frame(pose);
                require((selected>>face_variant::BlinkShift)==state,"speech discarded blink");
                require((selected&face_variant::FrameMask)==a.face_speech[opening],"blink changed speech aperture");
            }
        }
        fighter_expression::Pose pose;pose.valid=true;pose.blink=state;
        require(model.expression_frame(pose)==face_variant::blink(0,state),"neutral blink missing");
        pose.kind=fighter_expression::Kind::Hurt;
        require(model.expression_frame(pose)==model.face_animation_frame(8,0),"cosmetic blink overrides hurt");
        video::Scene scene;scene.viewports={preview(0,0),preview(0,1)};
        require(model.apply(scene,0,3,{face_variant::blink(a.face_speech.back(),state),0})==2,"blink preview failed");
        require(hash(scene.viewports[0].models[0].meshes)!=hash(scene.viewports[1].models[0].meshes),"blink/speech leaked to opponent");
    }
    std::printf("Akira blink: %u speech/eyelid combinations preserve mouth, brows, palettes and player isolation\n",comparisons);
}

}
int main(int argc,char **argv)
{
    if(argc!=3 && argc!=4){std::fprintf(stderr,"usage: fighter-expressions-test vf.zip vf2.zip [1|2]\n");return 2;}
    try {
        if(argc==3 || std::string(argv[3])=="1")check(argv[1],video::AkiraSource::VF1,1);
        if(argc==3 || std::string(argv[3])=="2") {
            check(argv[2],video::AkiraSource::VF2,0);
            check_blink(argv[2]);
            check(argv[2],video::AkiraSource::VF2,2);
            check(argv[2],video::AkiraSource::VF2,7);
        }
    }
    catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
