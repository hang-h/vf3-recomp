// Local-ROM render regression for the shared VF1/VF2 roster on VF3.
// Writes screenshots only under the caller's output directory; no ROM fixtures.
#include "../../src/machine/model3.h"
#include "../../src/rec/ppc_rec.h"
#include "../../src/host/vf1_models.h"
#include "../../src/host/vf2_models.h"
#include "../../src/host/png.h"
#include "../../src/video/fighter_layouts.h"
#include "../../src/video/vf1_akira.h"
#include "../../src/video/r3d_raster.h"
#include "../../src/video/tilegen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
static m3::Model3 board;
static ppcrec::Rec recompiler;
static video::SceneBuilder builder;
static video::Raster raster;
static video::TileGen tiles;
void require(bool value,const std::string &message) {if(!value)throw std::runtime_error(message);}
uint64_t ram_hash()
{
    uint64_t result=1469598103934665603ull;
    for(auto value:board.ram) {result^=value;result*=1099511628211ull;}
    return result;
}
bool shadow(const video::Model &model)
{
    if(model.meshes->empty())return false;
    for(const auto &mesh:*model.meshes) {
        if(!mesh.poly_alpha || mesh.lighting || mesh.textured)return false;
        for(const auto &v:mesh.verts)if(v.colour[0]!=0 || v.colour[1]!=0 || v.colour[2]!=0)return false;
    }
    return true;
}
void picture(const video::Scene &scene,const std::filesystem::path &path)
{
    std::vector<uint32_t> below,above;tiles.aligned(below,above);
    for(auto &p:below)if(!(p>>24))p=0xff303030;
    raster.render(scene,board.tex_ram.data());raster.compose(below.data(),below.data());
    for(size_t i=0;i<below.size();++i)if(above[i]>>24)below[i]=above[i];
    require(write_png(path.string().c_str(),below.data(),video::W,video::H),"PNG write failed");
}
void attributes(const video::Scene &before,const video::Scene &after)
{
    require(before.viewports.size()==after.viewports.size(),"viewport count changed");
    for(size_t v=0;v<before.viewports.size();++v) {
        const auto &a=before.viewports[v],&b=after.viewports[v];
        require(a.models.size()==b.models.size(),"model count changed");
        require(a.matrix_table==b.matrix_table && a.root_matrix==b.root_matrix &&
                !std::memcmp(a.root.m,b.root.m,sizeof a.root.m),"viewport root changed");
        for(size_t i=0;i<a.models.size();++i) {
            const auto &x=a.models[i],&y=b.models[i];
            require(x.addr==y.addr && x.source_node==y.source_node && x.node_path==y.node_path &&
                    x.matrix_index==y.matrix_index && x.alpha==y.alpha && x.scale==y.scale && x.page==y.page &&
                    !std::memcmp(x.matrix.m,y.matrix.m,sizeof x.matrix.m),"model transform or draw attributes changed");
        }
    }
}
void untouched(const video::Scene &a,const video::Scene &b)
{
    attributes(a,b);
    for(size_t v=0;v<a.viewports.size();++v)for(size_t i=0;i<a.viewports[v].models.size();++i)
        require(a.viewports[v].models[i].meshes==b.viewports[v].models[i].meshes,"guard changed meshes");
}
void check(video::Vf1Akira &appearance,const video::Scene &original,unsigned fighter,unsigned frame,
           const char *source,const std::filesystem::path &output,std::ofstream &evidence)
{
    const bool preview=frame<3500;
    const size_t expected=frame==1750?1:2;
    const auto ram_before=ram_hash();
    auto disabled=original;appearance.enabled=false;
    require(appearance.apply(disabled,3,3)==0,"disabled source applied");untouched(original,disabled);
    appearance.enabled=true;
    if(preview) {
        auto no_preview=original;
        require(appearance.apply(no_preview,3,0)==0,"head-only preview lacks explicit mask guard");
        untouched(original,no_preview);
    }
    auto both=original,first=original,second=original;
    const auto count=appearance.apply(both,3,preview?3:0);
    picture(both,output/(std::string(source)+"-"+std::to_string(frame)+".png"));
    require(count==expected,std::string(source)+": expected "+std::to_string(expected)+
            " fighters at frame "+std::to_string(frame)+", got "+std::to_string(count));
    require(appearance.apply(first,1,preview?1:0)==1,"P1 outfit mask did not replace one fighter");
    require(appearance.apply(second,2,preview?2:0)==expected-1,"P2 outfit mask selected incorrect fighter count");
    attributes(original,both);attributes(original,first);attributes(original,second);
    size_t changed=0,shadows=0,faces=0;
    for(size_t v=0;v<original.viewports.size();++v)for(size_t i=0;i<original.viewports[v].models.size();++i) {
        const auto &a=original.viewports[v].models[i],&b=both.viewports[v].models[i];
        const bool all=a.meshes!=b.meshes;
        const bool p1=a.meshes!=first.viewports[v].models[i].meshes;
        const bool p2=a.meshes!=second.viewports[v].models[i].meshes;
        require(!(p1&&p2),"independent outfit masks modified the same model");
        require(all==(p1||p2),"combined swap disagrees with independent masks");
        changed+=all;
        if(shadow(a)) {require(!all,"native shadow changed");++shadows;}
        for(const auto &layout:fighter_layouts::fighters[fighter].outfits)
            if(a.source_node==layout.face_node) {require(b.meshes->empty(),"native face overlays source head");++faces;}
    }
    require(changed>expected,"source changed too few models");
    if(preview)require(faces>=expected,"preview omitted an animated face");
    if(!preview)require(shadows>0,"fight did not exercise native shadow preservation");
    auto unknown=original;
    for(auto &v:unknown.viewports)for(auto &m:v.models)m.source_node=0;
    const auto unknown_before=unknown;
    require(appearance.apply(unknown,3,3)==0,"unknown layout accepted");untouched(unknown_before,unknown);
    require(ram_hash()==ram_before,"appearance changed emulated RAM");
    evidence<<"{\"fighter\":"<<fighter<<",\"source\":\""<<source<<"\",\"frame\":"<<frame
        <<",\"replaced\":"<<count<<",\"changed_models\":"<<changed<<",\"preserved_shadows\":"<<shadows
        <<",\"removed_faces\":"<<faces<<",\"independent_outfits\":true,\"ram_unchanged\":true}\n";
    evidence.flush();
    printf("%s %s frame %u: %zu fighters, %zu changed models, %zu preserved shadows\n",
           fighter_layouts::fighters[fighter].name,source,frame,count,changed,shadows);fflush(stdout);
}
void run(unsigned fighter,const char *vf1,const char *vf2,const std::filesystem::path &root)
{
    video::Vf1Akira first,second;
    std::string error;
    vf1_models::Assets assets;
    if(fighter<8) {
        require(vf1_models::load(vf1,assets,error,fighter),error);
        require(first.load_assets(assets,video::AkiraSource::VF1,error,fighter),error);
    }
    require(vf2_models::load(vf2,assets,error,fighter),error);
    require(second.load_assets(assets,video::AkiraSource::VF2,error,fighter),error);
    assets={};
    const auto output=root/fighter_layouts::fighters[fighter].name;
    std::filesystem::create_directories(output);
    std::ofstream evidence(output/"evidence.jsonl");require(bool(evidence),"cannot open evidence file");
    board.reset();builder=video::SceneBuilder{};
    const unsigned row=fighter/6,col=fighter%6;
    auto press=[](unsigned f,unsigned at,unsigned length=4){return f>=at && f<at+length;};
    for(unsigned f=0;f<4120;++f) {
        board.controls={};auto &c=board.controls;
        c.coin[0]=press(f,1310)||press(f,1330)||press(f,1350)||press(f,1370);
        c.start[0]=press(f,1390);c.start[1]=press(f,2200,12);
        if(row) {c.down[0]=press(f,1530);c.down[1]=press(f,2650);}
        for(unsigned i=0;i<col;++i)c.right[0]|=press(f,1550+i*30);
        for(unsigned i=0;i<5-col;++i)c.left[1]|=press(f,2700+i*40);
        c.punch[0]=press(f,1800)||press(f,4080);c.punch[1]=press(f,3100);
        c.guard[0]=f>=3500&&f<4000;c.guard[1]=f>=3500;c.kick[0]=press(f,4000);
        board.run_frame();
        const auto frame=f+1;
        if(frame!=1750 && frame!=3080 && frame!=3820 && frame!=3920 && frame!=4120)continue;
        const unsigned expected_id=fighter_layouts::fighters[fighter].vf3_id;
        if(frame==1750)require(board.read8(0x108042)==expected_id,"P1 selected wrong fighter");
        else if(frame==3080)require(board.read8(0x1080c2)==expected_id+13,"P2 selected wrong fighter");
        else require(board.read8(0x1087f0)%13==expected_id && board.read8(0x10a7f0)%13==expected_id,
                     "script did not enter the requested mirror fight");
        video::Scene original;builder.build(board,original);
        picture(original,output/("vf3-"+std::to_string(frame)+".png"));
        if(fighter<8)check(first,original,fighter,frame,"vf1",output,evidence);
        check(second,original,fighter,frame,"vf2",output,evidence);
    }
}
}
int main(int argc,char **argv)
{
    if(argc!=5 && argc!=6) {
        fprintf(stderr,"usage: roster-check vf3.zip vf.zip vf2.zip output-directory [fighter 0..9]\n");return 2;
    }
    try {
        std::string error;require(board.load(argv[1],error),error);
        require(recompiler.build(board.cpu,board.program_image()),"native PowerPC recompilation failed");
        board.cpu.rec=&recompiler;raster.resize(video::W,video::H);
        board.on_line=[](unsigned y){if(y==0)tiles.clear();tiles.line(board,y);};
        const unsigned first=argc==6?std::stoul(argv[5]):0,last=argc==6?first+1:10;
        require(first<10,"invalid fighter index");
        for(unsigned fighter=first;fighter<last;++fighter)run(fighter,argv[2],argv[3],argv[4]);
        return 0;
    } catch(const std::exception &e) {fprintf(stderr,"roster-check: %s\n",e.what());return 1;}
}
