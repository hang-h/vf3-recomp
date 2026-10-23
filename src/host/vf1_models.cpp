// Adapted from vf2-recomp: Copyright (c) 2026 mr-finster. MIT; see LICENSE.
#include "vf1_models.h"
#include "rom_zip.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vf1_models {
namespace {
using point = std::array<float, 3>;
using mesh = Mesh;

// MAME vf's ROM_LOAD, ROM_LOAD16_BYTE and ROM_LOAD32_WORD lanes. Only the
// character records/descriptors, colour tables and polygons are needed.
enum { RECORDS, DESCRIPTORS, COLOURS, POLYGONS, GRAPHICS, REGION_COUNT };
struct rom { const char *name; uint32_t crc; int region; size_t offset, size, unit, stride; };
constexpr rom ROMS[] = {
    {"epr-16080.4",  0x3662e1a5, RECORDS, 0, 0x20000, 1, 1},
    {"epr-16082.14", 0xb23f22ee, DESCRIPTORS, 0, 0x80000, 1, 2},
    {"epr-16083.15", 0xd12c77f8, DESCRIPTORS, 1, 0x80000, 1, 2},
    {"mpr-16090.12", 0x90c76831, COLOURS, 0, 0x80000, 1, 2},
    {"mpr-16091.13", 0x53115448, COLOURS, 1, 0x80000, 1, 2},
    {"mpr-16084.6",  0x483f453b, GRAPHICS, 0, 0x80000, 1, 2},
    {"mpr-16085.7",  0x5fa01277, GRAPHICS, 1, 0x80000, 1, 2},
    {"mpr-16096.26", 0xa92b0bf3, POLYGONS, 0x000000, 0x200000, 2, 4},
    {"mpr-16097.27", 0x0232955a, POLYGONS, 0x000002, 0x200000, 2, 4},
    {"mpr-16098.28", 0xcf2e1b84, POLYGONS, 0x400000, 0x200000, 2, 4},
    {"mpr-16099.29", 0x20e46854, POLYGONS, 0x400002, 0x200000, 2, 4},
    {"mpr-16100.30", 0xe13e983d, POLYGONS, 0x800000, 0x200000, 2, 4},
    {"mpr-16101.31", 0x0dbed94d, POLYGONS, 0x800002, 0x200000, 2, 4},
    {"mpr-16102.32", 0x4cb41fb6, POLYGONS, 0xc00000, 0x200000, 2, 4},
    {"mpr-16103.33", 0x526d1c76, POLYGONS, 0xc00002, 0x200000, 2, 4},
};

struct decoder {
    std::vector<uint8_t> regions[REGION_COUNT];

    const uint8_t *bytes(int region, size_t at, size_t count) const
    {
        const auto &r = regions[region];
        if (at > r.size() || count > r.size() - at) throw std::runtime_error("model address outside ROM");
        return r.data() + at;
    }
    const uint8_t *main_bytes(uint32_t a, size_t count) const
    {
        if (a >= 0xfc0000 && a < 0xfe0000) return bytes(RECORDS, a - 0xfc0000, count);
        if (a >= 0x200000 && a < 0x300000) return bytes(DESCRIPTORS, a - 0x200000, count);
        if (a >= 0x1000000 && a < 0x1100000) return bytes(GRAPHICS, a - 0x1000000, count);
        if (a >= 0x1300000 && a < 0x1400000) return bytes(COLOURS, a - 0x1300000, count);
        throw std::runtime_error("unsupported character record address");
    }
    uint32_t word(uint32_t a) const { return rom_zip::le32(main_bytes(a, 4)); }
    uint32_t poly_word(uint32_t a) const { return rom_zip::le32(bytes(POLYGONS, size_t(a) * 4, 4)); }
    point vertex(uint32_t a) const
    {
        point p;
        for (int k = 0; k < 3; ++k) {
            const auto bits = poly_word(a + k);
            memcpy(&p[k], &bits, 4);
            if (!std::isfinite(p[k]) || std::abs(p[k]) > 1000000)
                throw std::runtime_error("invalid model vertex");
        }
        return p;
    }
    uint32_t colour(uint32_t texture) const
    {
        // VF uploads the colour-index stream at maincpu 0x1300000 to TGP
        // address 0x40000, and the BGR555 palette at 0x2d963c to palette bank
        // 0x1000. Reading these ROM tables gives the same unlit dump_part colours.
        if (texture < 0x40000) throw std::runtime_error("invalid colour-stream address");
        const auto index = rom_zip::le16(bytes(COLOURS, size_t(texture - 0x40000) * 2, 2)) & 0x3ff;
        const auto bgr = rom_zip::le16(main_bytes(0x2d963c + 2 * index, 2));
        const uint32_t r = (bgr & 31) * 255 / 31, g = ((bgr >> 5) & 31) * 255 / 31,
                       b = ((bgr >> 10) & 31) * 255 / 31;
        return 0xff000000 | (r << 16) | (g << 8) | b;
    }
    static void triangle(mesh &out, uint32_t colour, const point &a, const point &b, const point &c)
    {
        double u[3], v[3];
        for (int k = 0; k < 3; ++k) { u[k] = double(b[k]) - a[k]; v[k] = double(c[k]) - a[k]; }
        const double x = u[1]*v[2] - u[2]*v[1], y = u[2]*v[0] - u[0]*v[2], z = u[0]*v[1] - u[1]*v[0];
        if (x*x + y*y + z*z > 1e-20) out.push_back({colour, {a, b, c}});
    }
    void part(mesh &out, uint32_t descriptor) const
    {
        if ((descriptor >> 16) != 0x2d) throw std::runtime_error("invalid model descriptor");
        const uint32_t count = word(descriptor);
        uint32_t tex = word(descriptor + 4), a = word(descriptor + 8);
        if (!count || count > 4096 || a & 0x800000) throw std::runtime_error("unsupported model stream");
        point old0 = vertex(a), old1 = vertex(a + 3);
        a += 6;
        for (uint32_t i = 0; i < count; ++i, a += 10) {
            const uint32_t flags = poly_word(a), type = flags & 3, link = (flags >> 8) & 3;
            if (!type) break;
            if (flags & 0x1000) ++tex;
            const point next0 = vertex(a + 4), next1 = type == 2 ? next0 : vertex(a + 7);
            if (link) {
                const uint32_t rgb = colour(tex);
                triangle(out, rgb, old1, old0, next0);
                triangle(out, rgb, old1, next0, next1);
            }
            switch (link) {
            case 0: case 2: old0 = next0; old1 = next1; break;
            case 1: old1 = next0; break;
            case 3: old0 = next1; break;
            }
        }
        if (out.empty() || out.size() > 16384) throw std::runtime_error("empty or oversized model");
    }

};
}

bool load(const std::string &path, Assets &out, std::string &error, unsigned fighter)
{
    try {
        if (fighter >= 8) throw std::runtime_error("fighter is not in the VF1 roster");
        rom_zip::zip z;
        if (!z.open(path)) throw std::runtime_error("cannot read ZIP");
        decoder d;
        d.regions[RECORDS].resize(0x20000);
        d.regions[DESCRIPTORS].resize(0x100000);
        d.regions[COLOURS].resize(0x100000);
        d.regions[POLYGONS].resize(0x1000000);
        d.regions[GRAPHICS].resize(0x100000);
        std::vector<uint8_t> raw;
        for (const auto &ro : ROMS) {
            const rom_zip::entry *e = nullptr;
            for (const auto &candidate : z.entries)
                if (candidate.crc == ro.crc && candidate.usize == ro.size) { e = &candidate; break; }
            if (!e || !z.read(*e, raw))
                throw std::runtime_error(std::string("missing, wrong or damaged VF1 model ROM: ") + ro.name);
            auto &dest = d.regions[ro.region];
            for (size_t i = 0; i < raw.size(); i += ro.unit)
                memcpy(dest.data() + ro.offset + (i / ro.unit) * ro.stride, raw.data() + i, ro.unit);
        }
        // Original P1 records, in the shared roster order (Akira through Jacky).
        constexpr uint32_t records[] = {
            0xfd4f99, 0xfd3987, 0xfd2f45, 0xfd3525, 0xfd3b05, 0xfd3231, 0xfd3811, 0xfd49b9
        };
        const uint32_t record = records[fighter];
        struct binding { Part part; uint32_t offset; };
        constexpr binding body[] = {
            {Part::Torso, 0x12}, {Part::Pelvis, 0x32},
            {Part::UpperArmA, 0x1a}, {Part::LowerArmA, 0x1e},
            {Part::UpperArmB, 0x26}, {Part::LowerArmB, 0x2a},
            {Part::UpperLegA, 0x36}, {Part::LowerLegA, 0x3a}, {Part::FootA, 0x3e},
            {Part::UpperLegB, 0x42}, {Part::LowerLegB, 0x46}, {Part::FootB, 0x4a},
        };
        Assets prepared;
        const auto byte = [&](uint32_t at) { return *d.main_bytes(at, 1); };
        const auto real = [&](uint32_t at) {
            const uint32_t bits = d.word(at);
            float value; memcpy(&value, &bits, sizeof value);
            if (!std::isfinite(value) || std::abs(value) > 1000000)
                throw std::runtime_error("invalid source attachment coordinate");
            return value;
        };
        const uint32_t skeleton = d.word(record);
        for (size_t bone = 0; bone < prepared.source_joints.size(); ++bone)
            for (unsigned axis = 0; axis < 3; ++axis)
                prepared.source_joints[bone][axis] = real(skeleton + uint32_t(bone) * 12 + axis * 4);
        for (const auto &b : body)
            d.part(prepared.parts[size_t(b.part)], d.word(record + b.offset));
        // Four fixed parts, paired eye/brow lists, and a mouth list.
        // Neutral VF1 appearance used by the existing VF2 integration.
        const uint32_t lists = d.word(record + 0x72);
        const unsigned frames[] = {0, 0, 0, 0, 0, 1, 0, 1, fighter == 5 ? 0u : 1u};
        auto &head = prepared.parts[size_t(Part::Head)];
        for (unsigned part = 0; part < 9; ++part) {
            // Kage has no separate brows; his mouth frame 0 is the blue mask.
            const uint32_t list = d.word(lists + 4 * part);
            if (list) d.part(head, d.word(list + 4 * frames[part]));
        }
        if (head.empty()) throw std::runtime_error("missing fighter head");
        for (size_t side = 0; side < 2; ++side) {
            const uint32_t table = d.word(record + (side ? 0x5e : 0x62));
            for (size_t pose = 0; pose < 32; ++pose)
                d.part(prepared.hands[side][pose], d.word(table + uint32_t(pose) * 4));
        }
        d.part(prepared.belt_knot, d.word(record + 0x0e));
        // Native dynamic accessories are keyed by costume slot, not record.
        // Keep the first head chain and two pelvis chains in the legacy slots;
        // braids/dreadlocks beyond the first head chain use the variable list.
        unsigned slot = 26;
        for (unsigned i = 0; i < 26; ++i)
            if (d.word(0xfd2e8f + 4 * i) == record) { slot = i; break; }
        if (slot == 26) throw std::runtime_error("missing VF1 costume slot");
        uint32_t strands = 0;
        bool terminated = false;
        for (unsigned i = 0; i < 15; ++i) {
            const uint32_t at = 0xfda917 + 5 * i;
            if (byte(at) == 255) { terminated = true; break; }
            if (byte(at) == slot) strands = d.word(at + 1);
        }
        if (!terminated) throw std::runtime_error("unterminated VF1 strand list");
        unsigned head_chains = 0, belt_chains = 0, total_segments = 0;
        if (strands) {
            uint32_t cursor = strands + 4;
            const unsigned groups = byte(cursor++);
            if (!groups || groups > 8) throw std::runtime_error("invalid VF1 strand group count");
            for (unsigned group = 0; group < groups; ++group) {
                cursor += 0x46; // original solver's group parameters
                const unsigned chains = byte(cursor++);
                if (!chains || chains > 8) throw std::runtime_error("invalid VF1 strand chain count");
                for (unsigned chain = 0; chain < chains; ++chain) {
                    Strand strand;
                    strand.bone = byte(cursor++);
                    for (float &axis : strand.anchor) { axis = real(cursor); cursor += 4; }
                    const unsigned count = byte(cursor++);
                    total_segments += count;
                    if (!count || count > 16 || total_segments > 64)
                        throw std::runtime_error("invalid VF1 strand segment count");
                    strand.segments.resize(count); strand.lengths.resize(count);
                    for (unsigned segment = 0; segment < count; ++segment) {
                        d.part(strand.segments[segment], d.word(cursor));
                        const float length = real(cursor + 4);
                        if (!(length > 0 && length < 1)) throw std::runtime_error("invalid strand length");
                        strand.lengths[segment] = length;
                        // Native Kage P1 belt polygons use a 0.1 TGP scale.
                        // Their anchors and segment lengths are already metres.
                        if (fighter == 5 && strand.bone == 9)
                            for (auto &triangle : strand.segments[segment])
                                for (auto &point : triangle.points) for (float &axis : point) axis *= .1f;
                        cursor += 0x18; // descriptor, length, spring parameter, initial world position
                    }
                    if (strand.bone == 2) {
                        if (head_chains++) prepared.extra_head_ribbons.push_back(std::move(strand));
                        else prepared.strands[0] = std::move(strand);
                    } else if (strand.bone == 9 && belt_chains < 2) {
                        prepared.strands[1 + belt_chains++] = std::move(strand);
                    } else throw std::runtime_error("unsupported VF1 strand attachment");
                }
            }
        }
        // Original 7x8-tile select artwork. Akira's updated icon uses byte
        // indices; the remaining portraits use word indices and graphics bank 0.
        struct icon_source { uint32_t map, graphics, palette; unsigned first, count; };
        constexpr icon_source icons[] = {
            {0x2ff5ec, 0x2fef8c, 0x2ff5cc, 0,      50},
            {0x13d1997,0x107f704,0x1082b44,0x2480,49},
            {0x13d191b,0x107f0c0,0x1082b24,0x2400,50},
            {0x13d1b87,0x1081174,0x1082bc4,0x2680,49},
            {0x13d1b0b,0x1080a90,0x1082ba4,0x2600,55},
            {0x13d1c03,0x1081798,0x1082be4,0x2700,56},
            {0x13d1a13,0x107fd28,0x1082b64,0x2500,53},
            {0x13d189f,0x107d910,0x1082aa4,0x2200,49}
        };
        const auto &icon=icons[fighter];
        if(d.word(icon.map)!=(fighter ? 0x18000u : 0xb080u) || d.word(icon.map+4)!=8 || d.word(icon.map+8)!=7)
            throw std::runtime_error("unsupported VF1 portrait map");
        auto &portrait=prepared.portrait;
        portrait.width=56;portrait.height=64;portrait.pixels.resize(56*64);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<56;++x) {
            const unsigned cell=(y/8)*7+x/8;
            const unsigned tile=(fighter ? rom_zip::le16(d.main_bytes(icon.map+12+2*cell,2)) :
                byte(icon.map+12+cell))-icon.first;
            if(tile>=icon.count)throw std::runtime_error("invalid VF1 portrait tile");
            const auto *row=d.main_bytes(icon.graphics+tile*32+(y%8)*4,4);
            const uint32_t packed=(uint32_t(rom_zip::le16(row))<<16)|rom_zip::le16(row+2);
            const unsigned pen=(packed>>(28-4*(x%8)))&15;
            const auto bgr=rom_zip::le16(d.main_bytes(icon.palette+pen*2,2));
            uint32_t rgb=0xff000000;
            for(unsigned axis=0;axis<3;++axis) {
                const unsigned value=(bgr>>(axis*5))&31;
                unsigned channel=(value<<3)|(value>>2);
                if(!(bgr&0x8000))channel>>=1;
                rgb|=channel<<(16-axis*8);
            }
            portrait.pixels[y*56+x]=rgb;
        }
        out = std::move(prepared);
        error.clear();
        return true;
    } catch (const std::runtime_error &e) {
        error = "VF1 models (" + path + "): " + e.what();
        return false;
    }
}
}
