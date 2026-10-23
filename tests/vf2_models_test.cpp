// Optional local-ROM decoder checks. No ROM data is included in this test.
#include "../src/host/vf2_models.h"
#include "../src/host/rom_zip.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}
bool equal_mesh(const vf1_models::Mesh &a,const vf1_models::Mesh &b)
{
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i].colour!=b[i].colour || a[i].points!=b[i].points)return false;
    return true;
}
bool equal(const vf1_models::Assets &a, const vf1_models::Assets &b)
{
    if(a.source_joints!=b.source_joints)return false;
    if(a.portrait.width!=b.portrait.width || a.portrait.height!=b.portrait.height ||
       a.portrait.pixels!=b.portrait.pixels)return false;
    for(size_t part=0;part<a.parts.size();++part)if(!equal_mesh(a.parts[part],b.parts[part]))return false;
    for(size_t side=0;side<2;++side)for(size_t pose=0;pose<32;++pose)
        if(!equal_mesh(a.hands[side][pose],b.hands[side][pose]))return false;
    if(!equal_mesh(a.belt_knot,b.belt_knot))return false;
    const auto same_strand=[](const auto &x,const auto &y) {
        if(x.bone!=y.bone || x.anchor!=y.anchor || x.lengths!=y.lengths ||
           x.rest_direction!=y.rest_direction)return false;
        if(x.segments.size()!=y.segments.size())return false;
        for(size_t segment=0;segment<x.segments.size();++segment)if(!equal_mesh(x.segments[segment],y.segments[segment]))return false;
        return true;
    };
    for(size_t chain=0;chain<3;++chain)if(!same_strand(a.strands[chain],b.strands[chain]))return false;
    if(a.extra_head_ribbons.size()!=b.extra_head_ribbons.size())return false;
    for(size_t i=0;i<a.extra_head_ribbons.size();++i)if(!same_strand(a.extra_head_ribbons[i],b.extra_head_ribbons[i]))return false;
    if(a.extra_strands.size()!=b.extra_strands.size())return false;
    for(size_t i=0;i<a.extra_strands.size();++i)if(!same_strand(a.extra_strands[i],b.extra_strands[i]))return false;
    return true;
}
struct temp_folder {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("vf3-vf2-models-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    temp_folder() { std::filesystem::create_directory(path); }
    ~temp_folder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}

int main(int argc, char **argv)
{
    if (argc != 2) { std::cerr << "usage: vf2_models_test /path/to/vf2.zip\n"; return 2; }
    try {
        vf1_models::Assets models;
        std::string error;
        require(vf2_models::load(argv[1], models, error), "decode: " + error);
        require(models.portrait.available() && models.portrait.width==48 && models.portrait.height==88,
                "missing original VF2 select portrait");
        bool coloured=false;
        for(uint32_t p:models.portrait.pixels) {
            require((p>>24)==255,"nonopaque VF2 portrait pixel");
            if(((p>>16)&255)!=(p&255))coloured=true;
        }
        require(coloured,"VF2 portrait lost its palette");
        size_t total = 0;
        std::set<uint32_t> colours;
        for (const auto &part : models.parts) {
            require(!part.empty(), "missing Akira part");
            total += part.size();
            for (const auto &triangle : part) {
                colours.insert(triangle.colour);
                require((triangle.colour >> 24) == 255, "non-opaque source colour");
                for (const auto &point : triangle.points) for (float coordinate : point)
                    require(std::isfinite(coordinate), "non-finite source vertex");
            }
        }
        for(size_t side=0;side<2;++side) {
            for(const auto &hand:models.hands[side])require(!hand.empty(),"incomplete hand pose");
            require(!equal_mesh(models.hands[side][0],models.hands[side][31]),"hand poses collapsed");
            float fist=0,open=0;
            for(const auto &t:models.hands[side][0])for(const auto &p:t.points)fist=std::max(fist,p[0]);
            for(const auto &t:models.hands[side][31])for(const auto &p:t.points)open=std::max(open,p[0]);
            require(open>fist+.03f,"hand poses not ordered closed to open");
        }
        // VF2's limb units must survive decoding: the renderer fits these at 2048x.
        const auto &arm=models.parts[size_t(vf1_models::Part::UpperArmA)];
        float xmax=-1;
        for(const auto &t:arm)for(const auto &p:t.points)xmax=std::max(xmax,p[0]);
        require(std::abs(xmax-.3003f)<1e-5f,"VF2 arm was scaled or wrong descriptor used");
        require(colours.size()>30,"native textures or palette curves were omitted");
        require(colours.count(0xffa57e4c),"native skin colour curve was lost");
        require(colours.count(0xff152944),"native blue gi colour curve was lost");
        require(models.strands[0].segments.size()==5 && models.extra_head_ribbons.size()==1 &&
                models.extra_head_ribbons[0].segments.size()==5,"missing native headband chains");
        require(models.strands[1].segments.size()==2 && models.strands[2].segments.size()==2 &&
                models.strands[1].bone==1 && models.strands[2].bone==1,"missing native belt tails");
        for(const auto &s:models.strands)for(size_t i=0;i<s.segments.size();++i) {
            require(!s.segments[i].empty(),"empty native accessory segment");
            require(std::abs(s.lengths[i]-(s.bone==2?.08f:.095f))<1e-5f,"incorrect native accessory length");
        }
        const auto reference = models;
        constexpr const char *names[]={"Akira","Pai","Lau","Wolf","Jeffry","Kage","Sarah","Jacky","Shun","Lion"};
        constexpr unsigned head_chains[]={2,2,1,1,6,2,3,15,2,3};
        constexpr unsigned body_chains[]={2,0,0,0,0,2,0,0,1,4};
        std::set<std::vector<uint32_t>> portraits;
        for(unsigned fighter=0;fighter<10;++fighter) {
            vf1_models::Assets roster;
            require(vf2_models::load(argv[1],roster,error,fighter),std::string(names[fighter])+": "+error);
            require(roster.portrait.available() && roster.portrait.width==48 && roster.portrait.height==88,
                    std::string(names[fighter])+": missing original select portrait");
            require(portraits.insert(roster.portrait.pixels).second,"fighter reused another portrait");
            for(const auto &part:roster.parts) {
                require(!part.empty(),std::string(names[fighter])+": missing body part");
                for(const auto &t:part) {
                    require((t.colour>>24)==255,"invalid fighter colour");
                    for(const auto &p:t.points)for(float v:p)require(std::isfinite(v),"invalid fighter geometry");
                }
            }
            for(const auto &side:roster.hands) {
                for(const auto &pose:side)require(!pose.empty(),"missing native hand pose");
                require(!equal_mesh(side[0],side[31]),"fighter hand poses collapsed");
            }
            require(roster.source_joints[0]==std::array<float,3>{},"world root baked into source skeleton");
            for(unsigned slot:{1,3,4,6,7,10,11,13,14})
                require(roster.source_joints[slot][0]>.1f && roster.source_joints[slot][0]<.6f,
                        "native skeletal proportions missing");
            require(roster.source_joints[2][2]<0 && roster.source_joints[5][2]>0,
                    "shoulder offsets use incorrect shared joint indexing");
            require(roster.source_joints[10][0]>roster.source_joints[9][0] &&
                    roster.source_joints[13][0]>roster.source_joints[12][0],
                    "leg fitting reads hip offset instead of thigh length");
            unsigned heads=0,bodies=0;
            unsigned white_beard_triangles=0;
            auto chain=[&](const vf1_models::Strand &s) {
                if(s.segments.empty())return;
                require(s.bone==1 || s.bone==2 || s.bone==9,"invalid native accessory attachment");
                require(s.rest_direction==std::array<float,3>{0,0,fighter==7?1.f:-1.f},
                        "upright hair was treated as a hanging braid");
                (s.bone==2?heads:bodies)++;
                require(s.segments.size()==s.lengths.size(),"accessory segment/length mismatch");
                for(size_t i=0;i<s.segments.size();++i) {
                    require(!s.segments[i].empty(),"empty native accessory segment");
                    require(s.lengths[i]>0 && s.lengths[i]<.5f,"invalid native accessory length");
                    if(fighter==8 && s.bone==2)for(const auto &t:s.segments[i]) {
                        const unsigned r=(t.colour>>16)&255,g=(t.colour>>8)&255,b=t.colour&255;
                        require(!(r>g+50 && r>b+50),"Shun beard reused Akira's red mouth colour curve");
                        white_beard_triangles+=r>150 && g>150 && b>150;
                    }
                }
            };
            for(const auto &s:roster.strands)chain(s);
            for(const auto &s:roster.extra_head_ribbons)chain(s);
            for(const auto &s:roster.extra_strands)chain(s);
            require(heads==head_chains[fighter] && bodies==body_chains[fighter],
                    std::string(names[fighter])+": native accessories omitted or duplicated");
            if(fighter==8)require(white_beard_triangles>0,"Shun's source white beard is missing");
            if(fighter)require(!equal_mesh(roster.parts.back(),reference.parts.back()),"fighter reused Akira's head");
            else require(equal(roster,reference),"explicit Akira differs from default load");
        }
        require(!vf2_models::load(argv[1],models,error,10) && !error.empty() && equal(models,reference),
                "unsupported fighter changed destination");
        temp_folder temp;
        require(!vf2_models::load((temp.path / "missing.zip").string(), models, error), "missing ZIP accepted");
        require(!error.empty() && equal(models, reference), "missing ZIP changed destination");
        const auto invalid = temp.path / "invalid.zip";
        { std::ofstream stream(invalid); stream << "not a ZIP"; }
        require(!vf2_models::load(invalid.string(), models, error), "invalid ZIP accepted");
        require(!error.empty() && equal(models, reference), "invalid ZIP changed destination");

        rom_zip::zip zip;
        require(zip.open(argv[1]), "cannot open reference ZIP");
        bool altered = false;
        for (const auto &entry : zip.entries) if (entry.crc == 0xd1389864 && entry.usize == 0x200000) {
            const size_t start = size_t(entry.local) + 30 +
                rom_zip::le16(&zip.data[entry.local + 26]) + rom_zip::le16(&zip.data[entry.local + 28]);
            require(entry.csize > 0 && start + entry.csize <= zip.data.size(), "invalid test payload");
            zip.data[start + entry.csize / 2] ^= 0x80;
            altered = true;
            break;
        }
        require(altered, "record ROM missing in test ZIP");
        const auto damaged = temp.path / "damaged.zip";
        { std::ofstream stream(damaged, std::ios::binary);
          stream.write(reinterpret_cast<const char *>(zip.data.data()), std::streamsize(zip.data.size())); }
        require(!vf2_models::load(damaged.string(), models, error), "corrupted payload accepted");
        require(!error.empty() && equal(models, reference), "corrupted ZIP changed destination");
        require(vf2_models::load(argv[1], models, error) && error.empty() && equal(models, reference),
                "repeat load differs or retains error");
        std::cout << models.parts.size() << " unscaled Akira parts, " << total
                  << " triangles preserved; all 10 VF2 fighters, 640 native hand poses, portraits, skeletons and accessories; "
                  << "missing, malformed, CRC damage, invalid fighter and transactional reload checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
