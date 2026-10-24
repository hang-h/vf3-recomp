// Local-ROM regression for Pai's baked victory speech faces. No ROM fixtures.
#include "../../src/machine/model3.h"
#include "../../src/rec/ppc_rec.h"
#include "../../src/host/vf1_models.h"
#include "../../src/host/vf2_models.h"
#include "../../src/host/fighter_expression.h"
#include "../../src/host/png.h"
#include "../../src/video/vf1_akira.h"
#include "../../src/video/fighter_layouts.h"
#include "../../src/video/r3d_raster.h"
#include "../../src/video/tilegen.h"
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace {
m3::Model3 board;
ppcrec::Rec recompiler;
video::SceneBuilder builder;
video::Raster raster;
video::TileGen tiles;
void require(bool ok,const std::string &message) {if(!ok)throw std::runtime_error(message);}
void picture(const video::Scene &scene,const std::filesystem::path &path)
{
    std::vector<uint32_t> below,above;tiles.aligned(below,above);
    raster.render(scene,board.tex_ram.data());raster.compose(below.data(),below.data());
    for(size_t i=0;i<below.size();++i)if(above[i]>>24)below[i]=above[i];
    require(write_png(path.string().c_str(),below.data(),video::W,video::H),"cannot write screenshot");
}
}

int main(int argc,char **argv)
{
    if(argc<5 || argc>7) {
        fprintf(stderr,"usage: pai-victory-check vf3.zip vf.zip vf2.zip output-dir [button 0=punch, 1=kick] [outfit 0..1]\n");return 2;
    }
    try {
        const unsigned button=argc>5?unsigned(std::stoul(argv[5])):0;
        const unsigned winner=argc>6?unsigned(std::stoul(argv[6])):0;
        require(button<2 && winner<2,"invalid victory button or outfit");
        const unsigned loser=winner^1,mask=1u<<winner;
        const std::filesystem::path output=argv[4];std::filesystem::create_directories(output);
        std::string error;
        require(board.load(argv[1],error),error);
        require(recompiler.build(board.cpu,board.program_image()),"PPC recompilation failed");
        board.cpu.rec=&recompiler;board.reset();
        video::Vf1Akira models[2];vf1_models::Assets assets;
        require(vf1_models::load(argv[2],assets,error,1,winner),error);
        require(models[0].load_assets(assets,video::AkiraSource::VF1,error,1),error);
        require(vf2_models::load(argv[3],assets,error,1,true,winner),error);
        require(models[1].load_assets(assets,video::AkiraSource::VF2,error,1),error);
        for(auto &model:models)model.enabled=true;
        assets={};raster.resize(video::W,video::H);
        board.on_line=[](unsigned y){if(y==0)tiles.clear();tiles.line(board,y);};
        fighter_expression::Tracker tracker;
        // Observed native CROM face nodes, independent of the ownership list.
        constexpr uint32_t faces[2][4]={{0x513e8,0x513f0,0x513f8,0x51400},
                                       {0x515d0,0x52198,0x521a0,0x521a8}};
        std::set<uint32_t> seen;std::set<int> slots;std::set<unsigned> animations;
        unsigned checked=0;
        auto press=[](unsigned f,unsigned at,unsigned length=4){return f>=at && f<at+length;};
        for(unsigned f=0;f<6500;++f) {
            board.controls={};auto &c=board.controls;
            c.coin[0]=press(f,1310)||press(f,1330)||press(f,1350)||press(f,1370);
            c.start[0]=press(f,1390);c.start[1]=press(f,2200,12);
            if(winner==0) {
                c.right[0]=press(f,1550);c.down[1]=press(f,2650);
            } else {
                c.down[0]=press(f,1530);
                for(unsigned i=0;i<5;++i)c.right[0]|=press(f,1550+i*30);
                for(unsigned i=0;i<4;++i)c.left[1]|=press(f,2700+i*40);
            }
            c.punch[0]=press(f,1800);c.punch[1]=press(f,3100);
            // Only the isolated test changes RAM: force the normal stage
            // picker to load Taka's stage, then arrange a non-perfect KO.
            // Perfect victories use another face path and hid this regression.
            if(f==1700)for(unsigned i=0;i<16;++i)board.write8(0x76fd6+i,12);
            if(f>=3500) {
                const auto hp=0x1087d6+0x2000*loser;
                if(f<5000 && board.read16(hp)>1)board.write16(hp,1);
                if(f==3500)board.write16(0x1087d6+0x2000*winner,100);
                c.punch[winner]=(f%20)<4;c.kick[winner]=(f%27)<4;
                c.right[winner]=winner==0;c.left[winner]=winner==1;
                // 40048 chooses the native victory from these held buttons.
                if(board.read8(0x10200b)>=12) {
                    c.right[winner]=c.left[winner]=false;
                    c.punch[winner]=button==0;c.kick[winner]=button==1;
                }
            }
            board.run_frame();
            const auto sample=fighter_expression::snapshot(board.ram);tracker.update(sample);
            if(!sample[winner].victory)continue;
            require(sample[winner].actor==6+13*winner,"wrong winning actor");
            animations.insert(sample[winner].animation);
            video::Scene original;builder.build(board,original);
            const auto &layout=fighter_layouts::fighters[1].outfits[winner];
            bool capture=false;
            for(const auto &vp:original.viewports)for(const auto &m:vp.models)
                for(auto node:faces[winner])if(m.source_node==node) {
                    capture|=seen.insert(node).second;
                    for(const auto &v:original.viewports)for(const auto &head:v.models)
                        if(head.source_node==layout.head_node)slots.insert(int(m.matrix_index)-head.matrix_index);
                }
            for(unsigned source=0;source<2;++source) {
                auto scene=original;std::array<uint16_t,2> frames{};
                frames[winner]=models[source].expression_frame(tracker.poses()[winner]);
                require(models[source].apply(scene,mask,0,frames)==1,"victory skeleton not replaced");
                for(size_t v=0;v<scene.viewports.size();++v)for(size_t i=0;i<scene.viewports[v].models.size();++i) {
                    const auto &a=original.viewports[v].models[i],&b=scene.viewports[v].models[i];
                    bool face=a.source_node==layout.face_node;
                    for(auto node:faces[winner])face|=a.source_node==node;
                    require(!face || b.meshes->empty(),"native victory face survived at frame "+std::to_string(f+1));
                    // No unselected opponent model may change.
                    const bool owned=std::find(layout.owned_nodes.begin(),layout.owned_nodes.end(),a.source_node)!=layout.owned_nodes.end();
                    require(owned || a.meshes==b.meshes,"unowned model changed");
                }
                if(capture)picture(scene,output/((source?"vf2-":"vf1-")+std::to_string(f+1)+".png"));
            }
            ++checked;
        }
        require(checked>30 && seen.size()==4,"replay did not exercise all four baked victory faces");
        printf("Pai outfit %u button %u: %u victory frames, %zu baked faces, slots",winner,button,checked,seen.size());
        for(auto slot:slots)printf(" %d",slot);
        printf(", animations");for(auto a:animations)printf(" %x",a);
        puts("; VF1/VF2 replacement and opponent preservation passed");
    } catch(const std::exception &e){fprintf(stderr,"pai-victory-check: %s\n",e.what());return 1;}
}
