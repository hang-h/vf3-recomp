// Visual retargeting state derived from the native game, never from input buttons.
#pragma once
#include "fighter_expression.h"
#include <algorithm>
#include <limits>

namespace fighter_expression {
enum class Kind { Neutral, Selection, Hurt, Victory, Defeat, HurtRecovery, VictoryRecovery };
struct Pose {
    bool valid=false;
    unsigned actor=255;
    Kind kind=Kind::Neutral;
    unsigned tick=0;
    bool speaking=false;
    float mouth_open=0;
    unsigned blink=0; // host cosmetic timer: open / half / closed, not a native morph ID
};
using Poses=std::array<Pose,2>;

class Tracker {
public:
    void reset() {*this=Tracker();}
    const Poses &poses() const {return current;}
    void update(const Snapshot &state) {
        for(unsigned player=0;player<2;++player) {
            const auto &s=state[player];
            Kind kind=Kind::Neutral;
            if(s.valid) {
                // The source selection gesture starts on confirmation. A
                // hovering cursor must keep its neutral preview, even after
                // the player has spent a long time choosing an edition.
                if(s.preview)kind=s.phase==5 ? Kind::Neutral : Kind::Selection;
                else if(s.victory)kind=Kind::Victory;
                else if(s.defeat || (!s.hp && (s.actor_flags&0x04000000)))kind=Kind::Defeat;
                // Proven hit handlers and the victim half of a native throw.
                else if(s.action_category==10 || s.action_category==14 || s.action_category==13)kind=Kind::Hurt;
            }
            const auto &old=previous[player];
            auto &p=current[player];
            if(kind==Kind::Neutral && !s.preview && p.actor==s.actor && s.valid) {
                if(p.kind==Kind::Hurt || (p.kind==Kind::HurtRecovery && p.tick<31))kind=Kind::HurtRecovery;
                else if(p.kind==Kind::Victory || (p.kind==Kind::VictoryRecovery && p.tick<31))kind=Kind::VictoryRecovery;
            }
            const bool new_motion=kind==Kind::Hurt &&
                (s.action!=old.action || s.animation!=old.animation || s.animation_frame<old.animation_frame);
            const bool changed=p.valid!=s.valid || p.actor!=s.actor || p.kind!=kind || new_motion;
            p={s.valid,s.actor,kind,changed ? 0u : p.tick<std::numeric_limits<unsigned>::max() ? p.tick+1 : p.tick};
            // Independent per-player cosmetic clock. It advances once per game
            // frame, not per render/interpolated picture, and never writes RAM.
            if(!s.valid || !old.valid || old.actor!=s.actor) {
                blink_wait[player]=180+73*player;blink_at[player]=6;
            } else if(blink_at[player]<6)++blink_at[player];
            else if(--blink_wait[player]==0) {
                blink_at[player]=0;
                blink_seed[player]=blink_seed[player]*1664525u+1013904223u;
                blink_wait[player]=120+(blink_seed[player]>>16)%240;
            }
            constexpr unsigned stages[]={1,2,2,2,1,1};
            p.blink=blink_at[player]<6?stages[blink_at[player]]:0;
            p.speaking=s.native_face_valid;
            p.mouth_open=s.mouth_open;
            previous[player]=s;
        }
    }
private:
    std::array<unsigned,2> blink_wait{180,253},blink_at{6,6},blink_seed{42,137};
    Snapshot previous{};
    Poses current{};
};
}
