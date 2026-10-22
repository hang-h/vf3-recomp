// Local-ROM integration check: no ROM-derived fixture or geometry is shipped.
#include "../../src/machine/model3.h"
#include "../../src/host/input_script.h"
#include "../../src/host/png.h"
#include "../../src/video/r3d_scene.h"
#include "../../src/video/r3d_raster.h"
#include "../../src/video/tilegen.h"
#include "../../src/video/vf1_akira.h"
#include "../../src/video/vf2_akira.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace {
void require(bool ok, const char *what) { if (!ok) throw std::runtime_error(what); }
static m3::Model3 board;
static video::SceneBuilder builder;
static video::Raster raster;
static video::TileGen tiles;
bool same_models(const video::Viewport &a, const video::Viewport &b)
{
    if (a.models.size()!=b.models.size()) return false;
    for (size_t i=0;i<a.models.size();++i)
        if (a.models[i].meshes != b.models[i].meshes) return false;
    return true;
}
void picture(const video::Scene &scene, const std::filesystem::path &path)
{
    std::vector<uint32_t> below,above;
    tiles.aligned(below,above);
    for (auto &pixel:below) if (!(pixel>>24)) pixel=0xff303030;
    raster.render(scene,board.tex_ram.data());
    raster.compose(below.data(),below.data());
    for(size_t i=0;i<below.size();++i) if(above[i]>>24) below[i]=above[i];
    require(write_png(path.string().c_str(),below.data(),video::W,video::H),"could not save screenshot");
}
template<class Appearance>
void check(Appearance &appearance, unsigned frame, size_t expected, const std::filesystem::path &output, const char *appearance_name="vf1")
{
    video::Scene original;
    builder.build(board,original);
    video::Scene changed=original;
    appearance.enabled=false;
    require(appearance.apply(changed)==0,"disabled appearance changed a scene");
    for(size_t v=0;v<original.viewports.size();++v)
        require(same_models(original.viewports[v],changed.viewports[v]),"disabled appearance changed meshes");
    appearance.enabled=true;
    const size_t count=appearance.apply(changed);
    require(count==expected,"expected Akira skeleton count not recognised");
    size_t changed_viewports=0;
    for(size_t v=0;v<original.viewports.size();++v) {
        const auto &before=original.viewports[v];const auto &after=changed.viewports[v];
        require(before.models.size()==after.models.size(),"model count changed");
        bool has_akira=false;
        for(const auto &head_vp:original.viewports) for(const auto &h:head_vp.models)
            if(h.addr==0x9aaccf || h.addr==0xa47386)
                for(const auto &m:before.models)
                    if(before.matrix_table==head_vp.matrix_table && before.root_matrix==head_vp.root_matrix &&
                        int(m.matrix_index)>=int(h.matrix_index)-4 && int(m.matrix_index)<=int(h.matrix_index)+91)
                        has_akira=true;
        for(size_t m=0;m<before.models.size();++m) {
            const auto &a=before.models[m],&b=after.models[m];

            require(a.addr==b.addr && a.node_path==b.node_path && a.source_node==b.source_node &&
                a.matrix_index==b.matrix_index && a.alpha==b.alpha && a.scale==b.scale && a.page==b.page &&
                !std::memcmp(a.matrix.m,b.matrix.m,sizeof a.matrix.m),"appearance changed a transform or draw attribute");
        }
        if (!same_models(before,after)) {
            require(has_akira,"opponent or environment meshes changed");
            ++changed_viewports;
        }
    }
    require(changed_viewports>=count,"fewer changed viewports than fighters");
    // An unrecognised layout must remain entirely original, not half a fighter.
    auto unsupported=original;
    for(auto &vp:unsupported.viewports) for(auto &m:vp.models) m.source_node=0;
    const auto intact=unsupported;
    require(appearance.apply(unsupported)==0,"unknown layout accepted");
    for(size_t v=0;v<unsupported.viewports.size();++v)
        require(same_models(unsupported.viewports[v],intact.viewports[v]),"unknown layout partially replaced");
    picture(original,output/("original-"+std::to_string(frame)+".png"));
    picture(changed,output/(std::string(appearance_name)+"-"+std::to_string(frame)+".png"));
    printf("frame %u: %zu Akira; transforms, other viewports and disabled/unsupported paths unchanged\n",frame,count);
    fflush(stdout);
}
}
int main(int argc,char **argv)
{
    if(argc<5 || argc>8) { fprintf(stderr,"usage: akira-check vf3.zip vf.zip input.txt output-directory [first-frame=2100] [fighters=1] [vf1|vf2]\n"); return 2; }
    try {
        std::string error;
        if (!board.load(argv[1],error)) throw std::runtime_error(error);
        video::Vf1Akira appearance;
        video::Vf2Akira appearance2;
        const bool use_vf2=argc>7 && std::string(argv[7])=="vf2";
        if (argc>7 && !use_vf2 && std::string(argv[7])!="vf1") throw std::runtime_error("appearance must be vf1 or vf2");
        if (!(use_vf2 ? appearance2.load(argv[2],error) : appearance.load(argv[2],error))) throw std::runtime_error(error);
        InputScript inputs;
        if (!inputs.load(argv[3],error)) throw std::runtime_error(error);
        const unsigned first=argc>5 ? unsigned(std::stoul(argv[5])) : 2100;
        const size_t expected=argc>6 ? std::stoul(argv[6]) : 1;
        if(first>100000 || expected<1 || expected>2) throw std::runtime_error("invalid frame/fighter count");
        const std::filesystem::path output=argv[4];
        std::filesystem::create_directories(output);
        board.reset();raster.resize(video::W,video::H);
        board.on_line=[](unsigned y){if(y==0)tiles.clear();tiles.line(board,y);};
        for(unsigned f=0;f<first+600;++f) {
            inputs.step(board.controls,f);board.run_frame();
            if(f+1>=first && (f+1-first)%100==0){ if(use_vf2)check(appearance2,f+1,expected,output,"vf2");else check(appearance,f+1,expected,output); }
        }
    } catch(const std::exception &e) { fprintf(stderr,"%s\n",e.what());return 1; }
}
