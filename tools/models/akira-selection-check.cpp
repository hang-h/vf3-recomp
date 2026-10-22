// End-to-end selection check using the player's local ROMs, without SDL.
#include "../../src/host/akira_selection.h"
#include "../../src/host/input_script.h"
#include "../../src/video/vf1_akira.h"
#include "../../src/video/vf2_akira.h"
#include <cstdio>
#include <stdexcept>
static m3::Model3 board;
static video::SceneBuilder builder;
int main(int argc,char **argv)
{
    if(argc!=7 && argc!=10) {fprintf(stderr,"usage: akira-selection-check vf3.zip vf.zip script frames expected-mask expected-replacements [vf2.zip expected-vf2-mask expected-vf2-replacements]\n");return 2;}
    try {
        std::string error;
        if(!board.load(argv[1],error)) throw std::runtime_error(error);
        video::Vf1Akira appearance;
        if(!appearance.load(argv[2],error)) throw std::runtime_error(error);
        appearance.enabled=true;
        video::Vf2Akira second;
        if(argc==10 && !second.load(argv[7],error)) throw std::runtime_error(error);
        second.enabled=second.ready();
        const unsigned expected2=argc==10 ? std::stoul(argv[8]) : 0;
        const unsigned expected_models2=argc==10 ? std::stoul(argv[9]) : 0;
        InputScript script;
        if(!script.load(argv[3],error)) throw std::runtime_error(error);
        AkiraSelection selection;
        m3::Controls raw;
        const unsigned frames=std::stoul(argv[4]), expected=std::stoul(argv[5]), expected_models=std::stoul(argv[6]);
        unsigned previous=0,previous2=0;
        board.reset();
        for(unsigned f=0;f<frames;++f) {
            script.step(raw,f);
            board.controls=raw;
            selection.update(board.controls,AkiraSelection::snapshot(board.ram),true,second.ready());
            board.run_frame();
            const auto state=AkiraSelection::snapshot(board.ram);
            const unsigned mask=selection.mask(state), mask2=selection.mask(state,2);
            if(mask!=previous || mask2!=previous2) {printf("frame %u: VF1 mask %u, VF2 mask %u\n",f+1,mask,mask2);previous=mask;previous2=mask2;}
        }
        const auto state=AkiraSelection::snapshot(board.ram);
        const unsigned mask=selection.mask(state);
        const unsigned mask2=selection.mask(state,2);
        if(mask2!=expected2) throw std::runtime_error("unexpected VF2 selection mask");
        if(mask!=expected) throw std::runtime_error("unexpected final player selection mask");
        video::Scene original;builder.build(board,original);
        auto changed=original;
        const size_t count=appearance.apply(changed,AkiraSelection::outfits(board.ram,mask),AkiraSelection::previews(state,mask));
        if(count!=expected_models) throw std::runtime_error("unexpected per-player replacement count");
        const auto vf1_scene=changed;
        const size_t count2=second.apply(changed,AkiraSelection::outfits(board.ram,mask2),AkiraSelection::previews(state,mask2));
        if(count2!=expected_models2) throw std::runtime_error("unexpected VF2 replacement count");
        const unsigned vf1_outfits=AkiraSelection::outfits(board.ram,mask)|AkiraSelection::previews(state,mask);
        const unsigned outfits=vf1_outfits | AkiraSelection::outfits(board.ram,mask2)|AkiraSelection::previews(state,mask2);
        for(size_t v=0;v<original.viewports.size();++v)
            for(size_t m=0;m<original.viewports[v].models.size();++m) {
                const auto &a=original.viewports[v].models[m], &b=changed.viewports[v].models[m];
                const unsigned outfit=a.addr==0x9aaccf ? 1 : a.addr==0xa47386 ? 2 : 0;
                if ((outfit & vf1_outfits) && vf1_scene.viewports[v].models[m].meshes!=b.meshes)
                    throw std::runtime_error("VF2 replacement overwrote VF1 player");
                if(outfit && bool(outfits & outfit)!=(a.meshes!=b.meshes))
                    throw std::runtime_error("wrong player's Akira head was replaced");
            }
        if(!(mask|mask2)) for(size_t v=0;v<original.viewports.size();++v)
            for(size_t m=0;m<original.viewports[v].models.size();++m)
                if(original.viewports[v].models[m].meshes!=changed.viewports[v].models[m].meshes)
                    throw std::runtime_error("unselected scene changed");
        printf("frame %u: VF1 mask %u (%zu fighter), VF2 mask %u (%zu fighter)\n",frames,mask,count,mask2,count2);
    } catch(const std::exception &e) {fprintf(stderr,"%s\n",e.what());return 1;}
}
