#pragma once
#include <array>

namespace fighter_roster {
inline constexpr unsigned count=10;
inline constexpr std::array<const char *,count> names={
    "AKIRA","PAI","LAU","WOLF","JEFFRY","KAGE","SARAH","JACKY","SHUN","LION"
};
// VF3's actor IDs differ from the companion ROMs' select-screen order.
inline constexpr std::array<unsigned,count> game_ids={0,6,4,7,5,3,2,1,8,10};
inline constexpr std::array<unsigned,count> grid_cells={0,1,2,3,4,5,6,7,8,9};
constexpr unsigned from_actor(unsigned id)
{
    if(id>=26)return 255;
    for(unsigned f=0;f<count;++f)if(id%13==game_ids[f])return f;
    return 255;
}
constexpr bool available(unsigned fighter,unsigned variant)
{
    return variant==0 || (variant==1 && fighter<8) || (variant==2 && fighter<count);
}
}
