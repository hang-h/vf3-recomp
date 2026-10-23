// Optional local-ROM decoder checks. No ROM data is included in this test.
#include "../src/host/vf1_models.h"
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
        if(x.bone!=y.bone || x.anchor!=y.anchor || x.lengths!=y.lengths || x.segments.size()!=y.segments.size())return false;
        for(size_t segment=0;segment<x.segments.size();++segment)
            if(!equal_mesh(x.segments[segment],y.segments[segment]))return false;
        return true;
    };
    for(size_t chain=0;chain<3;++chain)if(!same_strand(a.strands[chain],b.strands[chain]))return false;
    if(a.extra_head_ribbons.size()!=b.extra_head_ribbons.size())return false;
    for(size_t chain=0;chain<a.extra_head_ribbons.size();++chain)
        if(!same_strand(a.extra_head_ribbons[chain],b.extra_head_ribbons[chain]))return false;
    return true;
}
struct temp_folder {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("vf3-vf1-models-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    temp_folder() { std::filesystem::create_directory(path); }
    ~temp_folder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) { std::cerr << "usage: vf1_models_test /path/to/vf.zip [fighter 0..7]\n"; return 2; }
    try {
        vf1_models::Assets models;
        std::string error;
        require(vf1_models::load(argv[1], models, error), "decode: " + error);
        require(models.portrait.available() && models.portrait.width==56 && models.portrait.height==64,
                "missing original VF1 select portrait");
        bool coloured=false;
        for(uint32_t p:models.portrait.pixels) {
            require((p>>24)==255,"nonopaque VF1 portrait pixel");
            if(((p>>16)&255)!=(p&255))coloured=true;
        }
        require(coloured,"VF1 portrait lost its palette");
        size_t total = 0;
        for (const auto &part : models.parts) {
            require(!part.empty(), "missing Akira part");
            total += part.size();
            for (const auto &triangle : part) {
                require((triangle.colour >> 24) == 255, "non-opaque source colour");
                for (const auto &point : triangle.points) for (float coordinate : point)
                    require(std::isfinite(coordinate), "non-finite source vertex");
            }
        }
        for(size_t side=0;side<2;++side) {
            for(const auto &hand:models.hands[side])require(hand.size()==270,"incomplete hand pose");
            require(!equal_mesh(models.hands[side][0],models.hands[side][31]),"hand poses collapsed");
        }
        require(models.belt_knot.size()==12,"belt knot missing");
        require(models.strands[0].bone==2 && models.strands[1].bone==9 && models.strands[2].bone==9,
                "incorrect accessory attachment bones");
        for(size_t chain=0;chain<3;++chain)for(size_t segment=0;segment<3;++segment) {
            const auto &strand=models.strands[chain];
            require(strand.segments[segment].size()==(chain && segment==1 ? 88 : 4),"incomplete strand segment");
            require(std::abs(strand.lengths[segment]-.1f)<1e-6f,"incorrect strand length");
        }
        const auto reference = models;
        temp_folder temp;
        require(!vf1_models::load((temp.path / "missing.zip").string(), models, error), "missing ZIP accepted");
        require(!error.empty() && equal(models, reference), "missing ZIP changed destination");
        const auto invalid = temp.path / "invalid.zip";
        { std::ofstream stream(invalid); stream << "not a ZIP"; }
        require(!vf1_models::load(invalid.string(), models, error), "invalid ZIP accepted");
        require(!error.empty() && equal(models, reference), "invalid ZIP changed destination");

        rom_zip::zip zip;
        require(zip.open(argv[1]), "cannot open reference ZIP");
        bool altered = false;
        for (const auto &entry : zip.entries) if (entry.crc == 0x3662e1a5 && entry.usize == 0x20000) {
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
        require(!vf1_models::load(damaged.string(), models, error), "corrupted payload accepted");
        require(!error.empty() && equal(models, reference), "corrupted ZIP changed destination");
        require(vf1_models::load(argv[1], models, error) && error.empty() && equal(models, reference),
                "repeat load differs or retains error");
        for(unsigned invalid_fighter : {8u,9u,255u}) {
            require(!vf1_models::load(argv[1],models,error,invalid_fighter),"unsupported VF1 fighter accepted");
            require(!error.empty() && equal(models,reference),"invalid fighter changed destination");
        }
        const unsigned first=argc==3 ? std::stoul(argv[2]) : 0;
        const unsigned last=argc==3 ? first+1 : 8;
        require(first<8,"requested test fighter is outside VF1 roster");
        std::set<std::vector<uint32_t>> portraits;
        constexpr unsigned head_chains[]={1,2,1,1,3,2,1,0};
        constexpr unsigned head_segments[]={3,5,8,3,1,5,6,0};
        for(unsigned fighter=first;fighter<last;++fighter) {
            require(vf1_models::load(argv[1],models,error,fighter),"fighter "+std::to_string(fighter)+": "+error);
            for(const auto &part:models.parts) {
                require(!part.empty(),"fighter has empty body part");
                for(const auto &triangle:part) {
                    require(triangle.colour>>24==255,"fighter has nonopaque colour");
                    for(const auto &point:triangle.points)for(float axis:point)
                        require(std::isfinite(axis),"fighter has invalid vertex");
                }
            }
            for(const auto &side:models.hands) {
                for(const auto &pose:side)require(!pose.empty(),"missing fighter hand pose");
                require(!equal_mesh(side[0],side[31]),"fighter hand poses collapsed");
            }
            for(const auto &joint:models.source_joints)for(float axis:joint)
                require(std::isfinite(axis) && std::abs(axis)<1,"invalid source skeleton");
            require(models.source_joints[1][0]>.3f && models.source_joints[1][0]<.4f,"source neck length missing");
            require(models.portrait.available() && models.portrait.width==56 && models.portrait.height==64,
                    "fighter select portrait missing");
            require(portraits.insert(models.portrait.pixels).second,"fighters share substitute portrait");
            if(head_chains[fighter]) {
                require(models.strands[0].bone==2 && models.strands[0].segments.size()==head_segments[fighter],
                        "wrong native head accessory");
                require(models.extra_head_ribbons.size()==head_chains[fighter]-1,"missing extra head chain");
                std::vector<vf1_models::Strand> chains{models.strands[0]};
                chains.insert(chains.end(),models.extra_head_ribbons.begin(),models.extra_head_ribbons.end());
                for(const auto &chain:chains) {
                    require(chain.bone==2 && chain.segments.size()==chain.lengths.size(),"bad head chain attachment");
                    for(size_t i=0;i<chain.segments.size();++i)
                        require(!chain.segments[i].empty() && chain.lengths[i]>0 && chain.lengths[i]<1,"bad head segment");
                }
            } else {
                for(const auto &segment:models.strands[0].segments)require(segment.empty(),"unexpected head accessory");
                require(models.extra_head_ribbons.empty(),"unexpected extra head accessory");
            }
            for(unsigned chain=1;chain<3;++chain) {
                const bool belt=fighter==0 || fighter==5;
                for(const auto &segment:models.strands[chain].segments) {
                    require(segment.empty()!=belt,"unexpected/missing native belt");
                    if(fighter==5)for(const auto &triangle:segment)for(const auto &point:triangle.points)
                        require(std::abs(point[1])<.12f,"Kage belt omitted native 0.1 scale");
                }
            }
            const auto saved=models;
            require(!vf1_models::load(damaged.string(),models,error,fighter) && equal(models,saved),
                    "failed roster load changed destination");
            size_t count=0;for(const auto &part:models.parts)count+=part.size();
            std::cout<<"VF1 fighter "<<fighter<<": "<<count<<" body triangles, 64 hand poses, "
                     <<head_chains[fighter]<<" head chains, native portrait and skeleton verified.\n";
        }
        std::cout << models.parts.size() << " unscaled Akira parts, " << total
                  << " triangles, 64 hand poses, knot and 3 strands; missing, malformed, CRC damage, and transactional reload checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
