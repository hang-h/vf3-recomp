// SPDX-License-Identifier: BSD-3-Clause
// Model 2 strip/texture format follows MAME sega/model2_v.cpp and model2rd.ipp,
// copyright R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels,
// as documented in vf2-recomp/src/machine/model2_video.cpp.
#include "vf2_models.h"
#include "rom_zip.h"
#include "vf2_texture_decode.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vf2_models {
namespace {
using Point = std::array<float, 3>;
using UV = std::array<float, 2>;
using Mesh = vf1_models::Mesh;
enum { Main, Polygons, Textures, CPU, RegionCount };
struct Rom { const char *name; uint32_t crc; int region; size_t offset, size; };
constexpr Rom roms[] = {
    {"mpr-17560.10",0xd1389864,Main,0,0x200000}, {"mpr-17561.11",0xb98d0101,Main,2,0x200000},
    {"mpr-17558.8",0x4b15f5a6,Main,0x400000,0x200000}, {"mpr-17559.9",0xd3264de6,Main,0x400002,0x200000},
    {"mpr-17566.6",0xfb41ef98,Main,0x800000,0x200000}, {"mpr-17567.7",0xc3396922,Main,0x800002,0x200000},
    {"mpr-17564.4",0xd8062489,Main,0xc00000,0x200000}, {"mpr-17565.5",0x0517c6e9,Main,0xc00002,0x200000},
    {"mpr-17554.16",0x27896d82,Polygons,0,0x200000}, {"mpr-17548.20",0xc95facc2,Polygons,2,0x200000},
    {"mpr-17555.17",0x4df2810b,Polygons,0x400000,0x200000}, {"mpr-17549.21",0xe0bce0e6,Polygons,0x400002,0x200000},
    {"mpr-17556.18",0x41a47616,Polygons,0x800000,0x200000}, {"mpr-17550.22",0xc36ff3f5,Polygons,0x800002,0x200000},
    {"mpr-17553.25",0x5da1c5d3,Textures,0,0x200000}, {"mpr-17552.24",0xe91e7427,Textures,2,0x200000},
    {"mpr-17547.27",0xbe940431,Textures,0x800000,0x200000}, {"mpr-17546.26",0x042a194b,Textures,0x800002,0x200000},
    {"epr-18385.12",0x78ed2d41,CPU,0,0x20000}, {"epr-18386.13",0x3418f428,CPU,2,0x20000},
    {"epr-18387.14",0x124a8453,CPU,0x40000,0x20000}, {"epr-18388.15",0x8d347980,CPU,0x40002,0x20000},
};
struct Decoder {
    std::array<std::vector<uint8_t>, RegionCount> regions;
    std::vector<uint16_t> sheet;
    const uint8_t *bytes(int region, size_t address, size_t count) const {
        const auto &r=regions[region];
        if(address>r.size() || count>r.size()-address) throw std::runtime_error("VF2 model address outside ROM");
        return r.data()+address;
    }
    uint32_t word(int r,size_t a) const { return rom_zip::le32(bytes(r,a,4)); }
    uint16_t half(int r,size_t a) const { return rom_zip::le16(bytes(r,a,2)); }
    float scalar(size_t address) const {
        const auto bits=word(CPU,address);float v;memcpy(&v,&bits,4);
        if(!std::isfinite(v) || std::abs(v)>2)throw std::runtime_error("invalid VF2 accessory attachment");
        return v;
    }
    Point vertex(size_t a) const {
        Point p;
        for(int i=0;i<3;++i) {
            const auto bits=word(Polygons,a+i*4); memcpy(&p[i],&bits,4);
            if(!std::isfinite(p[i]) || std::abs(p[i])>10) throw std::runtime_error("invalid VF2 character vertex");
        }
        return p;
    }
    uint8_t channel(unsigned index,unsigned axis,unsigned luma) const {
        // The native game's first fight palette environment: channel offsets 64,
        // slopes 37 and gains 128. Special skin/gi curves and high-luma colours
        // come from ROM; the destination renderer supplies the changing lighting.
        unsigned value=0;
        if(index==28 || index==29) {
            const int raw=half(Main,0x101000+(index-28)*0x180+axis*0x80+luma*2);
            value=unsigned(std::max(raw-52,0));
        } else if(index<28 && luma>=48) {
            if(index>6)throw std::runtime_error("unsupported VF2 high-luma palette channel");
            const int raw=half(Main,0x105800+index*0x60+axis*0x20+(luma-48)*2);
            value=unsigned(std::max(raw-52,0));
        } else if(index<28 && index && luma) {
            value=64+((luma*(37*index*28/18))>>8);
            if(value>=256)value=255;
        } else if(index>=30)throw std::runtime_error("unsupported VF2 costume palette channel");
        return uint8_t(std::max(0,int(value&255)-64)*255/191);
    }
    unsigned filtered_texel(const std::array<uint16_t,4> &h,const UV &uv) const {
        // Model 2 interpolates texture indices and cutout coverage before its
        // luminance lookup. Filtering RGB, or treating index 15 as black,
        // instead introduces dark fringes around the eyes and lips.
        const unsigned width=32u<<(h[0]&7),height=32u<<((h[0]>>3)&7);
        auto axis=[](float coordinate,unsigned size,bool mirror,bool wrap) {
            int fixed=int(coordinate*256);
            if(mirror && (fixed&int(size<<8)))fixed=~fixed;
            fixed-=128;
            unsigned fraction=unsigned(fixed)&255;
            unsigned a=unsigned(fixed>>8)&(size-1),b=(a+1)&(size-1);
            if(!wrap && b==0) {
                if(fraction>=128) {a=b;++b;fraction=0;}
                else {b=a;--a;fraction=256;}
            }
            return std::array<unsigned,3>{a,b,fraction};
        };
        const auto u=axis(uv[0],width,h[0]&0x100,(h[0]&0x40) && !(h[0]&0x100));
        const auto v=axis(uv[1],height,h[0]&0x200,(h[0]&0x80) && !(h[0]&0x200));
        auto texel=[&](unsigned x,unsigned y) {
            x+=32*(h[2]&63);y+=32*((h[2]>>6)&31);
            if(x>=1024) {x-=1024;y^=1024;}
            const size_t offset=(size_t(y>>1)<<9)+(x>>1);
            if(offset>=sheet.size())throw std::runtime_error("VF2 texture coordinate outside sheet");
            const unsigned value=((sheet[offset]>>(((~y&1)<<3)+((~x&1)<<2)))&15)<<4;
            return value==240 ? value : value|0x00800000u;
        };
        auto lerp=[](uint32_t a,uint32_t b,unsigned fraction) {
            return (a+(((b-a)*fraction)>>8))&0x00ff00ffu;
        };
        uint32_t a=texel(u[0],v[0]),b=texel(u[1],v[0]);
        uint32_t c=texel(u[0],v[1]),d=texel(u[1],v[1]);
        if(a==240)a=b&255;
        if(b==240)b=a&255;
        if(c==240)c=d&255;
        if(d==240)d=c&255;
        a=lerp(a,b,u[2]);c=lerp(c,d,u[2]);
        if(a==240)a=c&255;
        if(c==240)c=a&255;
        return lerp(a,c,v[2]);
    }
    uint32_t colour(const std::array<uint16_t,4> &h,const UV &uv,bool detail=false) const {
        unsigned luma=40;
        if(h[0]&0x4000) {
            if(!(h[2]&0x1000) || sheet.empty())throw std::runtime_error("VF2 character texture sheet unavailable");
            unsigned lookup;
            if(detail) {
                const unsigned texel=filtered_texel(h,uv);
                if(texel<0x00400000)return 0;
                lookup=(texel&255)>>1;
            } else {
                const unsigned width=32u<<(h[0]&7),height=32u<<((h[0]>>3)&7);
                unsigned x=unsigned(uv[0]),y=unsigned(uv[1]);
                if((h[0]&0x100) && (x&width))x=~x;
                if((h[0]&0x200) && (y&height))y=~y;
                x=(x&(width-1))+32*(h[2]&63);
                y=(y&(height-1))+32*((h[2]>>6)&31);
                x&=2047;y&=1023;
                if(x>=1024) {x-=1024;y^=1024;}
                const size_t offset=(size_t(y>>1)<<9)+(x>>1);
                if(offset>=sheet.size())throw std::runtime_error("VF2 texture coordinate outside sheet");
                const unsigned texel=(sheet[offset]>>(((~y&1)<<3)+((~x&1)<<2)))&15;
                if((h[0]&0x2000) && texel==15)return 0;
                lookup=texel*8;
            }
            luma=std::min<unsigned>(*bytes(CPU,0x78d10+((h[1]&255)*128+lookup),1),63);
        }
        const auto pal=half(Main,0x100000+2*((h[3]>>6)&1023));
        return 0xff000000u | uint32_t(channel(pal&31,0,luma))<<16 |
            uint32_t(channel((pal>>5)&31,1,luma))<<8 | channel((pal>>10)&31,2,luma);
    }
    static void triangle(Mesh &out,uint32_t colour,const Point &a,const Point &b,const Point &c) {
        Point u,v;for(int k=0;k<3;++k) {u[k]=b[k]-a[k];v[k]=c[k]-a[k];}
        const double x=u[1]*v[2]-u[2]*v[1], y=u[2]*v[0]-u[0]*v[2], z=u[0]*v[1]-u[1]*v[0];
        if(x*x+y*y+z*z>1e-20) out.push_back({colour,{a,b,c}});
        if(out.size()>100000) throw std::runtime_error("oversized VF2 model");
    }
    void face(Mesh &out,const std::array<uint16_t,4> &h,const Point &a,const Point &b,const Point &c,
              const UV &ua,const UV &ub,const UV &uc,unsigned depth,unsigned limit,bool detail=false) const {
        const UV center={(ua[0]+ub[0]+uc[0])/3,(ua[1]+ub[1]+uc[1])/3};
        const auto rgb=colour(h,center,detail);
        float extent=0;
        for(unsigned k=0;k<2;++k)extent=std::max(extent,std::max({ua[k],ub[k],uc[k]})-std::min({ua[k],ub[k],uc[k]}));
        bool varies=depth==0;
        for(const auto &u:{ua,ub,uc}) {
            const auto sample=colour(h,u,detail);
            if((sample>>24)!=(rgb>>24))varies=true;
            for(unsigned shift:{0,8,16})if(std::abs(int((sample>>shift)&255)-int((rgb>>shift)&255))>12)varies=true;
        }
        // Do not let equal corner/centre samples hide a thin pupil or lip.
        // Facial cutouts need a texel-sized grid even on apparently flat cells.
        if(depth<limit && extent>(detail ? .75f : 3.0f) && (detail || varies)) {
            Point ab,bc,ca;UV uab,ubc,uca;
            for(unsigned k=0;k<3;++k) {ab[k]=(a[k]+b[k])/2;bc[k]=(b[k]+c[k])/2;ca[k]=(c[k]+a[k])/2;}
            for(unsigned k=0;k<2;++k) {uab[k]=(ua[k]+ub[k])/2;ubc[k]=(ub[k]+uc[k])/2;uca[k]=(uc[k]+ua[k])/2;}
            face(out,h,a,ab,ca,ua,uab,uca,depth+1,limit,detail);face(out,h,ab,b,bc,uab,ub,ubc,depth+1,limit,detail);
            face(out,h,ca,bc,c,uca,ubc,uc,depth+1,limit,detail);face(out,h,ab,bc,ca,uab,ubc,uca,depth+1,limit,detail);
        } else if(detail) {
            // Keep a cutout's boundary at its half-coverage contour, rather
            // than admitting or discarding an entire baked triangle. Otherwise
            // the eyes acquire sawtooth edges even with correctly filtered ink.
            struct Sample {Point point;UV uv;unsigned coverage;};
            const std::array<Sample,3> corners={{{a,ua,filtered_texel(h,ua)>>16},
                {b,ub,filtered_texel(h,ub)>>16},{c,uc,filtered_texel(h,uc)>>16}}};
            const unsigned inside=(corners[0].coverage>=64)+(corners[1].coverage>=64)+(corners[2].coverage>=64);
            if(inside==0 || inside==3) {
                if(rgb>>24)triangle(out,rgb,a,b,c);
                return;
            }
            std::array<Sample,4> clipped;unsigned count=0;
            for(unsigned i=0;i<3;++i) {
                const auto &p=corners[i],&q=corners[(i+1)%3];
                if(p.coverage>=64)clipped[count++]=p;
                if((p.coverage>=64)==(q.coverage>=64))continue;
                const float fraction=(64.f-p.coverage)/(float(q.coverage)-p.coverage);
                Sample intersection{};
                for(unsigned k=0;k<3;++k)intersection.point[k]=p.point[k]+fraction*(q.point[k]-p.point[k]);
                for(unsigned k=0;k<2;++k)intersection.uv[k]=p.uv[k]+fraction*(q.uv[k]-p.uv[k]);
                clipped[count++]=intersection;
            }
            for(unsigned i=1;i+1<count;++i) {
                UV center;
                for(unsigned k=0;k<2;++k)center[k]=(clipped[0].uv[k]+clipped[i].uv[k]+clipped[i+1].uv[k])/3;
                const auto sample=colour(h,center,true);
                if(sample>>24)triangle(out,sample,clipped[0].point,clipped[i].point,clipped[i+1].point);
            }
        } else if(rgb>>24)triangle(out,rgb,a,b,c);
    }
    Mesh part(unsigned descriptor,unsigned subdivision=2) const {
        const size_t d=0xe0000+size_t(descriptor)*16;
        size_t tpa=word(Main,d+4), tha=word(Main,d+8);
        const uint32_t object=word(Main,d+12);
        if((object&0xff800000)!=0x800000 || tpa&0x800000 || tha&0x800000)
            throw std::runtime_error("unsupported VF2 model descriptor");
        size_t at=size_t(object&0x7fffff)*4;
        Point old0=vertex(at),old1=vertex(at+12); at+=24;
        Mesh out;bool ended=false;
        for(unsigned i=0;i<4096;++i,at+=40) {
            const uint32_t attr=word(Polygons,at),type=attr&3,link=(attr>>8)&3;
            if(!type) {ended=true;break;}
            const Point a=vertex(at+16),b=(type&1)?vertex(at+28):a;
            std::array<UV,4> uv{};
            for(unsigned j=0;j<((type&1)?4:3);++j) {
                uv[j]={float(half(Textures,tpa*2+2))/8,float(half(Textures,tpa*2))/8};tpa+=2;
            }
            std::array<uint16_t,4> h;
            for(unsigned j=0;j<4;++j)h[j]=half(Textures,tha*2+j*2);
            int delta=(attr>>12)&31;if(delta&16)delta-=32;
            if(delta<0 && tha<size_t(-delta*4)) throw std::runtime_error("invalid VF2 texture header link");
            tha+=delta*4;
            if(link) {
                // Model 2 draws the alpha-cutout facial details over coplanar
                // skin in painter order. Real3D uses a depth buffer, and RGB
                // batching otherwise lets the skin overwrite eyes and lips.
                // Its source normals point inward; lift only the head decals
                // by 0.41 destination units, leaving the strip itself intact.
                const Point normal=vertex(at+4);
                const unsigned palette=(h[3]>>6)&1023;
                const bool detail=descriptor==0x19c && (h[0]&0x2000) && (palette==1 || palette==2);
                auto surface=[&](Point p) {
                    if(detail)
                        for(unsigned k=0;k<3;++k)p[k]-=.0002f*normal[k];
                    return p;
                };
                const unsigned limit=detail ? 5 : subdivision;
                face(out,h,surface(old1),surface(old0),surface(a),uv[0],uv[1],uv[2],0,limit,detail);
                if(type&1)face(out,h,surface(old1),surface(a),surface(b),uv[0],uv[2],uv[3],0,limit,detail);
            }
            if(link==0 || link==2) {old0=a;old1=b;}
            else if(link==1)old1=a;
            else old0=b;
        }
        if(!ended || out.empty())throw std::runtime_error("unterminated or empty VF2 model descriptor "+std::to_string(descriptor));
        return out;
    }
};
}
bool load(const std::string &path,vf1_models::Assets &out,std::string &error) {
    try {
        rom_zip::zip z;if(!z.open(path))throw std::runtime_error("cannot read VF2 ZIP");
        Decoder d;
        d.regions[Main].resize(0x1000000);d.regions[Polygons].resize(0xc00000);
        d.regions[Textures].resize(0xc00000,255);d.regions[CPU].resize(0x80000);
        std::vector<uint8_t> raw;
        for(const auto &rom:roms) {
            const rom_zip::entry *entry=nullptr;
            for(const auto &e:z.entries)if(e.crc==rom.crc && e.usize==rom.size) {entry=&e;break;}
            if(!entry || !z.read(*entry,raw))throw std::runtime_error(std::string("missing, wrong or damaged VF2 ROM: ")+rom.name);
            auto &dest=d.regions[rom.region];
            for(size_t i=0;i<raw.size();i+=2)memcpy(dest.data()+rom.offset+2*i,raw.data()+i,2);
        }
        d.sheet=detail::TextureDecoder(d.regions[Main]).akiraSheet();
        vf1_models::Assets next;
        constexpr unsigned body[]={0x996,0x994,0x19e,0x1a6,0x19d,0x1a5,0x1a1,0x19b,0x198,0x1a0,0x19a,0x197,0x19c};
        // Bound cached geometry: the head gets three subdivision levels (five
        // for its eye/mouth cutouts), body surfaces two, and cached hands one.
        for(size_t i=0;i<next.parts.size();++i)next.parts[i]=d.part(body[i],i==size_t(vf1_models::Part::Head)?3:2);
        for(unsigned hand=0;hand<2;++hand)for(unsigned pose=0;pose<32;++pose)
            next.hands[hand][pose]=d.part(d.word(Main,(hand?0x1088:0xf08)+(31-pose)*4),1);
        auto strand=[&](vf1_models::Strand &s,uint8_t bone,size_t anchor,size_t descriptors,size_t count) {
            s.bone=bone;
            for(unsigned axis=0;axis<3;++axis)s.anchor[axis]=d.scalar(anchor+axis*4);
            s.segments.resize(count);s.lengths.resize(count);
            for(size_t i=0;i<count;++i) {
                s.segments[i]=d.part(d.word(CPU,descriptors+i*0x44),3);
                s.lengths[i]=d.scalar(descriptors+i*0x44+12);
                if(s.lengths[i]<=0)throw std::runtime_error("invalid VF2 accessory segment length");
            }
        };
        strand(next.strands[0],2,0x67bd8,0x67c00,5);
        next.extra_head_ribbons.resize(1);
        strand(next.extra_head_ribbons[0],2,0x67bd8,0x67d6c,5);
        strand(next.strands[1],1,0x67a98,0x67ac0,2);
        strand(next.strands[2],1,0x67b38,0x67b60,2);
        // Original manga-style select icon. The six-by-eleven tile image is
        // embedded in a 62-column map; graphics and BGR555 palette are raw ROM.
        auto &portrait=next.portrait;
        portrait.width=48;portrait.height=88;portrait.pixels.resize(48*88);
        for(unsigned y=0;y<88;++y)for(unsigned x=0;x<48;++x) {
            const unsigned entry=d.half(Main,0xa712fc+(y/8)*124+(x/8)*2);
            if(entry<0x2a80 || entry>0x2abe)throw std::runtime_error("invalid VF2 Akira portrait tile");
            const size_t row=0x84f4d4+(entry-0x2a80)*32+(y%8)*4;
            const uint32_t packed=(uint32_t(d.half(Main,row))<<16)|d.half(Main,row+2);
            const unsigned pen=(packed>>(28-4*(x%8)))&15;
            const auto bgr=d.half(Main,0x800ee0+pen*2);
            uint32_t rgb=0xff000000;
            for(unsigned axis=0;axis<3;++axis) {
                const unsigned value=(bgr>>(axis*5))&31;
                rgb|=((value<<3)|(value>>2))<<(16-axis*8);
            }
            portrait.pixels[y*48+x]=rgb;
        }
        out=std::move(next);error.clear();return true;
    } catch(const std::exception &e) {error=e.what();return false;}
}
}
