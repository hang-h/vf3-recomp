#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace vf2_models::detail {
// VF2's texture uploader uses an LSB-first Huffman stream of packed 2x2
// four-bit texels. This is a direct translation of the ROM routines at
// 0x4c180 (header/tree), 0x4c6e0 (symbols), 0x4cb64 (rows), and 0x4d16c
// (placement). The caller validates the VF2 ROM CRCs before reaching here.
// It decodes the base level for colour baking into the destination meshes.
class TextureDecoder {
    const std::vector<std::uint8_t>& rom;
    static constexpr std::uint32_t base = 0x02000000;
    [[noreturn]] static void bad() { throw std::runtime_error("invalid VF2 Akira texture stream"); }
    std::uint32_t word(std::uint32_t address) const {
        if (address < base || std::uint64_t(address - base) + 4 > rom.size()) bad();
        const auto* p = rom.data() + address - base;
        return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    }
    struct Bits {
        const std::vector<std::uint8_t>& bytes;
        std::uint64_t position, end;
        std::uint32_t get(unsigned n) {
            if (n > 16 || position + n > end) bad();
            std::uint32_t result = 0;
            for (unsigned i = 0; i < n; ++i, ++position)
                result |= std::uint32_t((bytes[position / 8] >> (position % 8)) & 1) << i;
            return result;
        }
    };
    void decode(std::uint32_t address, std::uint32_t limit, std::uint32_t placement,
                std::vector<std::uint16_t>& sheet) const {
        if (word(address) != 0 || limit < address + 4 || limit < base || limit - base > rom.size()) bad();
        Bits bits{rom, std::uint64_t(address + 4 - base) * 8, std::uint64_t(limit - base) * 8};
        const auto rows = (bits.get(8) + 1) / 2;
        const auto columns = (bits.get(8) + 1) / 2;
        const auto treeBits = bits.get(8), leaves = bits.get(16);
        (void)bits.get(16); // Encoder's symbol count; the row dimensions bound decoding.
        const auto customCount = bits.get(16), customBits = bits.get(4);
        (void)bits.get(16); // Intermediate RLE marker, unnecessary for direct output.
        if (!rows || !columns || treeBits == 0 || treeBits > 16 || !leaves || leaves > 1024 ||
            customCount > 4096 || customBits > 12) bad();
        std::vector<std::uint32_t> tree(leaves * 2 - 1);
        for (auto& token : tree) token = bits.get(treeBits);
        std::vector<std::array<std::uint32_t, 2>> custom(customCount);
        for (auto& pair : custom) { pair[0] = bits.get(16); pair[1] = bits.get(4); }
        // Group 2 occupies the first 12 entries in the ROM placement table
        // 0x4c120: four rows of 256 pixels, then the following column.
        const auto tile = (placement & 0xffff) >> 1;
        if ((placement & 1) || tile >= 16) bad();
        const auto y0 = (tile % 4) * 256 + (placement >> 24);
        const auto x0 = (tile / 4) * 256 + ((placement >> 16) & 255);
        if ((x0 & 1) || (y0 & 1) || x0 + columns * 2 > 1024 || y0 + rows * 2 > 2048) bad();
        std::uint32_t previous = 0;
        for (unsigned y = 0; y < rows; ++y) {
            unsigned x = 0;
            while (x < columns) {
                std::uint32_t index = 0, token = 0;
                unsigned depth = 0;
                for (;;) {
                    if (index >= tree.size() || ++depth > 64) bad();
                    token = tree[index];
                    if (token < 322) break;
                    index = bits.get(1) ? token - 322 : index + 1;
                }
                std::uint32_t value = 0, count = 1;
                if (token < 256) {
                    const auto entry = word(0x02300010 + token * 4);
                    previous = (previous + (entry & 15)) & 15;
                    value = (entry >> 8) + previous * 0x1111;
                } else if (token == 256) {
                    const auto code = bits.get(customBits);
                    if (code >= custom.size()) bad();
                    previous = (previous + custom[code][1]) & 15;
                    value = custom[code][0] + previous * 0x1111;
                } else if (token == 257) {
                    value = bits.get(16);
                    previous = (previous + bits.get(4)) & 15;
                    value += previous * 0x1111;
                } else if (token < 290) {
                    count = token - 257;
                    if (count > 16) count = (count - 16) * 16;
                    value = previous * 0x1111;
                } else {
                    value = token - 289;
                    if (value > 16) value = (value - 16) * 16;
                    value += previous * 0x1111;
                }
                if (count > columns - x) bad();
                for (unsigned i = 0; i < count; ++i)
                    sheet[(y0 / 2 + y) * 512 + x0 / 2 + x++] = std::uint16_t(value);
            }
        }
    }
    void raw(std::uint32_t address, std::uint32_t limit, std::uint32_t placement,
             std::vector<std::uint16_t>& sheet) const {
        if (word(address) != 1 || address < base || limit < address + 7 || limit - base > rom.size()) bad();
        auto source = std::uint64_t(address - base) + 4;
        const auto rows = (std::uint32_t(rom[source]) + 1) / 2;
        const auto columns = (std::uint32_t(rom[source + 1]) + 1) / 2;
        source += 2;
        source += rom[source];
        const auto tile = (placement & 0xffff) >> 1;
        if ((placement & 1) || tile >= 16 || !rows || !columns || columns % 8 ||
            source % 16 || source + rows * columns * 2 > limit - base) bad();
        const auto y0 = (tile % 4) * 256 + (placement >> 24);
        const auto x0 = (tile / 4) * 256 + ((placement >> 16) & 255);
        if ((x0 & 1) || (y0 & 1) || x0 + columns * 2 > 1024 || y0 + rows * 2 > 2048) bad();
        // 0x4c9dc uploads four low halfwords, then their four high halves.
        for (unsigned y = 0; y < rows; ++y) {
            for (unsigned x = 0; x < columns; x += 8, source += 16) {
                for (unsigned k = 0; k < 8; ++k) {
                    const auto at = source + (k < 4 ? k * 4 : (k - 4) * 4 + 2);
                    sheet[(y0 / 2 + y) * 512 + x0 / 2 + x + k] =
                        std::uint16_t(rom[at] | (std::uint16_t(rom[at + 1]) << 8));
                }
            }
        }
    }
public:
    explicit TextureDecoder(const std::vector<std::uint8_t>& data) : rom(data) {}
    // Physical sheet: 1024 x 2048 texels, packed into 2x2 blocks. Akira's
    // three raw and nine compressed base textures (groups 1/2) use sheet 1. No game startup,
    // emulated machine, extracted texture file, or precomputed ROM data needed.
    std::vector<std::uint16_t> akiraSheet() const {
        const auto groups = word(0x0230000c);
        std::vector<std::uint16_t> sheet(0x80000, 0);
        for (unsigned group = 1; group <= 2; ++group) {
            const auto sources = word(groups + group * 4);
            const auto sourceEnd = word(groups + (group + 1) * 4);
            const auto placements = word(word(0x02300008) + word(sources) * 4);
            const auto count = group == 1 ? 3u : 9u;
            if (word(placements) != count) bad();
            for (unsigned i = 0; i < count; ++i) {
                const auto start = word(sources + 4 + i * 4);
                const auto end = i + 1 < count ? word(sources + 8 + i * 4) : sourceEnd;
                const auto destination = word(placements + 4 + i * 4);
                if (group == 1) raw(start, end, destination, sheet);
                else decode(start, end, destination, sheet);
            }
        }
        return sheet;
    }
};
} // namespace vf2_models::detail
