#include "vf2_akira.h"
#include "../host/vf2_models.h"

namespace video {
bool Vf2Akira::load(const std::string &zip, std::string &error)
{
    vf1_models::Assets assets;
    if (!vf2_models::load(zip,assets,error)) return false;
    if (!model.load_assets(assets,AkiraSource::VF2,error)) return false;
    model.enabled=true;
    return true;
}
}
