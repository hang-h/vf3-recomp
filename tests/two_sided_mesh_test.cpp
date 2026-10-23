// No ROMs: shared two-sided geometry must match explicit opposite-normal copies.
#include "../src/video/r3d_raster.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
video::Scene scene(float angle,float depth,float scale,bool reversed,bool alpha,bool compact)
{
    video::Scene s;video::Viewport v;
    v.vp_w=96;v.vp_h=96;v.sun[2]=1;v.sun_intensity=.7f;v.ambient=.2f;
    video::Model m;m.matrix=video::Mat4::identity();m.scale=std::abs(scale);
    const float c=std::cos(angle),sn=std::sin(angle);
    m.matrix.m[0]=c*scale;m.matrix.m[2]=-sn*scale;
    m.matrix.m[5]=scale;m.matrix.m[8]=sn*scale;m.matrix.m[10]=c*scale;
    m.matrix.m[14]=-depth;
    m.meshes=std::make_shared<std::vector<video::Mesh>>(1);
    auto &mesh=m.meshes->front();mesh.lighting=true;mesh.poly_alpha=alpha;mesh.two_sided=compact;
    const float points[3][3]={{-.8f,-.7f,0},{.8f,-.7f,0},{0,.8f,0}};
    for(int side=0;side<(compact?1:2);++side)for(unsigned i=0;i<3;++i) {
        video::Vertex p{};
        for(unsigned k=0;k<3;++k)p.pos[k]=points[i][k];
        p.normal[2]=p.face_normal[2]=(reversed?-1.f:1.f)*(side?-1.f:1.f);
        p.colour[0]=210;p.colour[1]=150;p.colour[2]=60;p.colour[3]=alpha?160:255;
        mesh.verts.push_back(p);
    }
    v.models.push_back(std::move(m));s.viewports.push_back(std::move(v));return s;
}
video::Scene many_triangles(bool compact,bool alpha)
{
    auto s=scene(.8f,.4f,1,false,alpha,compact);
    auto &meshes=*s.viewports[0].models[0].meshes;
    const auto prototype=meshes[0];meshes.clear();
    video::Mesh mesh=prototype;mesh.verts.clear();
    meshes.push_back(mesh); // empty draws must not affect workload partitioning
    for(unsigned i=0;i<4257;++i) {
        if(compact && i%137==0) {meshes.push_back(mesh);mesh.verts.clear();}
        for(auto v:prototype.verts) {
            v.pos[0]=v.pos[0]/64+(int(i%65)-32)/32.f;
            v.pos[1]=v.pos[1]/64+(int(i/65)-32)/32.f;
            mesh.verts.push_back(v);
        }
    }
    meshes.push_back(std::move(mesh));meshes.emplace_back();return s;
}
}
int main()
{
    try {
        video::Raster a,b;a.resize(96,96,96);b.resize(96,96,96);
        std::vector<uint32_t> under(96*96,0xff203040),pa(96*96),pb(96*96);
        unsigned cases=0,visible=0;
        for(float angle:{0.f,.8f,1.5707963f,2.4f,3.1415927f})
        for(float depth:{.001f,.4f,2.f})for(float scale:{1.f,-1.f,1.7f})
        for(bool reverse:{false,true})for(bool alpha:{false,true}) {
            const auto original=scene(angle,depth,scale,reverse,alpha,false);
            const auto compact=scene(angle,depth,scale,reverse,alpha,true);
            a.render(original,nullptr);b.render(compact,nullptr);
            require(a.tris.size()==b.tris.size(),"two-sided clipping/culling differs from explicit copies");
            for(size_t i=0;i<a.tris.size();++i) {
                const auto &x=a.tris[i],&y=b.tris[i];require(x.n==y.n,"clipped vertex count differs");
                for(int v=0;v<x.n;++v)for(unsigned q=0;q<11;++q)
                    require(x.a[v][q]==y.a[v][q],"GPU-facing prepared attributes changed");
            }
            a.compose(under.data(),pa.data());b.compose(under.data(),pb.data());
            require(pa==pb,"two-sided lighting or pixels changed");
            visible+=pa!=under;++cases;
        }
        require(visible>100,"reference scenes did not draw enough visible geometry");
        for(bool alpha:{false,true}) {
            // Different mesh and triangle counts force different task boundaries.
            // Include a large draw, empty draws, clipping and transparent passes.
            const auto original=many_triangles(false,alpha),compact=many_triangles(true,alpha);
            a.render(original,nullptr);b.render(compact,nullptr);
            require(a.tris.size()==b.tris.size(),"workload splitting lost triangles");
            for(size_t i=0;i<a.tris.size();++i)for(int v=0;v<3;++v) {
                require(a.tris[i].sx[v]==b.tris[i].sx[v] && a.tris[i].sy[v]==b.tris[i].sy[v],
                        "workload splitting changed triangle order or projection");
                for(unsigned q=0;q<11;++q)require(a.tris[i].a[v][q]==b.tris[i].a[v][q],
                        "workload splitting changed GPU attributes");
            }
            a.compose(under.data(),pa.data());b.compose(under.data(),pb.data());
            require(pa==pb,"workload splitting changed pixels");++cases;
        }
        a.prepare({});require(a.tris.empty(),"empty scene retained prepared triangles");
        std::printf("Two-sided meshes: %u exact CPU pixel and GPU preparation comparisons passed\n",cases);
    }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
