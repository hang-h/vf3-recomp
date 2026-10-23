// Fit the rigid VF2 waist pieces to VF3's independently bending torso/pelvis.
#pragma once
#include "../host/vf1_models.h"
#include <algorithm>
#include <limits>

namespace video {
inline void fit_vf2_waist(vf1_models::Mesh &torso, const vf1_models::Mesh &pelvis,
                         float shoulder_height)
{
    if(torso.empty() || pelvis.empty() || shoulder_height<=0)return;
    float hem=std::numeric_limits<float>::max(),belt=hem;
    for(const auto &t:torso)for(const auto &p:t.points)hem=std::min(hem,p[0]);
    for(const auto &t:pelvis)for(const auto &p:t.points)belt=std::min(belt,p[0]);
    // The two local X axes point away from their shared waist joint. Leave
    // overlap for bending instead of meeting at a single plane. Measure each
    // fighter after skeleton fitting; long hems that already overlap stay put.
    const float extension=std::max(0.f,hem+belt+.045f);
    const float fade=shoulder_height*.55f;
    if(extension==0 || fade<=hem)return;
    for(auto &t:torso)for(auto &p:t.points)
        p[0]-=extension*std::clamp((fade-p[0])/(fade-hem),0.f,1.f);
}
}
