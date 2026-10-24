// VF2 roster geometry decoded from the user's original vf2.zip.
#pragma once
#include "vf1_models.h"

namespace vf2_models {
// Shares the semantic body/accessory container; vertices remain in VF2 local units.
// ZIP member sizes and CRCs are checked. Failure leaves out unchanged.
// Runtime keeps original UV triangles and indexed materials. textured=false
// retains the subdivided-colour reference path for local decoder regressions.
bool load(const std::string &zip, vf1_models::Assets &out, std::string &error, unsigned fighter=0, bool textured=true, unsigned outfit=0);
}
