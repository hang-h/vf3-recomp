// VF3 revision D's native actor animation state. These are game-produced
// values, sampled after run_frame; no controller input predicts an expression.
#pragma once
#include "fighter_roster.h"
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <span>

namespace fighter_expression {
struct Sample {
    bool valid=false, preview=false;
    unsigned actor=255, phase=0, action_category=0;
    unsigned animation=0, animation_frame=0, hp=0;
    uint32_t action=0, pending_action=0;
    uint32_t actor_flags=0, winner=0, loser=0;
    unsigned native_phase=0, native_subphase=0;
    bool victory=false, defeat=false;
    // Native high-detail speech morph, when the exact actor's controller is
    // present. The target is a ROM morph index, not a semantic expression ID.
    bool native_face_valid=false;
    unsigned face_script=0, face_step=0, face_target=0, face_previous_target=0;
    unsigned face_transition_frames=0, face_hold_frames=0;
    float face_blend=0;
    float mouth_open=0; // measured VF3 lower-lip displacement, normalized per fighter
};
using Snapshot=std::array<Sample,2>;

// Revision D's seven native head morph banks, in shared-roster order. Values
// measure the vertical displacement of the lower central lip vertex relative
// to bank 0, normalized by that vertex's largest displacement across banks.
// Lip vertices: 402,350,521,39,306,343,388,366,345,408. Each is the vertex
// with maximal jaw descent in bank 2; no assumption that morph IDs are ordered
// by mouth opening. Bank pointers are in the descriptor table at 80f90+20*ID,
// entry +12 -> bank-0 CROM header, then 7 * vertex_count * 24-byte records.
inline constexpr std::array<std::array<float,7>,10> mouth_shapes={{
    {{0,.049144f,1,.396217f,.396217f,.543712f,.968367f}},
    {{0,.146449f,.993532f,.268489f,.268489f,.755309f,1}},
    {{0,0,1,.371201f,.340744f,.604861f,.803557f}},
    {{0,0,1,.269010f,.269010f,.555057f,.703506f}},
    {{0,.226576f,1,.284775f,.224611f,.509305f,.701588f}},
    {{0,.011915f,1,.309800f,.339589f,.514924f,.722788f}},
    {{0,.017268f,.928855f,.276291f,.362632f,.570022f,1}},
    {{0,.002254f,1,.377178f,.276593f,.533082f,.859632f}},
    {{0,0,1,.285218f,.429894f,.614831f,.882192f}},
    {{0,.013842f,1,.256073f,.290678f,.499066f,.882345f}}
}};

inline Snapshot snapshot(std::span<const uint8_t> ram)
{
    Snapshot result{};
    if(ram.size()<0x10a800) return result;
    // PPC RAM words are stored in host order by Model3.
    const auto word=[&](size_t address) {
        uint32_t value; std::memcpy(&value,ram.data()+address,4); return value;
    };
    const auto byte=[&](size_t address) { return unsigned(ram[address^3]); };
    const auto half=[&](size_t address) {
        uint16_t value; std::memcpy(&value,ram.data()+(address^2),2); return unsigned(value);
    };
    const unsigned phase[]={word(0x108000),word(0x108080)};
    const auto preview_phase=[](unsigned p) { return p==5 || p==13 || p==11; };
    const bool preview=preview_phase(phase[0]) || preview_phase(phase[1]);
    // Revision D's round resolver (29bdc/29be0) stores the winning and
    // losing actor pointers. Subphase 0d replays the finishing move; 0e/0f
    // show the victory pose selected by 40048, ending at 2a5e4. The pointers
    // remain populated later, so they alone must never imply a result pose.
    const unsigned native_phase=byte(0x102008), native_subphase=byte(0x10200b);
    const uint32_t winner=word(0x1061c4), loser=word(0x1061c8);
    const bool result_pose=native_phase==7 && (native_subphase==14 || native_subphase==15) &&
        ((winner==0x108780 && loser==0x10a780) || (winner==0x10a780 && loser==0x108780));
    for(unsigned p=0;p<2;++p) {
        auto &s=result[p];
        const size_t actor=0x108780+0x2000*p;
        s.phase=phase[p];
        s.preview=preview && phase[p]!=0;
        s.actor=s.preview ? byte(0x108042+0x80*p) : byte(actor+0x70);
        // An inactive selection slot retains its previous battle actor and
        // hit state. It must not override the active player's preview. Phase
        // zero remains valid in battle, where it also identifies CPU actors.
        s.valid=s.actor<26 && (!preview || phase[p]!=0);
        s.action=word(actor+0x48);
        s.pending_action=word(actor+0x44);
        s.action_category=s.action>>24;
        const auto animation=word(actor+0x4c);
        s.animation=animation>>16;
        s.animation_frame=animation&0xffff;
        s.hp=half(actor+0x56);
        s.actor_flags=word(actor);
        s.native_phase=native_phase;
        s.native_subphase=native_subphase;
        s.winner=winner; s.loser=loser;
        s.victory=s.valid && !s.preview && result_pose && winner==actor;
        s.defeat=s.valid && !s.preview && result_pose && loser==actor;
    }
    // 43f40/43e0c update type-1c face controllers. Their pool allocation is
    // dynamic; inspect a bounded range and accept only an exact owner, actor
    // ID, model resource, script-table entry and active native speech bit.
    // Missing/ambiguous controllers deliberately leave the raw actor fallback.
    if(ram.size()>=0x13c780) for(unsigned p=0;p<2;++p) {
        auto &s=result[p];
        if(!s.valid || !(word(0x102090)&(0x80000000u>>s.actor))) continue;
        const unsigned fighter=fighter_roster::from_actor(s.actor);
        if(fighter>=mouth_shapes.size()) continue;
        const size_t actor=0x108780+0x2000*p;
        const size_t descriptor=0x80f90+s.actor*20;
        if(word(descriptor+4)!=0x1c) continue;
        const uint32_t scripts=word(descriptor+16);
        if(scripts>0x9ffe8) continue;
        unsigned matches=0;
        for(size_t object=0x10c780;object<0x13c780;object+=0x100) {
            if(word(object+0x8c)!=0x1c || word(object+0xd8)!=actor ||
               half(object+0x7a)!=s.actor || half(object+0x7c)!=word(descriptor)) continue;
            const uint32_t script=word(object+0xb0);
            unsigned id=6;
            for(unsigned n=0;n<6;++n) if(word(scripts+n*4)==script) {id=n;break;}
            const unsigned offset=byte(object+0xbd);
            if(id==6 || (offset&3) || script>0x9ff00 || script+offset+4>0xa0000) continue;
            const unsigned target=byte(script+offset), duration=byte(script+offset+1);
            float blend; const auto bits=word(object+0xc4); std::memcpy(&blend,&bits,4);
            const unsigned previous=offset ? byte(script+offset-4) : 0;
            if(target>=7 || previous>=7 || !duration || !std::isfinite(blend) || blend<0 || blend>1.001f) continue;
            ++matches;
            s.face_script=id; s.face_step=offset/4; s.face_target=target;
            // 43ef0 feeds the preceding morph bank to 4364c. A newly
            // initialized type-1c script starts from bank zero (43c88).
            s.face_previous_target=previous;
            s.face_transition_frames=duration; s.face_hold_frames=byte(object+0xbc);
            s.face_blend=blend;
            const auto &mouth=mouth_shapes[fighter];
            s.mouth_open=mouth[previous]+(mouth[target]-mouth[previous])*(blend>1 ? 1 : blend);
        }
        s.native_face_valid=matches==1;
    }
    return result;
}
}
