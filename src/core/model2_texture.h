// SPDX-License-Identifier: BSD-3-Clause
// Model 2 sampling follows src/host/vf2_models.cpp and MAME sega/model2_v.cpp,
// copyright R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels.
// Immutable Model 2 index tile and its native luminance-to-colour lookup.
// Shared by the source decoder, reference rasterizer and GPU upload path.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

struct Model2Texture {
    unsigned width=0,height=0,flags=0;
    // 128 ARGB colours, followed by width*height four-bit indices as words.
    std::vector<uint32_t> words;
    uint32_t sample(float u,float v) const {
        auto axis=[](float coordinate,unsigned size,bool mirror,bool wrap) {
            int fixed=int(coordinate*256);
            if(mirror && (fixed&int(size<<8)))fixed=~fixed;
            fixed-=128;
            unsigned fraction=unsigned(fixed)&255;
            unsigned a=unsigned(fixed>>8)&(size-1),b=(a+1)&(size-1);
            if(!wrap && b==0) {
                if(fraction>=128){a=b;++b;fraction=0;}
                else {b=a;--a;fraction=256;}
            }
            return std::array<unsigned,3>{a,b,fraction};
        };
        auto texel=[&](unsigned x,unsigned y){return words[128+(y&(height-1))*width+(x&(width-1))];};
        if(!(flags&0x10000)) {
            unsigned x=unsigned(u),y=unsigned(v);
            if((flags&0x100) && (x&width))x=~x;
            if((flags&0x200) && (y&height))y=~y;
            const auto index=texel(x,y);
            return (flags&0x2000) && index==15 ? 0 : words[index*8];
        }
        const auto x=axis(u,width,flags&0x100,(flags&0x40) && !(flags&0x100));
        const auto y=axis(v,height,flags&0x200,(flags&0x80) && !(flags&0x200));
        auto packed=[&](unsigned a,unsigned b){const auto value=texel(a,b)<<4;return value==240?value:value|0x00800000u;};
        auto lerp=[](uint32_t a,uint32_t b,unsigned f){return (a+(((b-a)*f)>>8))&0x00ff00ffu;};
        uint32_t a=packed(x[0],y[0]),b=packed(x[1],y[0]),c=packed(x[0],y[1]),d=packed(x[1],y[1]);
        if(a==240)a=b&255;if(b==240)b=a&255;
        if(c==240)c=d&255;if(d==240)d=c&255;
        a=lerp(a,b,x[2]);c=lerp(c,d,x[2]);
        if(a==240)a=c&255;if(c==240)c=a&255;
        const auto value=lerp(a,c,y[2]);
        return value<0x00400000 ? 0 : words[(value&255)>>1];
    }
};
