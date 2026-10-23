// Optional local-ROM cache/ownership regression; no ROM-derived fixture is stored.
#include "../src/video/fighter_appearances.h"
#include "../src/video/fighter_layouts.h"
#include "../src/host/akira_selection.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>

namespace {
void require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
video::Viewport preview(unsigned fighter,unsigned outfit,unsigned table)
{
    const auto &layout=fighter_layouts::fighters[fighter].outfits[outfit];
    video::Viewport view;view.matrix_table=table;view.root_matrix=0;
    for(const auto [node,address,slot]:{std::array<unsigned,3>{layout.head_node,layout.head_addr,100},
            {layout.face_node,1,101},{0xdead00u+table,2,102}}) {
        video::Model model;model.source_node=node;model.addr=address;model.matrix_index=uint16_t(slot);
        model.matrix=video::Mat4::identity();model.meshes=std::make_shared<std::vector<video::Mesh>>(1);
        view.models.push_back(std::move(model));
    }
    return view;
}
uint64_t geometry_hash(const video::Meshes &meshes)
{
    uint64_t result=1469598103934665603ull;
    auto byte=[&](uint8_t value){result=(result^value)*1099511628211ull;};
    for(const auto &mesh:*meshes) {
        if(mesh.model2_texture)for(auto value:mesh.model2_texture->words)
            for(unsigned i=0;i<4;++i)byte(uint8_t(value>>(8*i)));

        for(unsigned i=0;i<8;++i)byte(uint8_t(mesh.key>>(8*i)));
        for(const auto &vertex:mesh.verts) {
            for(float coordinate:vertex.pos) {
                uint32_t bits;std::memcpy(&bits,&coordinate,4);
                for(unsigned i=0;i<4;++i)byte(uint8_t(bits>>(8*i)));
            }
            for(auto colour:vertex.colour)byte(colour);
        }
    }
    return result;
}
}
int main(int argc,char **argv)
{
    if(argc!=3){std::fprintf(stderr,"usage: fighter_appearances_test vf.zip vf2.zip\n");return 2;}
    try {
        std::string error;
        video::FighterAppearances vf1(video::AkiraSource::VF1),vf2(video::AkiraSource::VF2);
        require(vf1.load(argv[1],error),error.c_str());require(vf2.load(argv[2],error),error.c_str());
        vf1.enabled=vf2.enabled=true;
        require(vf1.ready(7) && !vf1.ready(8) && vf2.ready(9) && !vf2.ready(255),"roster availability mismatch");
        video::Scene first;first.viewports.push_back(preview(1,0,1));
        require(vf1.apply(first,0,1,1)==1,"initial Pai preview failed");
        const auto retained=first.viewports[0].models[0].meshes;
        const auto hash=geometry_hash(retained);
        const auto portrait=vf1.portrait(1).pixels;
        require(!portrait.empty(),"loaded Pai portrait missing");
        // Touch two more fighters to evict Pai while a published scene retains her meshes.
        for(unsigned fighter:{2u,3u}) {
            video::Scene next;next.viewports.push_back(preview(fighter,0,fighter+1));
            require(vf1.apply(next,0,1,fighter)==1,"cache replacement preview failed");
        }
        require(!vf1.portrait(1).available(),"third distinct fighter did not evict oldest cache entry");
        require(!retained->empty() && geometry_hash(retained)==hash,"eviction invalidated published scene geometry");
        video::Scene reloaded;reloaded.viewports.push_back(preview(1,0,4));
        require(vf1.apply(reloaded,0,1,1)==1,"evicted fighter reload failed");
        require(geometry_hash(reloaded.viewports[0].models[0].meshes)==hash && vf1.portrait(1).pixels==portrait,
                "cache reload changed geometry or portrait");
        require(!vf1.load("/not/a/companion.zip",error),"missing replacement ROM accepted");
        require(vf1.ready(1) && vf1.portrait(1).pixels==portrait,"failed ROM reload discarded working cache");
        // Inactive P2 can retain Akira's hurt state while P1 chooses Akira.
        // Exercise the complete RAM -> tracker -> cache adapter path: the
        // stale slot must not replace P1's neutral face with closed eyes.
        std::vector<uint8_t> ram(0x200000);
        const auto word=[&](size_t at,uint32_t value){std::memcpy(ram.data()+at,&value,4);};
        word(0x108000,5);word(0x10a7c8,0x0a0006c6);
        fighter_expression::Tracker expressions;
        for(unsigned tick=0;tick<200;++tick)expressions.update(fighter_expression::snapshot(ram));
        video::Scene hover;hover.viewports={preview(0,0,20)};
        for(unsigned segment=0;segment<3;++segment) {
            video::Model ribbon;ribbon.source_node=0x512b0+segment*8;
            ribbon.matrix_index=uint16_t(104+segment*2);ribbon.matrix=video::Mat4::identity();
            ribbon.meshes=std::make_shared<std::vector<video::Mesh>>();
            hover.viewports[0].models.push_back(std::move(ribbon));
        }
        auto neutral_hover=hover;
        require(vf2.apply(neutral_hover,0,1,0)==1 &&
                vf2.apply(hover,0,1,0,expressions.poses())==1,"Akira hover preview missing");
        require(geometry_hash(hover.viewports[0].models[0].meshes)==
                geometry_hash(neutral_hover.viewports[0].models[0].meshes),
                "inactive player's hurt expression changed the active preview");
        // Mirror match: each player chooses a different source edition of Pai.
        AkiraSelection selection;AkiraSelection::Snapshot state{};m3::Controls input;
        const unsigned id=fighter_roster::game_ids[1];
        state[0]={true,true,false,1,true,id,false,1};
        state[1]={true,true,false,2,true,id+13,false,1};
        selection.update(input,state,true,true);
        input.up[0]=input.up[1]=true;selection.update(input,state,true,true);
        input={};selection.update(input,state,true,true);
        input.up[1]=true;selection.update(input,state,true,true);
        require(selection.mask(state,1)==1 && selection.mask(state,2)==2,"mixed source selection ownership failed");
        video::Scene mixed;mixed.viewports={preview(1,0,10),preview(1,1,11)};
        const auto original=mixed;
        require(vf1.apply(mixed,0,AkiraSelection::previews(state,selection.mask(state,1),1),1)==1,"VF1 mirror player failed");
        const auto p1=mixed.viewports[0].models[0].meshes;
        require(mixed.viewports[1].models[0].meshes==original.viewports[1].models[0].meshes,"VF1 changed VF2 player's preview");
        require(vf2.apply(mixed,0,AkiraSelection::previews(state,selection.mask(state,2),1),1)==1,"VF2 mirror player failed");
        require(mixed.viewports[0].models[0].meshes==p1,"VF2 overwrote VF1 player");
        std::set<uint64_t> keys;
        for(const auto &mesh:*p1)keys.insert(mesh.key);
        for(const auto &mesh:*mixed.viewports[1].models[0].meshes)
            require(!keys.count(mesh.key),"different source editions share mesh identity");
        for(unsigned player=0;player<2;++player) {
            require(mixed.viewports[player].models[1].meshes->empty(),"original face was not suppressed");
            require(mixed.viewports[player].models[2].meshes==original.viewports[player].models[2].meshes,"unrelated model changed");
        }
        vf1.enabled=false;
        video::Scene disabled;disabled.viewports.push_back(preview(4,0,12));
        const auto untouched=disabled.viewports[0].models[0].meshes;
        require(vf1.apply(disabled,0,1,4)==0 && disabled.viewports[0].models[0].meshes==untouched,
                "disabled cache changed scene");
        require(!vf1.portrait(4).available(),"disabled appearance decoded an unused fighter");
        vf1.enabled=true;
        require(vf1.apply(disabled,3,3,255)==0 && vf1.apply(disabled,3,3,8)==0,"unsupported VF1 fighter accepted");
        std::puts("Fighter appearance cache: eviction lifetime, reload, failed replacement, mixed-source mirror ownership, disabled and unsupported paths passed");
    }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
