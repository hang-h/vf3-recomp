// Runtime regression for culled victory-pose limbs and head-only selection previews.
// Takes local ROMs; no ROM-derived fixtures are included.
#include "../../src/host/akira_selection.h"
#include "../../src/host/input_script.h"
#include "../../src/host/png.h"
#include "../../src/video/vf1_akira.h"
#include "../../src/host/vf2_models.h"
#include "../../src/video/r3d_raster.h"
#include "../../src/video/tilegen.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
static m3::Model3 board;
static video::SceneBuilder builder;
static video::Raster raster;
static video::TileGen tiles;
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static void picture(const video::Scene &scene,const std::filesystem::path &path)
{
    std::vector<uint32_t> below,above;tiles.aligned(below,above);
    for(auto &p:below)if(!(p>>24))p=0xff303030;
    raster.render(scene,board.tex_ram.data());raster.compose(below.data(),below.data());
    for(size_t i=0;i<below.size();++i)if(above[i]>>24)below[i]=above[i];
    require(write_png(path.string().c_str(),below.data(),video::W,video::H),"PNG write failed");
}
int main(int argc,char **argv)
{
    if(argc!=9 && argc!=10){fprintf(stderr,"usage: akira-layout-check vf3.zip vf.zip script output first last outfit-mask preview(0|1) [vf1|vf2]\n");return 2;}
    try {
        std::string error;require(board.load(argv[1],error),error.c_str());
        video::Vf1Akira appearance;
        const bool vf2=argc==10 && std::string(argv[9])=="vf2";
        if(vf2) {
            vf1_models::Assets assets;require(vf2_models::load(argv[2],assets,error),error.c_str());
            require(appearance.load_assets(assets,video::AkiraSource::VF2,error),error.c_str());
        } else require(appearance.load(argv[2],error),error.c_str());
        appearance.enabled=true;
        InputScript script;require(script.load(argv[3],error),error.c_str());
        const unsigned first=std::stoul(argv[5]),last=std::stoul(argv[6]),mask=std::stoul(argv[7]);
        const bool preview=std::stoul(argv[8])!=0;
        require(first<=last && last<=100000 && mask && mask<=3,"invalid frame/mask");
        const std::filesystem::path output=argv[4];std::filesystem::create_directories(output);
        board.reset();raster.resize(video::W,video::H);board.on_line=[](unsigned y){if(y==0)tiles.clear();tiles.line(board,y);};
        AkiraSelection choice;m3::Controls raw;
        for(unsigned f=0;f<last;++f){
            script.step(raw,f);board.controls=raw;choice.update(board.controls,AkiraSelection::snapshot(board.ram),true);board.run_frame();
            if(f+1<first || (f+1-first)%20)continue;
            video::Scene original;builder.build(board,original);auto changed=original;
            if(preview)require(appearance.apply(changed,0)==0,"preview applied without explicit host mask");
            require(appearance.apply(changed,preview?0:mask,preview?mask:0)>0,"visible Akira was not replaced");
            for(size_t v=0;v<original.viewports.size();++v){
                require(original.viewports[v].models.size()==changed.viewports[v].models.size(),"model count changed");
                for(size_t i=0;i<original.viewports[v].models.size();++i){
                    const auto &a=original.viewports[v].models[i],&b=changed.viewports[v].models[i];
                    require(a.addr==b.addr && a.source_node==b.source_node && a.node_path==b.node_path && a.matrix_index==b.matrix_index && a.alpha==b.alpha && a.scale==b.scale && a.page==b.page && !std::memcmp(a.matrix.m,b.matrix.m,sizeof a.matrix.m),"draw attributes changed");
                }
            }
            // A replaced skull alone is insufficient: the independently animated
            // VF3 face must also disappear, while an unselected opponent stays intact.
            unsigned faces=0;
            for(size_t v=0;v<original.viewports.size();++v)for(size_t m=0;m<original.viewports[v].models.size();++m) {
                const auto &a=original.viewports[v].models[m],&b=changed.viewports[v].models[m];
                const unsigned owner=a.source_node==0x50990 ? 1 : a.source_node==0x51e48 ? 2 : 0;
                if(owner & mask) {require(b.meshes->empty(),"original animated face overlays replacement");++faces;}
                else if(owner) require(a.meshes==b.meshes,"unselected opponent face changed");
                if(a.source_node==0x4c680 || a.source_node==0x6edd0)
                    require(a.meshes==b.meshes,"fight head shadow changed");
            }
            require(faces>0,"no animated face checked");
            auto unsupported=original;for(auto &v:unsupported.viewports)for(auto &m:v.models)m.source_node=0;
            require(appearance.apply(unsupported,mask,preview?mask:0)==0,"unknown layout accepted");
            picture(original,output/(std::to_string(f+1)+"-original.png"));picture(changed,output/(std::to_string(f+1)+(vf2?"-vf2.png":"-vf1.png")));
            printf("frame %u: %s replaced; transforms and unknown-layout guard preserved\n",f+1,preview?"selection preview":"visible fighter");fflush(stdout);
        }
    }catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}
}
