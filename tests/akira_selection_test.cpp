#include "../src/host/akira_selection.h"
#include <cassert>
#include <cstdio>
int main()
{
    std::vector<uint8_t> ram;
    assert(!AkiraSelection::snapshot(ram)[0].human);
    ram.resize(0x800000);
    const uint32_t phase=5;
    std::memcpy(ram.data()+0x108080,&phase,4);
    ram[0x1080c2^3]=13;
    const auto sampled=AkiraSelection::snapshot(ram);
    assert(!sampled[0].human && sampled[1].human && sampled[1].selecting && sampled[1].akira);
    assert(sampled[1].outfit==2 && sampled[1].preview);
    assert(AkiraSelection::previews(sampled,2)==2 && AkiraSelection::previews(sampled,1)==0);
    for(uint32_t confirmation:{13u,11u}) {
        std::memcpy(ram.data()+0x108080,&confirmation,4);
        const auto preview=AkiraSelection::snapshot(ram);
        assert(!preview[1].selecting && AkiraSelection::previews(preview,2)==2);
    }
    auto challenger=sampled;
    challenger[0]={true,false,true,1,false};
    assert(AkiraSelection::previews(challenger,3)==3); // confirmed P1 still has a preview
    const uint32_t fighting=10;
    std::memcpy(ram.data()+0x108080,&fighting,4);
    assert(AkiraSelection::previews(AkiraSelection::snapshot(ram),2)==0);
    ram[0x1087f0^3]=0; ram[0x10a7f0^3]=13;
    assert(AkiraSelection::outfits(ram,1)==1 && AkiraSelection::outfits(ram,2)==2);
    ram[0x1087f0^3]=13; ram[0x10a7f0^3]=0; // swapped costumes
    assert(AkiraSelection::outfits(ram,1)==2 && AkiraSelection::outfits(ram,2)==1);
    ram[0x1087f0^3]=0;
    assert(AkiraSelection::outfits(ram,3)==0); // ambiguous actors stay original
    ram[0x1087f0^3]=7;
    assert(AkiraSelection::outfits(ram,1)==0 && AkiraSelection::outfits(ram,2)==1);
    AkiraSelection choice;
    AkiraSelection::Snapshot s{{{true,true,true},{true,true,true}}};
    m3::Controls c;
    choice.update(c,s,true);
    c.up[0]=true; choice.update(c,s,true);
    assert(choice.mask(s)==1 && !c.up[0]);
    c={}; c.up[0]=true; choice.update(c,s,true); // held: stays selected
    assert(choice.mask(s)==1);
    c={}; choice.update(c,s,true);
    c.down[0]=true; choice.update(c,s,true);
    assert(choice.mask(s)==0 && !c.down[0]);
    c={}; c.down[0]=true; choice.update(c,s,true); // consume until release
    assert(!c.down[0]);
    c={}; choice.update(c,s,true);
    c.down[0]=true; choice.update(c,s,true); // a second down navigates lower row
    assert(c.down[0]);
    c={}; c.up[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==2);
    c={}; c.punch[1]=true; choice.update(c,s,true);
    c={}; c.down[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==2); // confirmation locks the appearance
    s[1].selecting=false;
    c={}; c.up[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==2 && c.up[1]); // fighting up remains a jump
    s[0].akira=false;
    c={}; c.up[0]=true; choice.update(c,s,true);
    assert(c.up[0] && choice.mask(s)==2); // another fighter remains untouched
    s[1].selecting=true;
    c={}; choice.update(c,s,true);
    assert(choice.mask(s)==0); // a new selection starts with VF3
    c.up[1]=true; choice.update(c,s,false);
    assert(c.up[1] && choice.mask(s)==0); // no companion: native inputs
    choice.reset();
    s[1].selecting=false;
    c={}; c.up[1]=true; choice.update(c,s,true);
    s[1].selecting=true;
    c.up[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==0); // held across entry needs a new press
    choice.reset();
    s={{{true,true,true},{true,true,true}}};
    c={}; choice.update(c,s,true,true);
    auto press_up=[&](unsigned player,bool vf1=true,bool vf2=true) {
        c={}; choice.update(c,s,vf1,vf2);
        c.up[player]=true; choice.update(c,s,vf1,vf2);
        assert(!c.up[player]);
    };
    press_up(0); assert(choice.mask(s,1)==1 && choice.mask(s,2)==0);
    press_up(0); assert(choice.mask(s,1)==0 && choice.mask(s,2)==1);
    c={}; c.up[0]=true; choice.update(c,s,true,true);
    assert(choice.mask(s,2)==1); // holding does not rotate again
    press_up(1); assert(choice.mask(s,1)==2 && choice.mask(s,2)==1);
    press_up(0); assert(choice.mask(s,1)==2 && choice.mask(s,2)==0); // VF3
    press_up(0); assert(choice.mask(s,1)==3); // repeat entire cycle
    choice.reset(); c={}; choice.update(c,s,false,true);
    press_up(0,false,true); assert(choice.mask(s,1)==0 && choice.mask(s,2)==1);
    press_up(0,false,true); assert(choice.mask(s,2)==0); // missing VF1 is skipped
    choice.reset(); c={}; choice.update(c,s,true,false);
    press_up(0,true,false); assert(choice.mask(s,1)==1);
    press_up(0,true,false); assert(choice.mask(s,1)==0); // missing VF2 is skipped
    // Final KO clears the human task before replay/You Lose. Preserve only the
    // defeated player's selected appearance while the same battle actors exist.
    for(unsigned player=0;player<2;++player)for(unsigned variant:{1u,2u}) {
        auto start=[&] {
            choice.reset();s={};
            s[player]={true,true,true,player+1,true,player ? 13u : 0u,false};
            s[player^1].actor=20;
            c={};choice.update(c,s,true,true);
            for(unsigned i=0;i<variant;++i)press_up(player);
            s[player].selecting=s[player].preview=false;
            s[player].fighting=true;
            c={};choice.update(c,s,true,true);
            assert(choice.mask(s,variant)==(1u<<player));
        };
        start();
        s[player].human=s[player].fighting=false;
        assert(choice.mask(s,variant)==(1u<<player)); // same frame as final KO
        c={};choice.update(c,s,true,true);
        assert(choice.mask(s,variant)==(1u<<player)); // replay and result pose
        assert(choice.mask(s,3-variant)==0);
        s[player^1].actor=7; // next actor setup, even if Akira's ID remains
        assert(choice.mask(s,variant)==0);
        choice.update(c,s,true,true);
        s[player^1].actor=20; // attract reuses these fighters later
        choice.update(c,s,true,true);
        assert(choice.mask(s,variant)==0);
        start();
        s[player].human=s[player].fighting=false;
        s[player^1].preview=true; // challenger/new character selection
        assert(choice.mask(s,variant)==0);
        start();
        s[player].human=s[player].fighting=false;
        choice.update(c,s,variant!=1,variant!=2);
        assert(choice.mask(s,variant)==0); // removed companion cannot linger
        start();
        s[player].human=s[player].fighting=false;
        choice.reset();
        assert(choice.mask(s,variant)==0);
    }
    puts("Akira selection: cycle, isolation, edges, confirm, navigation, missing ROMs and defeat/attract lifetime passed");
}
