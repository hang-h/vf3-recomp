// Per-player appearance choice. Only the character-select input is intercepted;
// fighting inputs and the emulated game's rules remain untouched.
#pragma once
#include "../machine/model3.h"
#include "fighter_roster.h"
#include <array>
#include <cstring>
#include <span>

class AkiraSelection {
public:
    struct Player {
        bool human=false, selecting=false, akira=false;
        unsigned outfit=0; // 1=original, 2=alternate (selection cursor, before actors spawn)
        bool preview=false;
        unsigned actor=255;
        bool fighting=false;
        unsigned fighter=0; // shared roster index; 255 for unsupported VF3 fighters
    };
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
            const unsigned actor=ram[(base+0x42)^3];
            const unsigned fighter=fighter_roster::from_actor(actor);
            result[p]={phase!=0,phase==5,fighter==0,
                       actor<13 ? 1u : actor<26 ? 2u : 0u,
                       phase==5 || phase==13 || phase==11,
                       ram.size()>0x10a7f3 ? unsigned(ram[(0x1087f0+0x2000*p)^3]) : 255u,
                       phase==10,fighter};
        }
        return result;
    }
    // Rendered actor identity can differ from the selection cursor (CPU,
    // alternate outfit, next opponent). Translate player choice into the
    // requested fighter's two costumes, only when that actor identity is unique.
    static unsigned outfits(std::span<const uint8_t> ram, unsigned players, unsigned fighter=0)
    {
        if (ram.size()<=0x10a7f3) return 0;
        const unsigned actor[]={ram[0x1087f0^3],ram[0x10a7f0^3]};
        unsigned result=0;
        for (unsigned p=0;p<2;++p) if ((players & (1u<<p)) && actor[p]!=actor[p^1]) {
            if (fighter<fighter_roster::count && fighter_roster::from_actor(actor[p])==fighter)
                result |= actor[p]<13 ? 1u : 2u;
        }
        return result;
    }
    // Selection previews exist before the two battle actors have distinct IDs.
    // Use the cursor only during selection/confirmation, never to identify a fighter.
    static unsigned previews(const Snapshot &state, unsigned players, unsigned fighter=0)
    {
        // A challenger's selection also shows the already-confirmed opponent.
        if(!state[0].preview && !state[1].preview) return 0;
        unsigned result=0;
        for(unsigned p=0;p<2;++p)
            if((players & (1u<<p)) && state[p].human && fighter<fighter_roster::count && state[p].fighter==fighter)
                result |= state[p].outfit;
        return result;
    }
    void reset() { *this = AkiraSelection(); }
    void update(m3::Controls &input, const Snapshot &state, bool vf1_available, bool vf2_available=false)
    {
        for (unsigned p=0;p<2;++p) {
            const bool up=input.up[p], down=input.down[p];
            const bool up_edge=up && !previous_up[p], down_edge=down && !previous_down[p];
            previous_up[p]=up; previous_down[p]=down;
            const auto &s=state[p];
            const bool loaded[]={true,vf1_available && fighter_roster::available(s.fighter,1),
                                      vf2_available && fighter_roster::available(s.fighter,2)};
            const bool available=loaded[1] || loaded[2];
            if (!loaded[selected[p]] || (!s.human && !retains_result(p,state)) ||
                (s.selecting && (!was_selecting[p] || previous_fighter[p]!=s.fighter))) {
                selected[p]=0; locked[p]=false; eat_down[p]=false;
                result_bound[p]=false;
            }
            if (selected[p] && choice_fighter[p]!=s.fighter) {
                selected[p]=0; locked[p]=false; result_bound[p]=false;
            }
            if (available && s.human && s.selecting) {
                // VF3 has two roster rows. Down still navigates normally when
                // VF3 is selected, so the lower row remains reachable.
                if (!locked[p]) {
                    if (up_edge && !down) {
                        // One edge per press, skipping companions not installed.
                        do { selected[p]=(selected[p]+1)%3; } while (!loaded[selected[p]]);
                        choice_fighter[p]=s.fighter;
                    }
                    else if (down_edge && !up && selected[p]) {
                        selected[p]=0; eat_down[p]=true;
                    }
                }
                input.up[p]=false;
                if (!down) eat_down[p]=false;
                if (eat_down[p]) input.down[p]=false;
                if (input.punch[p] || input.kick[p] || input.guard[p] || input.escape[p]) locked[p]=true;
            } else eat_down[p]=false;
            // The player task becomes inactive at the final KO, before the
            // replay and You Lose pose finish. Keep the choice attached to this
            // battle's actor pair, rather than ending it with the input task.
            if(selected[p] && s.fighting && s.fighter==choice_fighter[p] &&
               fighter_roster::from_actor(s.actor)==s.fighter && s.actor!=state[p^1].actor) {
                result_actors[p]={state[0].actor,state[1].actor};
                result_bound[p]=true;
            }
            was_selecting[p]=s.selecting;
            previous_fighter[p]=s.fighter;
        }
    }
    unsigned mask(const Snapshot &state, unsigned variant=1) const
    {
        unsigned result=0;
        for (unsigned p=0;p<2;++p)
            if (variant && selected[p]==variant && state[p].fighter==choice_fighter[p] &&
                fighter_roster::available(state[p].fighter,variant) &&
                (state[p].human || retains_result(p,state))) result |= 1u<<p;
        return result;
    }
private:
    bool retains_result(unsigned p,const Snapshot &state) const
    {
        // A new selection or actor setup ends ownership. In particular, an
        // attract-mode fighter must not inherit the previous player's appearance.
        return result_bound[p] && state[p].fighter==choice_fighter[p] && !state[0].preview && !state[1].preview &&
            result_actors[p][0]==state[0].actor && result_actors[p][1]==state[1].actor;
    }
    std::array<unsigned,2> previous_fighter{255,255}, choice_fighter{255,255};
    std::array<unsigned,2> selected{}; // 0=VF3, 1=VF1, 2=VF2
    std::array<std::array<unsigned,2>,2> result_actors{};
    std::array<bool,2> result_bound{};
    std::array<bool,2> locked{}, previous_up{}, previous_down{}, was_selecting{}, eat_down{};
};
