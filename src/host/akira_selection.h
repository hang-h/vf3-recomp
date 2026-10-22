// Per-player appearance choice. Only the character-select input is intercepted;
// fighting inputs and the emulated game's rules remain untouched.
#pragma once
#include "../machine/model3.h"
#include <array>
#include <cstring>
#include <span>

class AkiraSelection {
public:
    struct Player { bool human=false, selecting=false, akira=false; };
    using Snapshot = std::array<Player,2>;
    // VF3 revision D's two character-select tasks. These are PPC byte
    // addresses: Model3 stores each 32-bit RAM word in host order.
    static Snapshot snapshot(std::span<const uint8_t> ram)
    {
        Snapshot result{};
        if (ram.size()<0x108100) return result;
        for (unsigned p=0;p<2;++p) {
            const size_t base=0x108000+0x80*p;
            uint32_t phase;
            std::memcpy(&phase,ram.data()+base,sizeof phase);
            const unsigned fighter=ram[(base+0x42)^3];
            result[p]={phase!=0,phase==5,fighter==0 || fighter==13};
        }
        return result;
    }
    // Rendered actor identity can differ from the selection cursor (CPU,
    // alternate outfit, next opponent). Translate player choice into the
    // two supported head layouts, only when that actor identity is unique.
    static unsigned outfits(std::span<const uint8_t> ram, unsigned players)
    {
        if (ram.size()<=0x10a7f3) return 0;
        const unsigned actor[]={ram[0x1087f0^3],ram[0x10a7f0^3]};
        unsigned result=0;
        for (unsigned p=0;p<2;++p) if ((players & (1u<<p)) && actor[p]!=actor[p^1]) {
            if (actor[p]==0) result |= 1;
            else if (actor[p]==13) result |= 2;
        }
        return result;
    }
    void reset() { *this = AkiraSelection(); }
    void update(m3::Controls &input, const Snapshot &state, bool available)
    {
        for (unsigned p=0;p<2;++p) {
            const bool up=input.up[p], down=input.down[p];
            const bool up_edge=up && !previous_up[p], down_edge=down && !previous_down[p];
            previous_up[p]=up; previous_down[p]=down;
            const auto &s=state[p];
            if (!available || !s.human || (s.selecting && !was_selecting[p])) {
                selected[p]=false; locked[p]=false; eat_down[p]=false;
            }
            if (s.selecting && !s.akira) selected[p]=false;
            if (available && s.human && s.selecting && s.akira) {
                // VF3 has two roster rows. Down still navigates normally when
                // VF1 is not selected, so the lower row remains reachable.
                if (!locked[p]) {
                    if (up_edge && !down) selected[p]=true;
                    else if (down_edge && !up && selected[p]) {
                        selected[p]=false; eat_down[p]=true;
                    }
                }
                input.up[p]=false;
                if (!down) eat_down[p]=false;
                if (eat_down[p]) input.down[p]=false;
                if (input.punch[p] || input.kick[p] || input.guard[p] || input.escape[p]) locked[p]=true;
            } else eat_down[p]=false;
            was_selecting[p]=s.selecting;
        }
    }
    unsigned mask(const Snapshot &state) const
    {
        unsigned result=0;
        for (unsigned p=0;p<2;++p)
            if (selected[p] && state[p].human && state[p].akira) result |= 1u<<p;
        return result;
    }
private:
    std::array<bool,2> selected{}, locked{}, previous_up{}, previous_down{}, was_selecting{}, eat_down{};
};
