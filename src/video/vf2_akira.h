// Optional VF2 Akira appearance, using the same guarded VF3 skeleton retargeter.
#pragma once
#include "vf1_akira.h"

namespace video {
class Vf2Akira {
public:
    bool load(const std::string &zip, std::string &error);
    bool ready() const { return model.ready(); }
    bool enabled = false;
    size_t apply(Scene &scene, unsigned outfit_mask = 3) const
    {
        return enabled ? model.apply(scene,outfit_mask) : 0;
    }
private:
    Vf1Akira model;
};
}
