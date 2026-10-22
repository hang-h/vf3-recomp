// Optional local-ROM decoder checks. No ROM data is included in this test.
#include "../src/host/vf1_models.h"
#include "../src/host/rom_zip.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
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
    for(size_t part=0;part<a.parts.size();++part)if(!equal_mesh(a.parts[part],b.parts[part]))return false;
    for(size_t side=0;side<2;++side)for(size_t pose=0;pose<32;++pose)
        if(!equal_mesh(a.hands[side][pose],b.hands[side][pose]))return false;
    if(!equal_mesh(a.belt_knot,b.belt_knot))return false;
    for(size_t chain=0;chain<3;++chain) {
        const auto &x=a.strands[chain],&y=b.strands[chain];
        if(x.bone!=y.bone || x.anchor!=y.anchor || x.lengths!=y.lengths)return false;
        for(size_t segment=0;segment<3;++segment)if(!equal_mesh(x.segments[segment],y.segments[segment]))return false;
    }
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
    if (argc != 2) { std::cerr << "usage: vf1_models_test /path/to/vf.zip\n"; return 2; }
    try {
        vf1_models::Assets models;
        std::string error;
        require(vf1_models::load(argv[1], models, error), "decode: " + error);
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
        std::cout << models.parts.size() << " unscaled Akira parts, " << total
                  << " triangles, 64 hand poses, knot and 3 strands; missing, malformed, CRC damage, and transactional reload checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
