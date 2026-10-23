// Original companion-ROM artwork composited into the matching VF3 roster cell.
// This only changes the published picture, never the emulated tile memory.
#pragma once
#include "vf1_models.h"
#include "fighter_roster.h"
#include <span>
#include <algorithm>
#include <array>

namespace akira_portrait {
inline bool draw(std::span<uint32_t> above, unsigned width, unsigned height,
                 const vf1_models::Portrait &picture, unsigned fighter=0)
{
    if(fighter>=fighter_roster::count)return false;
    // Native NAME-text/cursor captures confirm the canonical roster occupies
    // cells 0..9. Pixel boundaries are measured at VF3's 496x384 output size;
    // the middle columns are one pixel wider than the outer columns.
    constexpr unsigned origins[]={33,104,176,248,320,392};
    constexpr unsigned widths[]={67,68,68,68,68,67};
    const unsigned cell=fighter_roster::grid_cells[fighter],column=cell%6;
    const unsigned x0=origins[column],y0=273+47*(cell/6),w=widths[column];
    constexpr unsigned h=42;
    if(!picture.available() || width<x0+w || height<y0+h || above.size()!=size_t(width)*height)
        return false;
    unsigned cx=0,cy=8,cw=56,ch=56; // Keep VF1's headband and whole face.
    if(picture.width==48 && picture.height==88) {cx=1;cy=24;cw=46;ch=60;}
    else if(picture.width!=56 || picture.height!=64) return false;
    // These source portraits are taller than VF3's landscape cell. Fit the
    // face without stretching it or cropping away the headband/chin.
    // Preserve the native blinking player marker, including its dark outline,
    // without leaving a rectangular fragment of the old VF3 portrait behind.
    // Use the exact marker ink, not similar reds in Jacky's portrait background.
    std::array<bool,68*h> marker{};
    for(unsigned y=28;y<h;++y)for(unsigned x=0;x<16;++x) {
        const auto pixel=above[size_t(y0+y)*width+x0+x];
        const unsigned r=pixel&255,g=(pixel>>8)&255,b=(pixel>>16)&255;
        if(!(pixel>>24) || !((r==255 && g==0 && b==0) || (b==255 && r==0 && g==0))) continue;
        marker[y*w+x]=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int nx=int(x)+dx,ny=int(y)+dy;
            if(nx<0 || nx>=int(w) || ny<0 || ny>=int(h))continue;
            const auto edge=above[size_t(y0+ny)*width+x0+nx];
            if((edge>>24) && (edge&255)<64 && ((edge>>8)&255)<64 && ((edge>>16)&255)<64)
                marker[ny*w+nx]=true;
        }
    }
    const unsigned dw=std::min(w,h*cw/ch),left=(w-dw)/2;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        if(marker[y*w+x]) continue;
        uint32_t argb=0xff10131bu;
        if(x>=left && x<left+dw)
            argb=picture.pixels[(cy+y*ch/h)*picture.width+cx+(x-left)*cw/dw];
        above[size_t(y0+y)*width+x0+x]=(argb&0xff00ff00u)|((argb&255)<<16)|((argb>>16)&255);
    }
    return true;
}
}
