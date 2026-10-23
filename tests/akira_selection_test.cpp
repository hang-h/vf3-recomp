#include "../src/host/akira_selection.h"
#include "../src/host/akira_portrait.h"
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
    assert(choice.mask(s)==1 && c.down[0]); // navigate directly even with VF1 selected
    s[0].fighter=6;s[0].akira=false; // native DOWN moved Akira -> Sarah
    c={};c.down[0]=true;choice.update(c,s,true);
    assert(choice.mask(s)==0 && !c.down[0]); // held direction cannot cycle on row entry
    c={};choice.update(c,s,true);
    c.down[0]=true;choice.update(c,s,true);
    assert(choice.mask(s)==1 && !c.down[0]); // fresh DOWN selects Sarah VF1
    c={};c.up[0]=true;choice.update(c,s,true);
    assert(choice.mask(s)==1 && c.up[0]); // UP navigates straight back
    s[0].fighter=0;s[0].akira=true;
    c={};c.up[0]=true;choice.update(c,s,true);
    assert(choice.mask(s)==0 && !c.up[0]); // no accidental top-row cycle
    c={}; c.up[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==2);
    c={}; c.punch[1]=true; choice.update(c,s,true);
    c={}; c.down[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==2); // confirmation locks the appearance
    s[1].selecting=false;
    c={}; c.up[1]=true; choice.update(c,s,true);
    assert(choice.mask(s)==2 && c.up[1]); // fighting up remains a jump
    s[0].akira=false; s[0].fighter=255;
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
    auto press_cycle=[&](unsigned player,bool vf1=true,bool vf2=true) {
        c={}; choice.update(c,s,vf1,vf2);
        const bool bottom=fighter_roster::cycle_down(s[player].fighter);
        (bottom ? c.down[player] : c.up[player])=true; choice.update(c,s,vf1,vf2);
        assert(!(bottom ? c.down[player] : c.up[player]));
    };
    press_cycle(0); assert(choice.mask(s,1)==1 && choice.mask(s,2)==0);
    press_cycle(0); assert(choice.mask(s,1)==0 && choice.mask(s,2)==1);
    c={}; c.up[0]=true; choice.update(c,s,true,true);
    assert(choice.mask(s,2)==1); // holding does not rotate again
    press_cycle(1); assert(choice.mask(s,1)==2 && choice.mask(s,2)==1);
    press_cycle(0); assert(choice.mask(s,1)==2 && choice.mask(s,2)==0); // VF3
    press_cycle(0); assert(choice.mask(s,1)==3); // repeat entire cycle
    choice.reset(); c={}; choice.update(c,s,false,true);
    press_cycle(0,false,true); assert(choice.mask(s,1)==0 && choice.mask(s,2)==1);
    press_cycle(0,false,true); assert(choice.mask(s,2)==0); // missing VF1 is skipped
    choice.reset(); c={}; choice.update(c,s,true,false);
    press_cycle(0,true,false); assert(choice.mask(s,1)==1);
    press_cycle(0,true,false); assert(choice.mask(s,1)==0); // missing VF2 is skipped
    // Final KO clears the human task before replay/You Lose. Preserve only the
    // defeated player's selected appearance while the same battle actors exist.
    for(unsigned player=0;player<2;++player)for(unsigned variant:{1u,2u}) {
        auto start=[&] {
            choice.reset();s={};
            s[player]={true,true,true,player+1,true,player ? 13u : 0u,false};
            s[player^1].actor=20;
            c={};choice.update(c,s,true,true);
            for(unsigned i=0;i<variant;++i)press_cycle(player);
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
    // Every shared fighter cycles only through editions in which it exists.
    for(unsigned fighter=0;fighter<fighter_roster::count;++fighter)for(unsigned player=0;player<2;++player) {
        choice.reset();s={};
        const unsigned game=fighter_roster::game_ids[fighter];
        s[player]={true,true,fighter==0,player+1,true,game+13*player,false,fighter};
        s[player^1].actor=12;
        c={};choice.update(c,s,true,true);
        press_cycle(player);
        const unsigned first=fighter<8 ? 1 : 2;
        assert(choice.mask(s,first)==(1u<<player) && choice.mask(s,3-first)==0);
        if(fighter<8) {press_cycle(player);assert(choice.mask(s,2)==(1u<<player));}
        press_cycle(player);assert(choice.mask(s,1)==0 && choice.mask(s,2)==0);
        press_cycle(player);assert(choice.mask(s,first)==(1u<<player));
        // Each query addresses only this fighter's preview, never another one.
        assert(AkiraSelection::previews(s,1u<<player,fighter)==player+1);
        assert(AkiraSelection::previews(s,1u<<player,(fighter+1)%10)==0);
        s[player].selecting=s[player].preview=false;s[player].fighting=true;
        c={};choice.update(c,s,true,true);
        s[player].human=s[player].fighting=false;
        choice.update(c,s,true,true);assert(choice.mask(s,first)==(1u<<player));
        // Same actors with a different cursor/owner cannot retain the result.
        s[player].fighter=(fighter+1)%10;
        assert(choice.mask(s,first)==0);
        choice.update(c,s,true,true);
        s[player].fighter=fighter;
        assert(choice.mask(s,first)==0);
        // Verify native RAM decoding and alternate costume routing for both sides.
        const size_t base=0x108000+0x80*player;
        std::memcpy(ram.data()+base,&phase,4);
        ram[(base+0x42)^3]=uint8_t(game+13*player);
        ram[(0x1087f0+0x2000*player)^3]=uint8_t(game+13*player);
        ram[(0x1087f0+0x2000*(player^1))^3]=12;
        const auto state=AkiraSelection::snapshot(ram);
        assert(state[player].fighter==fighter && state[player].akira==(fighter==0));
        assert(state[player].outfit==player+1);
        assert(AkiraSelection::outfits(ram,1u<<player,fighter)==player+1);
        assert(AkiraSelection::outfits(ram,1u<<player,(fighter+1)%10)==0);
    }
    // Players on opposite rows cycle independently; simultaneous directions do not cycle.
    choice.reset();s={{{true,true,true},{true,true,false,2,true,15,false,6}}};
    c={};choice.update(c,s,true,true);
    c.up[0]=true;c.down[1]=true;choice.update(c,s,true,true);
    assert(choice.mask(s,1)==3 && !c.up[0] && !c.down[1]);
    c={};c.up[0]=true;c.down[1]=true;choice.update(c,s,true,true);
    assert(choice.mask(s,1)==3); // held input is one press on both rows
    c={};choice.update(c,s,true,true);
    c.up[0]=c.down[0]=c.up[1]=c.down[1]=true;choice.update(c,s,true,true);
    assert(choice.mask(s,1)==3 && c.down[0] && c.up[1]);
    s[1].selecting=false;c={};c.down[1]=true;choice.update(c,s,true,true);
    assert(c.down[1]); // fighting DOWN remains a crouch
    // Cursor movement resets a locked choice, and the other player's edition survives.
    choice.reset();s={{{true,true,true},{true,true,true}}};
    c={};choice.update(c,s,true,true);press_cycle(0);press_cycle(1);
    c={};c.punch[0]=true;choice.update(c,s,true,true);
    s[0].fighter=1;s[0].akira=false;
    c={};choice.update(c,s,true,true);assert(choice.mask(s,1)==2);
    press_cycle(0);assert(choice.mask(s,1)==3);
    // Unsupported VF3-only fighters leave every control native.
    for(unsigned game=0;game<26;++game)if(fighter_roster::from_actor(game)==255) {
        ram[0x108042^3]=uint8_t(game);std::memcpy(ram.data()+0x108000,&phase,4);
        s=AkiraSelection::snapshot(ram);choice.reset();c={};choice.update(c,s,true,true);
        c.up[0]=true;c.down[0]=true;choice.update(c,s,true,true);
        assert(c.up[0] && c.down[0] && choice.mask(s,1)==0 && choice.mask(s,2)==0);
        c={};c.up[0]=true;choice.update(c,s,true,true);assert(c.up[0]);
    }
    // Shun/Lion have no VF1 edition: with only VF1 installed, both directions stay native.
    for(unsigned fighter:{8u,9u}) {
        choice.reset();s={};s[0]={true,true,false,1,true,255,false,fighter};
        c={};choice.update(c,s,true,false);c.up[0]=true;choice.update(c,s,true,false);
        assert(c.up[0] && choice.mask(s,1)==0 && choice.mask(s,2)==0);
        c={};c.down[0]=true;choice.update(c,s,true,false);assert(c.down[0]);
    }
    // Composite only the selected fighter's measured cell, retaining marker ink.
    constexpr unsigned width=496,height=384;
    constexpr unsigned origins[]={33,104,176,248,320,392};
    constexpr unsigned widths[]={67,68,68,68,68,67};
    vf1_models::Portrait picture;picture.width=56;picture.height=64;
    picture.pixels.assign(56*64,0xffa06020);
    for(unsigned fighter=0;fighter<10;++fighter) {
        std::vector<uint32_t> pixels(width*height,0xff123456);
        const unsigned x0=origins[fighter%6],y0=273+47*(fighter/6),w=widths[fighter%6];
        pixels[(y0+30)*width+x0+3]=0xff0000ff; // 1P's exact red ink (ABGR)
        pixels[(y0+30)*width+x0+4]=0xff000000; // black marker outline
        pixels[(y0+32)*width+x0+5]=0xff0008b4; // red portrait background, not marker
        const auto before=pixels;
        assert(akira_portrait::draw(pixels,width,height,picture,fighter));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const size_t i=size_t(y)*width+x;
            if(x<x0 || x>=x0+w || y<y0 || y>=y0+42)assert(pixels[i]==before[i]);
        }
        assert(pixels[(y0+30)*width+x0+3]==0xff0000ff);
        assert(pixels[(y0+30)*width+x0+4]==0xff000000);
        assert(pixels[(y0+32)*width+x0+5]!=before[(y0+32)*width+x0+5]);
        assert(pixels[(y0+20)*width+x0+w/2]==0xff2060a0); // ARGB to ABGR
    }
    std::vector<uint32_t> untouched(width*height,0xff123456);
    const auto original_pixels=untouched;
    for(unsigned unsupported:{10u,11u,255u})assert(!akira_portrait::draw(untouched,width,height,picture,unsupported));
    assert(untouched==original_pixels);
    assert(!akira_portrait::draw(untouched,100,100,picture,1));
    picture.width=48;picture.height=88;picture.pixels.assign(48*88,0xffa06020);
    assert(akira_portrait::draw(untouched,width,height,picture,9));
    puts("Roster selection: all fighters, cycle, isolation, edges, confirm, navigation, missing editions and defeat/attract lifetime passed");
}
