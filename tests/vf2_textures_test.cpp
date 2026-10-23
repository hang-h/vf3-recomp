// Keep the original decoder's colour sampler as an independent oracle.
#include "../src/host/vf2_models.cpp"
#include <cstdio>
#include <type_traits>

static void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv)
{
    try {
        static_assert(std::is_trivially_copyable_v<vf1_models::Triangle>);
        vf2_models::Decoder d;
        d.regions[0].resize(0x108000);d.regions[3].resize(0x80000);
        d.sheet.resize(1024*512);
        uint32_t random=42;
        for(auto &v:d.sheet){random=random*1664525+1013904223;v=uint16_t(random>>16);}
        auto half=[&](size_t at,uint16_t value){d.regions[0][at]=value;d.regions[0][at+1]=value>>8;};
        half(0x100000,uint16_t(28|(29<<5)|(3<<10)));
        for(unsigned i=0;i<0x800;++i)d.regions[3][0x78d10+i]=uint8_t((i*37+11)%64);
        for(unsigned i=0;i<0x300;i+=2)half(0x101000+i,uint16_t(64+(i*7)%192));
        for(unsigned i=0;i<0x2a0;i+=2)half(0x105800+i,uint16_t(64+(i*11)%192));
        unsigned checks=0;
        for(unsigned mode=0;mode<64;++mode)for(bool detail:{false,true}) {
            std::array<uint16_t,4> h={uint16_t(0x4000|((mode&1)?0x2000:0)|((mode&30)<<5)|1|(2<<3)),3,0x1000|15|(12<<6),0};
            const auto id=d.material(h,detail);const auto &texture=*d.materials->textures.at(id-1);
            for(unsigned i=0;i<1000;++i) {
                random=random*1664525+1013904223;float u=(random%65536)/128.f;
                random=random*1664525+1013904223;float v=(random%131072)/128.f;
                require(texture.sample(u,v)==d.colour(h,{u,v},detail),"indexed sampler differs from native colour oracle");++checks;
            }
        }
        std::printf("Model 2 texture sampler: %u native palette, mirror/wrap and cutout comparisons passed\n",checks);
        if(argc!=2)return 0;
        unsigned frames=0;
        for(unsigned fighter=0;fighter<10;++fighter) {
            vf1_models::Assets a;std::string error;
            require(vf2_models::load(argv[1],a,error,fighter),error.c_str());
            require(!a.textures.empty(),"missing source materials");
            for(const auto &t:a.textures) {
                require(t->words.size()==128+t->width*t->height,"invalid material size");
                for(size_t i=128;i<t->words.size();++i)require(t->words[i]<16,"invalid palette index");
            }
            auto mesh=[&](const vf1_models::Mesh &m) {
                require(!m.empty(),"empty body/expression");
                for(const auto &t:m) {
                    require(t.material<=a.textures.size(),"material lifetime/index invalid");
                    for(auto uv:t.uv)for(float v:uv)require(std::isfinite(v),"invalid source UV");
                }
            };
            size_t triangles=0;
            for(const auto &m:a.parts){mesh(m);triangles+=m.size();}
            for(const auto &side:a.hands)for(const auto &m:side)mesh(m);
            require(triangles<5000,"body unexpectedly subdivided into coloured triangles");
            for(unsigned frame=0;frame<a.face_frame_count;++frame){mesh(a.decode_face(frame));++frames;}
            std::printf("VF2 fighter %u: %zu body triangles, %zu immutable materials, %u expressions\n",
                        fighter,triangles,a.textures.size(),a.face_frame_count);
        }
        require(frames==669,"source facial recipes changed");
    } catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
