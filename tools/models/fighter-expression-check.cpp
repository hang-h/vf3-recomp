// Native-game expression integration check. Source ROM assets remain local.
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
#include <set>
#include "../../src/host/akira_selection.h"

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
uint64_t head_hash(const video::Meshes &meshes)
{
    uint64_t result=1469598103934665603ull;
    auto add=[&](uint8_t byte){result=(result^byte)*1099511628211ull;};
    for(const auto &mesh:*meshes) {
        if(mesh.model2_texture)for(auto value:mesh.model2_texture->words)
            for(unsigned i=0;i<4;++i)add(uint8_t(value>>(8*i)));
        for(const auto &v:mesh.verts) {
        for(float x:v.pos) {uint32_t bits;std::memcpy(&bits,&x,4);for(unsigned i=0;i<4;++i)add(uint8_t(bits>>(8*i)));}
        for(auto c:v.colour)add(c);
        }
    }
    return result;
}
struct Statistics {
    unsigned checks=0,removed_faces=0;
    std::array<std::set<uint64_t>,2> heads,speech_heads;
    std::array<unsigned,2> expression_changes{},speech_changes{};
    std::set<unsigned> captures;
};
void check(video::Vf1Akira &appearance,const video::Scene &original,unsigned fighter,unsigned frame,
           unsigned previews,const fighter_expression::Poses &poses,const char *source,
           const std::filesystem::path &output,std::ofstream &evidence,Statistics &stats)
{
    std::array<uint16_t,2> frames{};
    for(const auto &pose:poses)if(pose.valid && fighter_roster::from_actor(pose.actor)==fighter)
        frames[pose.actor>=13]=appearance.expression_frame(pose);
    appearance.enabled=true;
    auto both=original,first=original,second=original,neutral=original;
    const auto count=appearance.apply(both,3,previews,frames);
    require(appearance.apply(neutral,3,previews,{})==count,"neutral/expression skeleton counts differ");
    const auto p1count=appearance.apply(first,1,previews&1,frames);
    const auto p2count=appearance.apply(second,2,previews&2,frames);
    require(count==p1count+p2count,"combined expression count differs from independent outfits");
    attributes(original,both);attributes(original,first);attributes(original,second);
    size_t heads=0,faces=0;
    for(size_t v=0;v<original.viewports.size();++v)for(size_t i=0;i<original.viewports[v].models.size();++i) {
        const auto &a=original.viewports[v].models[i],&b=both.viewports[v].models[i];
        const bool all=a.meshes!=b.meshes,p1=a.meshes!=first.viewports[v].models[i].meshes,
                   p2=a.meshes!=second.viewports[v].models[i].meshes;
        require(!(p1&&p2),"players modified the same native model");
        require(all==(p1||p2),"combined expression disagrees with independent masks");
        if(shadow(a))require(!all,"native shadow changed");
        for(unsigned outfit=0;outfit<2;++outfit) {
            const auto &layout=fighter_layouts::fighters[fighter].outfits[outfit];
            if(a.source_node==layout.head_node) {
                ++heads;require(all,"visible native head was not replaced");
                const auto hash=head_hash(b.meshes);stats.heads[outfit].insert(hash);
                const bool expression_changed=hash!=head_hash(neutral.viewports[v].models[i].meshes);
                stats.expression_changes[outfit]+=expression_changed;
                for(const auto &pose:poses)if(pose.speaking && (pose.actor>=13)==bool(outfit) &&
                    fighter_roster::from_actor(pose.actor)==fighter){stats.speech_heads[outfit].insert(hash);stats.speech_changes[outfit]+=expression_changed;}
            }
            if(a.source_node==layout.face_node && (outfit?p2count:p1count)) {
                require(b.meshes->empty(),std::string(source)+" original VF3 face overlaps imported expression at frame "+std::to_string(frame)+" outfit "+std::to_string(outfit)+" counts "+std::to_string(p1count)+","+std::to_string(p2count));++faces;
            }
        }
    }
    require(heads==count,"visible fighter skeleton not recognized");
    if(!heads){if(frame==5840)picture(original,output/(std::string(source)+"-native-no-head-5840.png"));return;}
    ++stats.checks;stats.removed_faces+=unsigned(faces);
    unsigned capture=0;
    if(frame==1750)capture=1;
    else if(frame==3080)capture=2;
    else for(const auto &pose:poses) {
        if(pose.speaking&&pose.mouth_open>.65f)capture=5;
        else if(!capture&&pose.kind==fighter_expression::Kind::Victory&&pose.tick>=8)capture=4;
        else if(!capture&&pose.kind==fighter_expression::Kind::Hurt&&pose.tick>=6)capture=3;
    }
    if(capture && stats.captures.insert(capture).second)
        picture(both,output/(std::string(source)+"-state"+std::to_string(capture)+"-f"+std::to_string(frame)+".png"));
    evidence<<"{\"frame\":"<<frame<<",\"source\":\""<<source<<"\",\"replaced\":"<<count
        <<",\"head_frames\":["<<frames[0]<<","<<frames[1]<<"],\"kinds\":["<<unsigned(poses[0].kind)<<","<<unsigned(poses[1].kind)
        <<"],\"speech\":["<<poses[0].speaking<<","<<poses[1].speaking<<"],\"mouth\":["<<poses[0].mouth_open<<","<<poses[1].mouth_open
        <<"],\"faces_removed\":"<<faces<<",\"independent_players\":true,\"ram_unchanged\":true}\n";
}
void run(unsigned fighter,const char *vf1,const char *vf2,const std::filesystem::path &root,unsigned limit)
{
    video::Vf1Akira models[2];vf1_models::Assets assets;std::string error;
    if(fighter<8) {
        require(vf1_models::load(vf1,assets,error,fighter),error);
        require(models[0].load_assets(assets,video::AkiraSource::VF1,error,fighter),error);
    }
    require(vf2_models::load(vf2,assets,error,fighter),error);
    require(models[1].load_assets(assets,video::AkiraSource::VF2,error,fighter),error);assets={};
    Statistics stats[2];
    const auto output=root/fighter_roster::names[fighter];std::filesystem::create_directories(output);
    std::ofstream evidence(output/"evidence.jsonl");require(bool(evidence),"cannot open evidence");
    board.reset();builder=video::SceneBuilder{};fighter_expression::Tracker tracker;
    const unsigned row=fighter/6,col=fighter%6;
    auto press=[](unsigned f,unsigned at,unsigned length=4){return f>=at&&f<at+length;};
    std::array<std::set<unsigned>,2> kinds;
    unsigned speech=0;bool victory=false,defeat=false,selection=false,hurt=false;
    for(unsigned f=0;f<limit;++f) {
        board.controls={};auto &c=board.controls;
        c.coin[0]=press(f,1310)||press(f,1330)||press(f,1350)||press(f,1370);
        c.start[0]=press(f,1390);c.start[1]=press(f,2200,12);
        if(row){c.down[0]=press(f,1530);c.down[1]=press(f,2650);}
        for(unsigned i=0;i<col;++i)c.right[0]|=press(f,1550+i*30);
        for(unsigned i=0;i<5-col;++i)c.left[1]|=press(f,2700+i*40);
        c.punch[0]=press(f,1800);c.punch[1]=press(f,3100);
        if(f>=3500) {
            const unsigned r=(f/7)*1664525u+1013904223u;
            c.punch[0]=r&1;c.kick[0]=r&2;c.guard[0]=r&4;c.down[0]=r&8;
            c.up[0]=r&16;c.left[0]=r&32;c.right[0]=r&64;c.guard[1]=true;
        }
        board.run_frame();const unsigned frame=f+1;
        const auto sample=fighter_expression::snapshot(board.ram);tracker.update(sample);
        const auto &poses=tracker.poses();
        for(unsigned p=0;p<2;++p)if(poses[p].valid&&fighter_roster::from_actor(poses[p].actor)==fighter) {
            const auto kind=poses[p].kind;kinds[p].insert(unsigned(kind));
            selection|=kind==fighter_expression::Kind::Selection;
            hurt|=kind==fighter_expression::Kind::Hurt;victory|=sample[p].victory;defeat|=sample[p].defeat;
            speech+=sample[p].native_face_valid;
        }
        if(frame!=1750&&frame!=3080&&(frame<3500||frame%20))continue;
        if(frame==1750)require(board.read8(0x108042)==fighter_roster::game_ids[fighter],"wrong P1 selection");
        if(frame==3080)require(board.read8(0x1080c2)==fighter_roster::game_ids[fighter]+13,"wrong P2 selection");
        const auto before=ram_hash();video::Scene scene;builder.build(board,scene);
        const auto select=AkiraSelection::snapshot(board.ram);
        const unsigned previews=AkiraSelection::previews(select,3,fighter);
        for(unsigned source=fighter<8?0:1;source<2;++source)
            check(models[source],scene,fighter,frame,previews,poses,source?"vf2":"vf1",output,evidence,stats[source]);
        require(before==ram_hash(),"expression code changed emulated RAM");
        if(frame%1000==0){printf("%s frame%u speech%u result%u/%u\n",fighter_roster::names[fighter],frame,speech,victory,defeat);fflush(stdout);}
    }
    require(selection&&victory&&defeat,"native selection/result states not exercised");
    // Native 43b28..43b50 skips speech morphs for Kage while the mask remains on.
    if(fighter!=5)require(speech>0,"native speech controller not exercised");
    for(unsigned source=fighter<8?0:1;source<2;++source) {
        for(unsigned p=0;p<2;++p) {
            if(fighter==5 && source==0)
                require(stats[source].expression_changes[p]==0,"masked VF1 Kage exposed an animated face");
            else require(stats[source].expression_changes[p]>0,std::string(source?"VF2":"VF1")+" native states did not change source face relative to neutral in the same pose, outfit "+std::to_string(p));
        }
        if(fighter!=5)require(stats[source].speech_changes[0]>0||stats[source].speech_changes[1]>0,"native speech did not change source face relative to neutral in the same pose");
        printf("PASS %s %s: %u scenes, variants %zu/%zu, speech variants %zu/%zu, hurt=%u; same-pose expression changes %u/%u, speech changes %u/%u; RAM and isolation passed\n",
            fighter_roster::names[fighter],source?"VF2":"VF1",stats[source].checks,stats[source].heads[0].size(),stats[source].heads[1].size(),
            stats[source].speech_heads[0].size(),stats[source].speech_heads[1].size(),hurt,stats[source].expression_changes[0],stats[source].expression_changes[1],stats[source].speech_changes[0],stats[source].speech_changes[1]);
    }
}
}
int main(int argc,char **argv)
{
    if(argc<5||argc>7){fprintf(stderr,"usage: fighter-expression-check vf3.zip vf.zip vf2.zip output-dir [fighter 0..9|all] [frames=6500]\n");return 2;}
    try {
        std::string error;require(board.load(argv[1],error),error);
        require(recompiler.build(board.cpu,board.program_image()),"PPC recompilation failed");board.cpu.rec=&recompiler;
        raster.resize(video::W,video::H);board.on_line=[](unsigned y){if(y==0)tiles.clear();tiles.line(board,y);};
        const bool all=argc<6||std::string(argv[5])=="all";
        const unsigned first=all?0:unsigned(std::stoul(argv[5])),last=all?10:first+1;
        const unsigned frames=argc<7?6500:unsigned(std::stoul(argv[6]));
        require(first<10&&frames>=6000&&frames<=30000,"invalid fighter/frame limit");
        for(unsigned f=first;f<last;++f)run(f,argv[2],argv[3],argv[4],frames);
    } catch(const std::exception &e){fprintf(stderr,"fighter-expression-check: %s\n",e.what());return 1;}
}
