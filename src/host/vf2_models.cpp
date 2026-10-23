// SPDX-License-Identifier: BSD-3-Clause
// Model 2 strip/texture format follows MAME sega/model2_v.cpp and model2rd.ipp,
// copyright R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels,
// as documented in vf2-recomp/src/machine/model2_video.cpp.
#include "vf2_models.h"
#include "rom_zip.h"
#include "vf2_texture_decode.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
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
    struct Materials {
        std::map<std::vector<uint32_t>,unsigned> ids;
        std::vector<std::shared_ptr<const Model2Texture>> textures;
    };
    bool textured=false;
    std::shared_ptr<Materials> materials=std::make_shared<Materials>();
    mutable std::map<std::array<unsigned,5>,unsigned> material_cache;
    struct PreparedFace {
        std::array<uint16_t,4> header{};
        std::array<Point,3> points{};
        std::array<UV,3> uv{};
        unsigned limit=0;
        bool detail=false;
        Mesh fixed;
    };
    struct Fragment { size_t address; std::shared_ptr<const std::vector<uint8_t>> data; };
    std::array<std::vector<uint8_t>, RegionCount> regions;
    std::array<std::vector<Fragment>, RegionCount> fragments;
    std::vector<uint16_t> sheet;
    unsigned head_descriptor = 0x19c;
    unsigned native_actor = 0;
    unsigned material(const std::array<uint16_t,4> &h,bool detail) const {
        const std::array<unsigned,5> header={h[0],h[1],h[2],h[3],unsigned(detail)};
        if(const auto found=material_cache.find(header);found!=material_cache.end())return found->second;
        auto tile=std::make_shared<Model2Texture>();
        tile->width=32u<<(h[0]&7);tile->height=32u<<((h[0]>>3)&7);
        tile->flags=h[0]|(detail?0x10000:0);
        if(tile->width>1024 || tile->height>1024 || !(h[2]&0x1000))
            throw std::runtime_error("invalid VF2 material tile");
        const auto pal=half(Main,0x100000+2*((h[3]>>6)&1023));
        for(unsigned i=0;i<128;++i) {
            const unsigned luma=std::min<unsigned>(*bytes(CPU,0x78d10+(h[1]&255)*128+i,1),63);
            tile->words.push_back(0xff000000u|uint32_t(channel(pal&31,0,luma))<<16|
                uint32_t(channel((pal>>5)&31,1,luma))<<8|channel((pal>>10)&31,2,luma));
        }
        for(unsigned y=0;y<tile->height;++y)for(unsigned x=0;x<tile->width;++x) {
            unsigned sx=(x+32*(h[2]&63))&2047,sy=(y+32*((h[2]>>6)&31))&1023;
            if(sx>=1024){sx-=1024;sy^=1024;}
            const auto at=(size_t(sy>>1)<<9)+(sx>>1);
            if(at>=sheet.size())throw std::runtime_error("VF2 material outside atlas");
            tile->words.push_back((sheet[at]>>(((~sy&1)<<3)+((~sx&1)<<2)))&15);
        }
        auto key=tile->words;key.push_back(tile->width);key.push_back(tile->height);key.push_back(tile->flags);
        auto [it,inserted]=materials->ids.emplace(std::move(key),unsigned(materials->textures.size()+1));
        if(inserted)materials->textures.push_back(std::move(tile));
        material_cache.emplace(header,it->second);
        return it->second;
    }
    const uint8_t *bytes(int region, size_t address, size_t count) const {
        const auto &r=regions[region];
        if(r.empty()) {
            const auto &f=fragments[region];
            auto it=std::upper_bound(f.begin(),f.end(),address,
                [](size_t a,const Fragment &b){return a<b.address;});
            if(it!=f.begin()) {
                --it;const auto offset=address-it->address;
                if(offset<=it->data->size() && count<=it->data->size()-offset)
                    return it->data->data()+offset;
            }
            throw std::runtime_error("VF2 face address outside captured ROM");
        }
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
        // slopes 37 and gains 128. i960 754..9e8 selects each native fighter's
        // skin/costume curves and high-luma eye, mouth and hair colours. Reading
        // actor zero's tables for everyone turns Shun's white beard red/black.
        // The destination renderer supplies the changing lighting.
        unsigned value=0;
        if(index==28 || index==29) {
            const int raw=half(Main,0x101000+native_actor*0x300+(index-28)*0x180+axis*0x80+luma*2);
            value=unsigned(std::max(raw-52,0));
        } else if(index<28 && luma>=48) {
            if(index>6)throw std::runtime_error("unsupported VF2 high-luma palette channel");
            const int raw=half(Main,0x105800+native_actor*0x2a0+index*0x60+axis*0x20+(luma-48)*2);
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
        if(textured) {
            const size_t first=out.size();
            triangle(out,(h[0]&0x4000)?0xffffffffu:colour(h,ua),a,b,c);
            if(out.size()!=first && (h[0]&0x4000)) {
                out.back().material=material(h,detail);out.back().uv={ua,ub,uc};
            }
            return;
        }
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
    Mesh part(unsigned descriptor,unsigned subdivision=2,std::vector<PreparedFace> *prepared=nullptr) const {
        const size_t d=0xe0000+size_t(descriptor)*16;
        size_t tpa=word(Main,d+4), tha=word(Main,d+8);
        const uint32_t object=word(Main,d+12);
        if((object&0xff800000)!=0x800000 || tpa&0x800000 || tha&0x800000)
            throw std::runtime_error("unsupported VF2 model descriptor");
        size_t at=size_t(object&0x7fffff)*4;
        Point old0=vertex(at),old1=vertex(at+12); at+=24;
        Mesh out;bool ended=false;
        std::map<std::array<Point,3>,unsigned> head_layers;
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
                const bool detail=(prepared || descriptor==head_descriptor) && (h[0]&0x2000) &&
                                  (native_actor!=0 || palette==1 || palette==2);
                const unsigned limit=detail && (palette==1 || palette==2) ? 5 : subdivision;
                auto emit=[&](Point p,Point q,Point r,const UV &up,const UV &uq,const UV &ur) {
                    unsigned layer=0;
                    if(native_actor!=0 && (prepared || descriptor==head_descriptor)) {
                        std::array<Point,3> key={p,q,r};std::sort(key.begin(),key.end());
                        layer=head_layers[key]++;
                    }
                    // Native painter order includes opaque coplanar overlays,
                    // notably Kage's mask over his skin and mouth. Keep their
                    // exact source order when RGB groups enter a depth buffer.
                    // Kage's opaque mask (palette 92) is emitted after the
                    // mouth and skin with some different triangulation. Its
                    // entire surface must remain above those two layers.
                    if(native_actor==3 && (prepared || descriptor==head_descriptor) && palette==92)
                        layer=std::max(layer,3u);
                    const float lift=.0002f*std::max(layer,unsigned(detail));
                    for(unsigned k=0;k<3;++k) {p[k]-=lift*normal[k];q[k]-=lift*normal[k];r[k]-=lift*normal[k];}
                    const size_t begin=out.size();
                    face(out,h,p,q,r,up,uq,ur,0,limit,detail);
                    if(prepared) {
                        const unsigned x=(h[2]&63)*32,y=((h[2]>>6)&31)*32;
                        const unsigned height=32u<<((h[0]>>3)&7);
                        const bool dynamic=(h[0]&0x4000) && x<192 && y<832 && y+height>768;
                        if(dynamic)prepared->push_back({h,{p,q,r},{up,uq,ur},limit,detail,{}});
                        else {
                            if(prepared->empty() || prepared->back().limit)prepared->push_back({});
                            auto &fixed=prepared->back().fixed;
                            fixed.insert(fixed.end(),out.begin()+begin,out.end());
                        }
                    }
                };
                emit(old1,old0,a,uv[0],uv[1],uv[2]);
                if(type&1)emit(old1,a,b,uv[0],uv[2],uv[3]);
            }
            if(link==0 || link==2) {old0=a;old1=b;}
            else if(link==1)old1=a;
            else old0=b;
        }
        if(!ended || out.empty())throw std::runtime_error("unterminated or empty VF2 model descriptor "+std::to_string(descriptor));
        return out;
    }
};
// i960 4b604..4b7ac: native linked face timelines. Each step independently
// interpolates mouth/eye phases, selects a jaw descriptor and uploads two
// Huffman-compressed texture patches. Keep only combinations the scripts use.
void faces(const Decoder &source,vf1_models::Assets &out) {
    using Key=std::array<unsigned,3>; // descriptor, mouth patch+1, eye patch+1
    struct Patch {bool mouth;std::vector<uint16_t> blocks;};
    struct Recipe {
        Decoder decoder;std::vector<Key> keys;std::vector<Patch> patches;
        std::map<unsigned,std::vector<Decoder::PreparedFace>> heads;
    };
    auto recipe=std::make_shared<Recipe>();
    recipe->decoder.native_actor=source.native_actor;
    recipe->decoder.textured=source.textured;recipe->decoder.materials=source.materials;
    recipe->decoder.sheet=source.sheet;
    std::map<Key,unsigned> bank;
    auto add=[&](Key key) {
        auto [it,inserted]=bank.emplace(key,unsigned(recipe->keys.size()));
        if(inserted)recipe->keys.push_back(key);
        if(recipe->keys.size()>2048)throw std::runtime_error("oversized VF2 face bank");
        return uint16_t(it->second);
    };
    add({source.head_descriptor,0,0});
    std::map<std::array<unsigned,3>,unsigned> patch_ids;
    const detail::TextureDecoder texture(source.regions[Main]);
    auto patch=[&](unsigned group,unsigned frame,bool mouth) {
        const std::array<unsigned,3> key={group,frame,unsigned(mouth)};
        const auto found=patch_ids.find(key);
        if(found!=patch_ids.end())return found->second;
        if(recipe->patches.size()>=512)throw std::runtime_error("oversized VF2 face patch bank");
        auto sheet=source.sheet;
        texture.facePatch(sheet,group,frame,mouth);
        Patch p{mouth,{}};
        const unsigned x=mouth?64:0,width=mouth?32:64;
        for(unsigned y=384;y<416;++y)
            p.blocks.insert(p.blocks.end(),sheet.begin()+y*512+x,sheet.begin()+y*512+x+width);
        recipe->patches.push_back(std::move(p));
        const unsigned id=unsigned(recipe->patches.size());patch_ids.emplace(key,id);return id;
    };
    auto pointer=[&](uint32_t address) -> size_t {
        if(address<0x02000000 || address>=0x03000000)
            throw std::runtime_error("invalid VF2 face script pointer");
        return address-0x02000000;
    };
    for(unsigned animation=0;animation<10;++animation) {
        const size_t table=pointer(source.word(Main,0x6278+animation*4));
        uint32_t node=source.word(Main,table+source.native_actor*4);
        auto &timeline=out.face_animations[animation];
        // Recovery scripts interpolate back from the fully active/closed state.
        std::array<int,2> phase={animation==2 || animation==9?1000:0,animation==2 || animation==9?1000:0};
        std::array<unsigned,2> patches{};
        std::map<std::array<unsigned,5>,unsigned> visited;
        for(unsigned nodes=0;node;++nodes) {
            if(nodes>=256)throw std::runtime_error("oversized VF2 face script");
            const std::array<unsigned,5> state={node,unsigned(phase[0]),unsigned(phase[1]),patches[0],patches[1]};
            auto [it,inserted]=visited.emplace(state,unsigned(timeline.frames.size()));
            if(!inserted) {timeline.loop_start=int(it->second);break;}
            const size_t at=pointer(node);
            const unsigned duration=source.half(Main,at);
            if(duration>1000 || timeline.frames.size()+duration>4096)
                throw std::runtime_error("oversized VF2 face timeline");
            const std::array<int,2> target={source.half(Main,at+4),source.half(Main,at+6)};
            if(target[0]>1000 || target[1]>1000)throw std::runtime_error("invalid VF2 face phase");
            const size_t mouth=pointer(source.word(Main,at+8));
            const unsigned count=source.half(Main,mouth+2),group=source.half(Main,mouth);
            if(!count || count>33)throw std::runtime_error("invalid VF2 mouth sequence");
            const size_t descriptors=pointer(source.word(Main,mouth+4));
            for(unsigned tick=0;tick<duration;++tick) {
                const int remaining=int(duration-tick-1);
                for(unsigned side=0;side<2;++side)
                    phase[side]=remaining?phase[side]+(target[side]-phase[side])/remaining:target[side];
                const unsigned frame=unsigned(phase[0])*count/1001;
                const unsigned index=*source.bytes(Main,mouth+8+frame,1);
                const unsigned descriptor=source.word(Main,descriptors+index*16);
                if(group!=0xffff) {
                    patches[0]=patch(group,frame,true);
                    const size_t eyes=pointer(source.word(Main,at+12));
                    const unsigned eye_count=source.half(Main,eyes+2);
                    if(!eye_count || eye_count>33)throw std::runtime_error("invalid VF2 eye sequence");
                    patches[1]=patch(source.half(Main,eyes),unsigned(phase[1])*eye_count/1001,false);
                }
                timeline.frames.push_back(add({descriptor,patches[0],patches[1]}));
            }
            node=source.word(Main,at+16);
        }
    }
    // Script 8 traverses the original neutral/action/closed aliases. Decode
    // these against the initial atlas, separately from any animated uploads.
    const size_t table=pointer(source.word(Main,0x6278+8*4));
    const size_t node=pointer(source.word(Main,table+source.native_actor*4));
    const size_t sequence=pointer(source.word(Main,node+8));
    const size_t aliases=pointer(source.word(Main,sequence+4));
    for(unsigned state=0;state<3;++state) {
        const unsigned index=*source.bytes(Main,sequence+8+state,1);
        out.face_states[state]=add({source.word(Main,aliases+index*16),0,0});
    }
    // A cross-game aperture lookup, separate from the untouched native scripts.
    // Script 1 includes closing passes (and Akira opens twice), so its phase is
    // not a linear jaw opening. Use the original final opening pass, verified
    // against each 64x64 mouth atlas; keep neutral eyes and exact source jaws.
    const size_t speech_table=pointer(source.word(Main,0x6278+4));
    const size_t speech_node=pointer(source.word(Main,speech_table+source.native_actor*4));
    const size_t speech=pointer(source.word(Main,speech_node+8));
    const unsigned speech_count=source.half(Main,speech+2),speech_group=source.half(Main,speech);
    const size_t speech_descriptors=pointer(source.word(Main,speech+4));
    unsigned first=0,last=speech_count-1;
    if(source.native_actor==0)first=10;
    if(source.native_actor==5)first=13,last=19;
    if(source.native_actor==7)last=9;
    if(source.native_actor==3 || source.native_actor==8)last=5;
    if(last>=speech_count || first>last)throw std::runtime_error("invalid VF2 speech opening pass");
    out.face_speech.push_back(0);
    for(unsigned step=1;step<=16;++step) {
        const unsigned frame=first+(step*(last-first)+8)/16;
        const unsigned index=*source.bytes(Main,speech+8+frame,1);
        out.face_speech.push_back(add({source.word(Main,speech_descriptors+index*16),
                                     patch(speech_group,frame,true),0}));
    }
    // Prepare exact painter-order geometry once per native jaw/UV alias.
    std::set<unsigned> descriptors;
    for(const auto &key:recipe->keys)descriptors.insert(key[0]);
    for(unsigned descriptor:descriptors)source.part(descriptor,3,&recipe->heads[descriptor]);
    // Baking needs only colour curves and luminance tables after preparation.
    // The final callback retains neither these ROM slices nor the atlas.
    for(const auto &[region,begin,end]:std::array<std::array<size_t,3>,2>{{
        {Main,0x100000,0x108000},{CPU,0x78d10,0x79510}}}) {
        const auto *data=source.bytes(int(region),begin,end-begin);
        recipe->decoder.fragments[region].push_back({begin,
            std::make_shared<const std::vector<uint8_t>>(data,data+end-begin)});
    }
    // Factor the native head into consecutive static, mouth and eye pieces.
    // Identical geometry is shared between jaw aliases, and each independent
    // patch is baked only once. Compress runtime meshes so frame changes need
    // bounded decompression instead of recursive cutout triangulation.
    struct Packed {unsigned count;std::vector<uint8_t> data;};
    struct Bank {std::vector<Packed> pieces;std::vector<std::vector<unsigned>> frames;};
    struct Group {unsigned side=0;std::vector<Decoder::PreparedFace> faces;};
    auto compiled=std::make_shared<Bank>();
    std::vector<Group> groups;
    std::map<std::vector<uint8_t>,unsigned> group_ids;
    std::map<unsigned,std::vector<unsigned>> head_groups;
    auto intern=[&](Group group) {
        std::vector<uint8_t> bytes;
        auto append=[&](const auto &value) {
            const auto *p=reinterpret_cast<const uint8_t*>(&value);
            bytes.insert(bytes.end(),p,p+sizeof(value));
        };
        append(group.side);
        for(const auto &face:group.faces) {
            if(group.side) {
                append(face.header);append(face.points);append(face.uv);append(face.limit);append(face.detail);
            } else for(const auto &triangle:face.fixed) {
                append(triangle.colour);append(triangle.points);append(triangle.material);append(triangle.uv);
            }
        }
        auto [it,inserted]=group_ids.emplace(std::move(bytes),unsigned(groups.size()));
        if(inserted)groups.push_back(std::move(group));
        return it->second;
    };
    for(auto &[descriptor,head]:recipe->heads) {
        auto &indices=head_groups[descriptor];Group group;bool begun=false;
        for(auto &face:head) {
            const unsigned x=(face.header[2]&63)*32;
            const unsigned side=face.limit?(x>=128?1:2):0;
            if(face.limit && x+(32u<<(face.header[0]&7))>(side==1?192:128))
                throw std::runtime_error("VF2 face texture crosses component boundary");
            if(begun && side!=group.side) {indices.push_back(intern(std::move(group)));group={};begun=false;}
            group.side=side;group.faces.push_back(std::move(face));begun=true;
        }
        if(begun)indices.push_back(intern(std::move(group)));
    }
    std::map<std::pair<unsigned,unsigned>,unsigned> baked_ids;
    Decoder baker=std::move(recipe->decoder);
    size_t compressed_bytes=0;
    for(const auto &key:recipe->keys) {
        std::vector<unsigned> indices;
        for(unsigned group_id:head_groups.at(key[0])) {
            const auto &group=groups[group_id];const unsigned patch_id=group.side?key[group.side]:0;
            auto [it,inserted]=baked_ids.emplace(std::make_pair(group_id,patch_id),unsigned(compiled->pieces.size()));
            if(inserted) {
                Mesh mesh;
                if(group.side) {
                    // Header identities are reusable only while the source
                    // atlas is unchanged. This piece installs another patch.
                    baker.material_cache.clear();
                    // Zero means the untouched native atlas, not patch frame 0.
                    const unsigned x=group.side==1?64:0,width=group.side==1?32:64;
                    for(unsigned row=0;row<32;++row) {
                        const auto *data=patch_id?recipe->patches[patch_id-1].blocks.data()+row*width:
                            source.sheet.data()+(384+row)*512+x;
                        std::copy_n(data,width,baker.sheet.begin()+(384+row)*512+x);
                    }
                    for(const auto &face:group.faces)
                        baker.face(mesh,face.header,face.points[0],face.points[1],face.points[2],
                                   face.uv[0],face.uv[1],face.uv[2],0,face.limit,face.detail);
                } else for(const auto &face:group.faces)mesh.insert(mesh.end(),face.fixed.begin(),face.fixed.end());
                Packed packed{unsigned(mesh.size()),{}};
                if(!mesh.empty()) {
                    const size_t size=mesh.size()*sizeof(vf1_models::Triangle);
                    uLongf length=compressBound(size);packed.data.resize(length);
                    if(compress2(packed.data.data(),&length,reinterpret_cast<const Bytef*>(mesh.data()),size,1)!=Z_OK)
                        throw std::runtime_error("cannot compress VF2 face component");
                    packed.data.resize(length);compressed_bytes+=length;
                }
                if(compressed_bytes>32*1024*1024 || compiled->pieces.size()>=8192)
                    throw std::runtime_error("oversized VF2 compiled face bank");
                compiled->pieces.push_back(std::move(packed));
            }
            indices.push_back(it->second);
        }
        compiled->frames.push_back(std::move(indices));
    }
    out.face_frame_count=unsigned(compiled->frames.size());
    out.face_frames={out.parts[size_t(vf1_models::Part::Head)]};
    out.decode_face=[compiled=std::shared_ptr<const Bank>(std::move(compiled))](unsigned frame) {
        if(frame>=compiled->frames.size())throw std::out_of_range("VF2 face frame outside bank");
        size_t count=0;for(unsigned id:compiled->frames[frame])count+=compiled->pieces[id].count;
        if(count>100000)throw std::runtime_error("oversized VF2 compiled head");
        Mesh result(count);size_t offset=0;
        for(unsigned id:compiled->frames[frame]) {
            const auto &piece=compiled->pieces[id];
            if(piece.count) {
                uLongf size=size_t(piece.count)*sizeof(vf1_models::Triangle);
                const auto expected=size;
                if(uncompress(reinterpret_cast<Bytef*>(result.data()+offset),&size,piece.data.data(),piece.data.size())!=Z_OK || size!=expected)
                    throw std::runtime_error("invalid VF2 compressed face component");
                offset+=piece.count;
            }
        }
        return result;
    };
}
}
bool load(const std::string &path,vf1_models::Assets &out,std::string &error,unsigned fighter,bool textured) {
    try {
        if(fighter>=10)throw std::runtime_error("unsupported VF2 fighter");
        rom_zip::zip z;if(!z.open(path))throw std::runtime_error("cannot read VF2 ZIP");
        Decoder d;
        d.textured=textured;
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
        // Actual VF2 2.1 P1 records in the shared select order. Pai's default
        // body comes from the second costume bank; its native actor remains 6.
        constexpr unsigned records[]={0xec8,0x4858,0x1d08,0x27b8,0x2098,0x1978,0x15e8,0x1258,0x2b48,0x2f68};
        constexpr unsigned native_actors[]={0,6,4,7,5,3,2,1,8,10};
        const unsigned record=records[fighter];
        d.native_actor=native_actors[fighter];
        d.sheet=detail::TextureDecoder(d.regions[Main]).fighterSheet(d.word(Main,record+0x370));
        vf1_models::Assets next;
        constexpr unsigned body_slots[]={1,9,3,4,6,7,10,11,12,13,14,15,2};
        d.head_descriptor=d.word(Main,record+2*4);
        // Bound cached geometry: the head gets three subdivision levels (five
        // for its eye/mouth cutouts), body surfaces two, and cached hands one.
        for(size_t i=0;i<next.parts.size();++i)
            next.parts[i]=d.part(d.word(Main,record+body_slots[i]*4),i==size_t(vf1_models::Part::Head)?3:2);
        for(unsigned hand=0;hand<2;++hand)for(unsigned pose=0;pose<32;++pose)
            next.hands[hand][pose]=d.part(d.word(Main,record+0x40+hand*0x180+(31-pose)*4),1);
        const uint32_t skeleton=d.word(Main,record+0x344);
        if(skeleton<0x02000000)throw std::runtime_error("invalid VF2 skeleton pointer");
        // The native table starts with torso/root offset 0, followed by neck,
        // shoulders and limb lengths, matching the shared VF1 joint indexing.
        for(unsigned slot=0;slot<15;++slot)for(unsigned axis=0;axis<3;++axis) {
            const auto bits=d.word(Main,skeleton-0x02000000+slot*12+axis*4);
            float value;memcpy(&value,&bits,4);
            if(!std::isfinite(value)||std::abs(value)>2)throw std::runtime_error("invalid VF2 skeleton joint");
            next.source_joints[slot][axis]=value;
        }
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
        if(fighter==0) {
            strand(next.strands[0],2,0x67bd8,0x67c00,5);
            next.extra_head_ribbons.resize(1);
            strand(next.extra_head_ribbons[0],2,0x67bd8,0x67d6c,5);
            strand(next.strands[1],1,0x67a98,0x67ac0,2);
            strand(next.strands[2],1,0x67b38,0x67b60,2);
        } else if(fighter==7) {
            // Jacky's authored selection hair shares the head matrix in VF2.
            // The fifteen battle tufts require the original spring transforms;
            // pointing them all upward produces an incorrect narrow crest.
            // Keep the original rigid silhouette in every destination pose.
            for(auto &s:next.strands) {s.segments.clear();s.lengths.clear();}
            next.head_accessory=d.part(d.word(Main,0x61b8+d.native_actor*4),3);
        } else {
            // VF2's bounded accessory bytecode: 4 starts a chain, 5 appends
            // a segment. Commands 1..3 configure native cloth simulation;
            // the destination uses its own animation and these source meshes.
            size_t at=d.word(CPU,0x658a4+native_actors[fighter]*4);
            std::vector<vf1_models::Strand> chains;
            vf1_models::Strand current;
            current.segments.clear();current.lengths.clear();
            for(auto &s:next.strands) {s.segments.clear();s.lengths.clear();}
            bool attached=false,ended=false;
            auto finish=[&] {
                if(!current.segments.empty())chains.push_back(std::move(current));
                current={};current.segments.clear();current.lengths.clear();
            };
            constexpr size_t command_bytes[]={4,36,64,12,24,68};
            for(unsigned command=0;command<128;++command) {
                const unsigned op=d.word(CPU,at);
                if(!op) {finish();ended=true;break;}
                if(op>5)throw std::runtime_error("unknown VF2 accessory command");
                if(op==4) {
                    finish();
                    const auto bone=d.word(CPU,at+8);
                    if(bone) {
                        if(bone!=1 && bone!=2 && bone!=9)throw std::runtime_error("unsupported VF2 accessory bone");
                        current.bone=uint8_t(bone);
                        for(unsigned axis=0;axis<3;++axis)current.anchor[axis]=d.scalar(at+12+axis*4);
                        attached=true;
                    } else {
                        if(chains.empty())throw std::runtime_error("VF2 accessory has no parent attachment");
                        current.bone=chains.back().bone;current.anchor=chains.back().anchor;attached=true;
                    }
                } else if(op==5) {
                    if(!attached || current.segments.size()>=16)throw std::runtime_error("invalid VF2 accessory chain");
                    current.segments.push_back(d.part(d.word(CPU,at+28),3));
                    const auto length=d.scalar(at+40);
                    if(length<=0)throw std::runtime_error("invalid VF2 accessory length");
                    current.lengths.push_back(length);
                }
                at+=command_bytes[op];
            }
            if(!ended || chains.size()>32)throw std::runtime_error("unterminated VF2 accessory commands");
            unsigned belts=0;
            for(auto &chain:chains) {
                if(chain.bone==2) {
                    if(next.strands[0].segments.empty())next.strands[0]=std::move(chain);
                    else next.extra_head_ribbons.push_back(std::move(chain));
                } else if(belts<2)next.strands[1+belts++]=std::move(chain);
                else next.extra_strands.push_back(std::move(chain));
            }
        }
        // Original manga-style select icon. The six-by-eleven tile image is
        // embedded in a 62-column map; graphics and BGR555 palette are raw ROM.
        auto &portrait=next.portrait;
        portrait.width=48;portrait.height=88;portrait.pixels.resize(48*88);
        size_t graphics=0x84f4d0;
        for(unsigned i=0;i<fighter;++i) {
            const auto count=d.word(Main,graphics);
            if(!count || count>128)throw std::runtime_error("invalid VF2 portrait graphics block");
            graphics+=4+count*32;
        }
        const auto tile_count=d.word(Main,graphics),first_tile=0x2a80+fighter*0x80;
        if(!tile_count || tile_count>128)throw std::runtime_error("invalid VF2 portrait tile count");
        for(unsigned y=0;y<88;++y)for(unsigned x=0;x<48;++x) {
            const unsigned entry=d.half(Main,0xa712fc+fighter*12+(y/8)*124+(x/8)*2);
            if(entry<first_tile || entry>=first_tile+tile_count)throw std::runtime_error("invalid VF2 fighter portrait tile");
            const size_t row=graphics+4+(entry-first_tile)*32+(y%8)*4;
            const uint32_t packed=(uint32_t(d.half(Main,row))<<16)|d.half(Main,row+2);
            const unsigned pen=(packed>>(28-4*(x%8)))&15;
            const auto bgr=d.half(Main,0x800ee0+fighter*32+pen*2);
            uint32_t rgb=0xff000000;
            for(unsigned axis=0;axis<3;++axis) {
                const unsigned value=(bgr>>(axis*5))&31;
                rgb|=((value<<3)|(value>>2))<<(16-axis*8);
            }
            portrait.pixels[y*48+x]=rgb;
        }
        faces(d,next);
        next.textures=std::move(d.materials->textures);
        out=std::move(next);error.clear();return true;
    } catch(const std::exception &e) {error=e.what();return false;}
}
}
