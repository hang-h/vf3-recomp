#include "../src/video/vf2_body_fit.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
int main()
{
    try {
        for(float hem:{-.03f,0.f,.03f})for(float belt:{-.02f,.02f,.04f}) {
            vf1_models::Mesh torso={{0xff123456,{{{hem,.1f,.1f},{.2f,.12f,.13f},{.3f,.1f,.1f}}}}};
            vf1_models::Mesh pelvis={{0xffabcdef,{{{belt,.1f,.1f},{.2f,.1f,.1f},{.2f,-.1f,-.1f}}}}};
            const auto original=torso,original_pelvis=pelvis;
            video::fit_vf2_waist(torso,pelvis,.28f);
            require(torso.size()==original.size() && torso[0].colour==original[0].colour,"fit changed texture/topology");
            require(torso[0].points[1]==original[0].points[1] && torso[0].points[2]==original[0].points[2],
                    "fit changed chest/shoulder anchors");
            require(torso[0].points[0][1]==.1f && torso[0].points[0][2]==.1f,"fit widened waist");
            require(torso[0].points[0][0]+belt<=-.045f+1e-6f,"waist pieces lack bending overlap");
            require(pelvis[0].points==original_pelvis[0].points,"fit changed pelvis");
            // Fitting is idempotent, including a hem that already covers the belt.
            const auto fitted=torso;
            video::fit_vf2_waist(torso,pelvis,.28f);
            require(std::abs(torso[0].points[0][0]-fitted[0].points[0][0])<1e-6f,"fit extended a covered hem again");
        }
        std::puts("VF2 waist fit: overlap, fixed shoulders, unchanged pelvis/colours and idempotence passed");
    }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
