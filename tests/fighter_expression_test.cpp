// Native RAM sampling guards; no game data or controller-input heuristics.
#include "../src/host/fighter_expression_pose.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
}
int main()
{
    try {
        std::vector<uint8_t> ram(0x200000);
        auto word=[&](size_t at,uint32_t value){std::memcpy(ram.data()+at,&value,4);};
        auto byte=[&](size_t at,uint8_t value){ram[at^3]=value;};
        auto half=[&](size_t at,uint16_t value){std::memcpy(ram.data()+(at^2),&value,2);};
        word(0x108000,10);word(0x108080,10);
        byte(0x1087f0,6);byte(0x10a7f0,19);
        word(0x1087c8,0x0a0006c6);word(0x1087c4,0x01000000);
        word(0x1087cc,0x06c60006);half(0x1087d6,161);
        word(0x10a7cc,0x074f0002);half(0x10a7d6,180);
        auto state=fighter_expression::snapshot(ram);
        require(state[0].valid && state[0].actor==6 && state[1].actor==19,"actor/outfit sampling failed");
        require(state[0].action_category==10 && state[0].animation==0x6c6 &&
                state[0].animation_frame==6 && state[0].hp==161,"native endian/action timing mismatch");
        require(state[1].animation==0x74f && state[1].animation_frame==2 && state[1].hp==180,
                "player animation states are not independent");
        byte(0x102008,7);byte(0x10200b,14);word(0x1061c4,0x10a780);word(0x1061c8,0x108780);
        state=fighter_expression::snapshot(ram);
        require(state[0].defeat && !state[0].victory && state[1].victory && !state[1].defeat,
                "result pose mapped to wrong actor");
        word(0x108000,0);byte(0x10200b,15);
        require(fighter_expression::snapshot(ram)[0].defeat,"inactive losing player lost result expression");
        for(unsigned sub:{0u,13u,16u,255u}) {
            byte(0x10200b,uint8_t(sub));state=fighter_expression::snapshot(ram);
            require(!state[0].defeat && !state[1].victory,"stale result pointers affect replay or next round");
        }
        byte(0x10200b,15);byte(0x102008,6);
        require(!fighter_expression::snapshot(ram)[1].victory,"result state crossed main game phase");
        byte(0x102008,7);word(0x1061c4,0x108780);word(0x1061c8,0x10a780);
        state=fighter_expression::snapshot(ram);
        require(state[0].victory && state[1].defeat,"swapped winner/loser was not sampled");
        word(0x1061c8,0x108780);
        require(!fighter_expression::snapshot(ram)[0].victory,"ambiguous winner/loser pointers accepted");
        word(0x1061c8,0x10a780);word(0x108000,10);word(0x108080,5);
        byte(0x108042,2);byte(0x1080c2,15);
        state=fighter_expression::snapshot(ram);
        require(state[0].preview && state[1].preview && state[0].actor==2 && state[1].actor==15,
                "confirmed opponent did not use selection preview identity");
        require(!state[0].victory && !state[1].defeat,"stale battle state overrides selection preview");
        byte(0x1080c2,255);
        require(!fighter_expression::snapshot(ram)[1].valid,"invalid cursor accepted");
        // The inactive slot can retain the same actor and a stale hurt state.
        // Both poses then address the same preview head unless it is excluded.
        word(0x108000,5);word(0x108080,0);
        byte(0x108042,0);byte(0x10a7f0,0);word(0x10a7c8,0x0a0006c6);
        state=fighter_expression::snapshot(ram);
        fighter_expression::Tracker preview_tracker;preview_tracker.update(state);
        require(state[0].valid && !state[1].valid &&
                preview_tracker.poses()[0].kind==fighter_expression::Kind::Neutral &&
                !preview_tracker.poses()[1].valid,"inactive player overwrote the neutral selection face");
        word(0x108000,10);
        require(fighter_expression::snapshot(ram)[1].valid,"phase-zero CPU lost its battle expression");
        word(0x108000,0);word(0x108080,5);byte(0x1080c2,13);
        state=fighter_expression::snapshot(ram);
        require(!state[0].valid && state[1].valid,"inactive first player overwrote the second player's preview");
        for(size_t size:{size_t(0),size_t(1024),size_t(0x108100)}) {
            state=fighter_expression::snapshot(std::span<const uint8_t>(ram).first(size));
            require(!state[0].valid && !state[1].valid,"truncated native RAM accepted");
        }
        // An active face controller must match its owner, costume, resource,
        // script table and voice bit. Morph IDs are not an ordered jaw scale.
        word(0x108000,10);word(0x108080,10);byte(0x1087f0,0);
        word(0x102090,0x80000000);word(0x80f90,0x1234);word(0x80f94,0x1c);word(0x80fa0,0x80000);
        word(0x80000,0x80100);byte(0x80100,2);byte(0x80101,6);
        const size_t object=0x10c780;
        word(object+0x8c,0x1c);word(object+0xd8,0x108780);half(object+0x7a,0);half(object+0x7c,0x1234);
        word(object+0xb0,0x80100);byte(object+0xbd,0);
        float blend=.5f;uint32_t bits;std::memcpy(&bits,&blend,4);word(object+0xc4,bits);
        state=fighter_expression::snapshot(ram);
        require(state[0].native_face_valid && !state[1].native_face_valid &&
                std::abs(state[0].mouth_open-.5f)<1e-5f,"native jaw blend/ownership mismatch");
        byte(0x80100,6);state=fighter_expression::snapshot(ram);
        require(std::abs(state[0].mouth_open-.4841835f)<1e-5f,"morph IDs were incorrectly treated as ordinal opening");
        std::memcpy(ram.data()+object+0x100,ram.data()+object,0x100);
        require(!fighter_expression::snapshot(ram)[0].native_face_valid,"duplicate face controller accepted");
        std::memset(ram.data()+object+0x100,0,0x100);word(0x102090,0);
        require(!fighter_expression::snapshot(ram)[0].native_face_valid,"inactive voice uses stale face controller");
        using fighter_expression::Kind;
        fighter_expression::Tracker tracker;fighter_expression::Snapshot samples{};
        samples[0].valid=true;samples[0].actor=6;samples[0].preview=true;samples[0].phase=13;
        samples[1].valid=true;samples[1].actor=19;samples[1].action_category=10;
        samples[1].action=0x0a0006c6;samples[1].animation=0x6c6;
        for(unsigned tick=0;tick<8;++tick)tracker.update(samples);
        require(tracker.poses()[0].kind==Kind::Selection && tracker.poses()[0].tick==7 &&
                tracker.poses()[1].kind==Kind::Hurt,"per-player visual state timing mismatch");
        samples[0].preview=false;samples[0].victory=true;samples[0].animation_frame=1;
        for(unsigned tick=0;tick<20;++tick)tracker.update(samples);
        require(tracker.poses()[0].kind==Kind::Victory && tracker.poses()[0].tick==19,
                "frozen native body motion froze the victory expression");
        samples[1].action_category=1;tracker.update(samples);
        require(tracker.poses()[1].kind==Kind::HurtRecovery,"hit expression snapped directly to neutral");
        for(unsigned tick=0;tick<40;++tick)tracker.update(samples);
        require(tracker.poses()[1].kind==Kind::Neutral,"hit recovery never completed");
        samples[0].actor=2;samples[0].victory=false;tracker.update(samples);
        require(tracker.poses()[0].kind==Kind::Neutral && tracker.poses()[0].tick==0,
                "new actor inherited the previous fighter's expression");
        samples[0].native_face_valid=true;samples[0].mouth_open=.7f;tracker.update(samples);
        require(tracker.poses()[0].speaking && tracker.poses()[0].mouth_open==.7f,"native speech blend was discarded");
        samples[0].native_face_valid=false;samples[0].action_category=10;tracker.update(samples);
        samples[0].preview=true;samples[0].phase=5;tracker.update(samples);
        require(tracker.poses()[0].kind==Kind::Neutral,"hover preview inherited battle hit recovery");
        tracker.reset();require(!tracker.poses()[0].valid,"reset retained actor expression");
        std::puts("Native facial sampling: endian fields, two-player timing, preview ownership, guarded result phases and short RAM passed");
    }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
