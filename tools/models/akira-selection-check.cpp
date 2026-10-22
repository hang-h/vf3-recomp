// End-to-end selection check using the player's local ROMs, without SDL.
#include "../../src/host/akira_selection.h"
#include "../../src/host/input_script.h"
#include "../../src/video/vf1_akira.h"
#include <cstdio>
#include <stdexcept>
static m3::Model3 board;
static video::SceneBuilder builder;
int main(int argc,char **argv)
{
    if(argc!=7) {fprintf(stderr,"usage: akira-selection-check vf3.zip vf.zip script frames expected-mask expected-replacements\n");return 2;}
    try {
        std::string error;
        if(!board.load(argv[1],error)) throw std::runtime_error(error);
        video::Vf1Akira appearance;
        if(!appearance.load(argv[2],error)) throw std::runtime_error(error);
        appearance.enabled=true;
        InputScript script;
        if(!script.load(argv[3],error)) throw std::runtime_error(error);
        AkiraSelection selection;
        m3::Controls raw;
        const unsigned frames=std::stoul(argv[4]), expected=std::stoul(argv[5]), expected_models=std::stoul(argv[6]);
        unsigned previous=0;
        board.reset();
        for(unsigned f=0;f<frames;++f) {
            script.step(raw,f);
            board.controls=raw;
            selection.update(board.controls,AkiraSelection::snapshot(board.ram),true);
            board.run_frame();
            const auto state=AkiraSelection::snapshot(board.ram);
            const unsigned mask=selection.mask(state);
            if(mask!=previous) {printf("frame %u: mask %u\n",f+1,mask);previous=mask;}
        }
        const unsigned mask=selection.mask(AkiraSelection::snapshot(board.ram));
        if(mask!=expected) throw std::runtime_error("unexpected final player selection mask");
        video::Scene original;builder.build(board,original);
        auto changed=original;
        const size_t count=appearance.apply(changed,AkiraSelection::outfits(board.ram,mask));
        if(count!=expected_models) throw std::runtime_error("unexpected per-player replacement count");
        const unsigned outfits=AkiraSelection::outfits(board.ram,mask);
        for(size_t v=0;v<original.viewports.size();++v)
            for(size_t m=0;m<original.viewports[v].models.size();++m) {
                const auto &a=original.viewports[v].models[m], &b=changed.viewports[v].models[m];
                const unsigned outfit=a.addr==0x9aaccf ? 1 : a.addr==0xa47386 ? 2 : 0;
                if(outfit && bool(outfits & outfit)!=(a.meshes!=b.meshes))
                    throw std::runtime_error("wrong player's Akira head was replaced");
            }
        if(!mask) for(size_t v=0;v<original.viewports.size();++v)
            for(size_t m=0;m<original.viewports[v].models.size();++m)
                if(original.viewports[v].models[m].meshes!=changed.viewports[v].models[m].meshes)
                    throw std::runtime_error("unselected scene changed");
        printf("frame %u: selection mask %u; replaced %zu fighter(s)\n",frames,mask,count);
    } catch(const std::exception &e) {fprintf(stderr,"%s\n",e.what());return 1;}
}
