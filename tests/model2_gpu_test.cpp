// ROM-free CPU/GPU comparison, including imported texture residency/lifetime.
#include "../src/gpu/r3d_gpu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

static void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
static video::Scene picture(std::shared_ptr<const Model2Texture> texture)
{
    video::Scene scene;video::Viewport vp;vp.ambient=1;
    vp.spot_ellipse[2]=vp.spot_ellipse[3]=1;
    video::Model model;model.matrix=video::Mat4::identity();model.matrix.m[14]=-2;
    model.meshes=std::make_shared<std::vector<video::Mesh>>(1);
    auto &mesh=model.meshes->front();mesh.textured=true;mesh.alpha_test=true;
    mesh.model2_texture=texture;mesh.width=texture?texture->width:32;mesh.height=texture?texture->height:32;
    mesh.format=7;
    constexpr float xy[4][2]={{-2,-2},{2,-2},{2,2},{-2,2}};
    for(unsigned i:{0u,1u,2u,0u,2u,3u}) {
        video::Vertex v{};v.pos[0]=xy[i][0];v.pos[1]=xy[i][1];
        v.normal[2]=v.face_normal[2]=1;
        v.uv[0]=(xy[i][0]+2)/4*(texture?texture->width*3:1);
        v.uv[1]=(xy[i][1]+2)/4*(texture?texture->height*3:1);
        for(auto &c:v.colour)c=255;mesh.verts.push_back(v);
    }
    vp.models.push_back(model);scene.viewports.push_back(vp);return scene;
}
int main()
{
    SDL_GPUDevice *device=nullptr;
    try {
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_DXIL,false,nullptr);
        require(device,SDL_GetError());
        {
            R3dGpu gpu;require(gpu.init(device),gpu.error().c_str());require(gpu.output(128,128),gpu.error().c_str());
            video::Raster cpu;cpu.resize(128,128,128);
            std::vector<uint16_t> ram(2048*2048,0xffff);
            std::vector<m3::Model3::TexUpload> uploads;
            unsigned cases=0;
            auto check=[&](const video::Scene &scene) {
                cpu.render(scene,ram.data());
                require(gpu.render(scene,cpu,ram,uploads),gpu.error().c_str());
                std::vector<uint32_t> actual;require(gpu.read_output(actual),gpu.error().c_str());
                std::vector<uint8_t> expected(128*128*4);cpu.to_premultiplied(expected.data(),128*4,1);
                const auto *bytes=reinterpret_cast<const uint8_t*>(actual.data());
                unsigned differences=0;
                for(size_t i=0;i<expected.size();++i)differences+=std::abs(int(bytes[i])-int(expected[i]))>1;
                if(differences)std::fprintf(stderr,"case %u: %u channel differences\n",cases,differences);
                require(differences==0,"GPU indexed texture differs from CPU sampler");++cases;
            };
            check(picture({}));
            std::shared_ptr<const Model2Texture> retained;
            for(unsigned mode=0;mode<16;++mode) {
                auto texture=std::make_shared<Model2Texture>();texture->width=texture->height=32;
                texture->flags=0x4000|((mode&1)?0x12000:0)|((mode&2)?0x100:0)|((mode&4)?0x200:0)|((mode&8)?0xc0:0);
                for(unsigned i=0;i<128;++i)texture->words.push_back(0xff000000u|((i*37%256)<<16)|((i*13%256)<<8)|(i*7%256));
                for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)texture->words.push_back((x/2+y/4)%16);
                if(!mode)retained=texture;
                check(picture(texture));check(picture(texture));
            }
            // Nonlinear colour curves must see native material luminance, not
            // a final RGB multiply. Include unlit facial ink and full sun.
            for(unsigned parameters:{0x5f7fu,0x9e57u,0xff00u})for(float sun:{-1.f,0.f,.25f,.5f,1.f}) {
                auto texture=std::make_shared<Model2Texture>(*retained);
                texture->flags|=Model2Texture::NativeLighting;
                for(unsigned i=0;i<64;++i)texture->words.push_back(0xff000000u|((i*4)<<16)|((63-i)*4<<8)|(i*i%256));
                for(unsigned i=0;i<128;++i)texture->words.push_back(i/2);
                texture->words.push_back(parameters);
                auto scene=picture(texture);auto &vp=scene.viewports.front();
                vp.sun[2]=sun;vp.ambient=.3f;vp.sun_intensity=.4f;
                vp.models.front().meshes->front().lighting=true;
                check(scene);
            }
            // Facial ink lies just behind the skin's different triangulation.
            // Bias depth alone: keep projection/UVs, transparent holes and a
            // genuinely nearer occluder, independently of draw order.
            auto ink=std::make_shared<Model2Texture>();ink->width=ink->height=32;ink->flags=0x6000;
            ink->words.resize(128+32*32,15);
            std::fill_n(ink->words.begin(),128,0xff00ff00u);
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<16;++x)ink->words[128+y*32+x]=1;
            for(bool reverse:{false,true}) {
                auto scene=picture(ink);auto &model=scene.viewports[0].models[0];
                auto &eye=model.meshes->front();eye.depth_bias=.004f;
                auto skin=eye;skin.depth_bias=0;skin.model2_texture.reset();skin.textured=false;
                for(auto &v:skin.verts){v.pos[2]=.001f;v.pos[0]=v.pos[0]*1.1f+.0125f;v.pos[1]*=1.1f;v.colour[0]=255;v.colour[1]=v.colour[2]=0;}
                model.meshes->push_back(skin);
                auto cover=skin;
                for(auto &v:cover.verts){v.pos[2]=.1f;v.pos[0]=v.pos[0]*.1f;v.colour[0]=0;v.colour[2]=255;}
                model.meshes->push_back(cover);
                if(reverse)std::reverse(model.meshes->begin(),model.meshes->end());
                check(scene);
                require(cpu.base[64*128+10].g>.9f,"skin clips biased eye ink");
                require(cpu.base[64*128+30].r>.9f,"transparent eye hole hides skin");
                require(cpu.base[64*128+64].b>.9f,"decal ignores nearer occluder");
                const auto biased=cpu.tris;
                auto plain=scene;plain.viewports[0].models[0].meshes=std::make_shared<std::vector<video::Mesh>>(*model.meshes);
                for(auto &m:*plain.viewports[0].models[0].meshes)m.depth_bias=0;
                cpu.prepare(plain);require(cpu.tris.size()==biased.size(),"bias changed clipping");
                for(size_t i=0;i<biased.size();++i) {
                    const auto &a=biased[i],&b=cpu.tris[i];
                    require(!std::memcmp(a.sx,b.sx,sizeof a.sx) && !std::memcmp(a.sy,b.sy,sizeof a.sy) &&
                            !std::memcmp(a.iw,b.iw,sizeof a.iw) && !std::memcmp(a.a,b.a,sizeof a.a),
                            "depth bias moved geometry or changed perspective attributes");
                }
            }
            // Force buffer growth and bounded-cache eviction, then revisit a
            // retained scene and native texture RAM after subsequent uploads.
            for(unsigned i=0;i<10;++i) {
                auto texture=std::make_shared<Model2Texture>(*retained);
                texture->width=texture->height=1024;texture->words.resize(128+1024*1024,1);
                texture->words[8]=0xff123400u+i;check(picture(texture));
            }
            check(picture(retained));check(picture({}));
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)ram[y*2048+x]=0xf00f;
            uploads.push_back({0,0,32,32});check(picture({}));check(picture(retained));
            std::printf("Indexed textures: %u CPU/GPU comparisons, cache reuse, growth, eviction and native RAM passed\n",cases);
        }
        SDL_DestroyGPUDevice(device);SDL_Quit();return 0;
    }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
