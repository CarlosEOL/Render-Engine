#pragma once
#include "Astronomy.h"
#include "PlanetTerrain.h"
#include "glad/glad.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/type_precision.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

unsigned int compileShaderProgram(const char*,const char*);

// Local, deterministic visual ecology. It does not claim observed extrasolar life.
class PlanetVegetation {
public:
    struct Profile {unsigned seed;double radiusMetres,temperature;float seaLevel;bool life;};
    struct Habitat {float elevation=0,grass=0,forest=0;int species=0;bool suitable=false;};
    static float Unit(unsigned value){value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;return (value&0xffffffu)/16777215.f;}
    static const char* ShaderFunctions(){return R"(
float vegetationMoisture(vec3 n,float seed){
 return .53+.22*sin(n.x*13.0+seed*.001)*cos(n.z*17.0-seed*.0007)
  +.12*sin(n.y*31.0+n.z*8.0)-.16*exp(-pow((abs(n.y)-.43)/.12,2.0));
}
)";}
    static Habitat Sample(const Profile& p,const glm::dvec3& direction){
        Habitat h;if(!p.life || p.radiusMetres<1000)return h;
        const auto n=glm::normalize(glm::vec3(direction));
        // Relief one gives the same clamped terrain height used by the material.
        const float height=PlanetTerrain::SurfaceElevation(n,p.seed,0,p.seaLevel,1,0);
        h.elevation=height*.003f;
        const double altitude=(height-p.seaLevel)*.003*p.radiusMetres;
        const double temperature=p.temperature+8-28*n.y*n.y-std::max(altitude,0.)*.005;
        const float moisture=.53f+.22f*std::sin(n.x*13+p.seed*.001f)*std::cos(n.z*17-p.seed*.0007f)
            +.12f*std::sin(n.y*31+n.z*8)-.16f*std::exp(-std::pow((std::abs(n.y)-.43f)/.12f,2));
        h.suitable=height>p.seaLevel+.006f && height<std::max(.18f,p.seaLevel+.11f)
            && altitude<2200 && temperature>265 && temperature<312 && moisture>.30f;
        if(!h.suitable)return h;
        h.species=temperature<281?1:temperature>294 && std::abs(n.y)<.45f?2:0;
        h.forest=glm::smoothstep(.42f,.68f,moisture)*.75f;
        h.grass=(1-glm::smoothstep(.65f,.82f,moisture))*glm::smoothstep(.30f,.42f,moisture);
        return h;
    }
    bool Initialize(){
        const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec3 color;
layout(location=2) in vec4 centreScale;layout(location=3) in vec4 variation;
layout(location=4) in vec2 uv;layout(location=5) in float card;
uniform mat4 viewProjection;uniform mat3 localFrame;uniform float seconds;
out vec3 material,relativeMetres;out vec2 spriteUv;out float mode,fade;
void main(){
 vec3 p=position*centreScale.w;
 float c=cos(variation.x),s=sin(variation.x);p.xz=mat2(c,-s,s,c)*p.xz;
 p.x+=sin(seconds*1.3+variation.y+p.y*.27)*p.y*p.y*(card>.5?.008:.0007);
 vec3 relative=centreScale.xyz+localFrame*p/149597870700.0;
 gl_Position=viewProjection*vec4(relative,1);relativeMetres=relative*149597870700.0;
 material=color*variation.z;spriteUv=uv;mode=card;fade=variation.w;
})";
        const char* fragment=R"(#version 330 core
in vec3 material,relativeMetres;in vec2 spriteUv;in float mode,fade;
uniform vec3 sunlight,radial;uniform int species;out vec4 result;
float hash(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
void main(){
 vec3 albedo=material;
 if(mode>.5){
  vec2 q=spriteUv;float coverage=0;bool trunk=false;
  if(mode<1.5){
   // Six tapered, bent blades per crossed sprite, with real cutout gaps.
   for(int blade=0;blade<6;++blade){
    float centre=.09+float(blade)*.164;
    float height=.51+.48*fract(float(blade)*.618+.17);
    float y=q.y/height;
    float bend=sin(float(blade)*4.1)*y*y*.13;
    float width=.028*pow(max(0.0,1-y),.65);
    float d=abs(q.x-centre-bend);
    float aa=max(fwidth(q.x),.001);
    coverage=max(coverage,(1-smoothstep(width-aa,width+aa,d))*(1-smoothstep(.97,1.0,y)));
   }
   albedo*=mix(.50,1.15,q.y);
  }else{
   float x=q.x-.5,y=q.y;
   trunk=abs(x)<.026 && y<.68;
   if(species==1)coverage=y>.2 && abs(x)<(.48*(1-y))?1:0;
   else if(species==2){
    coverage=abs(x)<.43 && abs(y-.75)<.055+.17*pow(abs(x)/.43,1.3)?1:0;
    trunk=abs(x-.035*sin(y*3.0))<.025 && y<.80;
   }else coverage=pow(x/.39,2)+pow((y-.68)/.31,2)<1?1:0;
   if(trunk){coverage=1;albedo=vec3(.20,.12,.055);}else albedo=species==1?vec3(.12,.27,.105):vec3(.22,.37,.11);
  }
  if(coverage<.5)discard;
 }
 if(hash(floor(gl_FragCoord.xy))>fade)discard;
 vec3 normal=normalize(radial);
 if(mode<.5){vec3 n=cross(dFdx(relativeMetres),dFdy(relativeMetres));if(dot(n,n)>1e-12)normal=normalize(n);if(dot(normal,-relativeMetres)<0)normal=-normal;}
 float light=.30+.70*max(dot(normal,normalize(sunlight)),0.0);
 result=vec4(albedo*light,0);
 gl_FragDepth=clamp(log2(1.0+length(relativeMetres)/149597870700.0*1e12)/log2(1.0+20000.0*1e12),0.0,.999998);
})";
        program=compileShaderProgram(vertex,fragment);int linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){Release();return false;}
        for(int species=0;species<3;++species)Upload(meshes[species],Tree(species));
        Upload(meshes[3],Cards(false));Upload(meshes[4],Cards(true));return true;
    }
    void Render(const Profile& p,const glm::dmat3& rotation,const glm::dvec3& centreRelativeAu,
        const glm::mat4& vp,const glm::vec3& worldFront,double seconds){
        lastTrees=lastGrass=0;if(!program || !p.life)return;
        const auto cameraLocal=glm::transpose(rotation)*(-centreRelativeAu)*Astronomy::metresPerAu;
        const auto normal=glm::normalize(cameraLocal);
        const auto here=Sample(p,normal);
        const double altitude=glm::length(cameraLocal)-p.radiusMetres*(1+here.elevation);
        if(altitude>1800 || altitude<-20)return;
        const glm::i64vec3 key(glm::floor(cameraLocal/64.));
        if(cachedSeed!=p.seed || cachedRadius!=p.radiusMetres || key!=cachedKey){Generate(p,normal);cachedKey=key;cachedSeed=p.seed;cachedRadius=p.radiusMetres;}
        const auto ref=std::abs(normal.y)<.999999?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
        const auto east=glm::normalize(glm::cross(ref,normal)),north=glm::cross(normal,east);
        const glm::mat3 frame(rotation*glm::dmat3(east,normal,north));
        std::array<std::vector<Instance>,7> batches;
        const auto submit=[&](const Plant& plant,bool grass){
            const auto relative=rotation*(plant.root-cameraLocal);const double distance=glm::length(relative);
            const double renderDistance=grass?85.:900.;if(distance>renderDistance)return;
            if(glm::dot(relative,glm::dvec3(worldFront))<-(grass?2.:18.))return;
            const auto clip=vp*glm::vec4(glm::vec3(relative/Astronomy::metresPerAu),1);
            if(clip.w>0 && (std::abs(clip.x)>clip.w*1.18 || std::abs(clip.y)>clip.w*1.35))return;
            const int batch=grass?6:plant.species+(distance>260?3:0);
            if(!grass && lastTrees>=2000)return;
            auto& visible=batches[batch];if(visible.size()>=static_cast<size_t>(grass?6000:1200))return;
            const float fade=1-glm::smoothstep(static_cast<float>(grass?50:650),static_cast<float>(renderDistance),static_cast<float>(distance));
            visible.push_back({glm::vec4(glm::vec3(relative/Astronomy::metresPerAu),plant.scale),{plant.angle,plant.phase,plant.tint,fade}});
            if(grass)++lastGrass;else ++lastTrees;
        };
        for(const auto& plant:trees)submit(plant,false);for(const auto& plant:grass)submit(plant,true);
        glUseProgram(program);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
        glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
        glUniformMatrix3fv(glGetUniformLocation(program,"localFrame"),1,GL_FALSE,glm::value_ptr(frame));
        const auto radial=glm::vec3(rotation*normal);
        // The host is at the system origin; caller supplies its planet-to-star vector.
        glUniform3fv(glGetUniformLocation(program,"radial"),1,glm::value_ptr(radial));
        glUniform3fv(glGetUniformLocation(program,"sunlight"),1,glm::value_ptr(lightDirection));
        glUniform1f(glGetUniformLocation(program,"seconds"),static_cast<float>(std::remainder(seconds,glm::two_pi<double>()/1.3)));
        for(int batch=0;batch<7;++batch){
            auto& visible=batches[batch];if(visible.empty())continue;
            auto& mesh=meshes[batch<3?batch:batch<6?3:4];
            glUniform1i(glGetUniformLocation(program,"species"),batch%3);
            glBindVertexArray(mesh.vao);glBindBuffer(GL_ARRAY_BUFFER,mesh.instances);
            glBufferData(GL_ARRAY_BUFFER,visible.size()*sizeof(Instance),visible.data(),GL_DYNAMIC_DRAW);
            glDrawArraysInstanced(GL_TRIANGLES,0,mesh.count,static_cast<int>(visible.size()));
        }
        glBindVertexArray(0);
    }
    void SetSunlight(const glm::vec3& value){lightDirection=value;}
    size_t LastTrees()const{return lastTrees;}size_t LastGrass()const{return lastGrass;}
    void ClearCounts(){lastTrees=lastGrass=0;}
    void Release(){for(auto& mesh:meshes){if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);if(mesh.vertices)glDeleteBuffers(1,&mesh.vertices);if(mesh.instances)glDeleteBuffers(1,&mesh.instances);mesh={};}if(program)glDeleteProgram(program);program=0;trees.clear();grass.clear();cachedSeed=~0u;}
private:
    struct Vertex{glm::vec3 p,color;glm::vec2 uv;float card;};
    struct Instance{glm::vec4 centreScale,variation;};
    struct Mesh{unsigned vao=0,vertices=0,instances=0;int count=0;};
    struct Plant{glm::dvec3 root;float scale,angle,phase,tint;int species;};
    static glm::dvec3 Face(int face,double u,double v){
        if(u*u+v*v>=1)return glm::dvec3(0);const double a=std::sqrt(1-u*u-v*v)*(face%2?-1:1);
        switch(face/2){case 0:return {a,u,v};case 1:return {u,a,v};default:return {u,v,a};}
    }
    void Generate(const Profile& p,const glm::dvec3& normal){
        trees.clear();grass.clear();
        for(int face=0;face<6;++face){
            const int axis=face/2;const double component=normal[axis]*(face%2?-1:1);if(component<.5)continue;
            // Orthographic chart coordinates have at least the requested metre
            // footprint even at cube corners; ratio charts shrink it threefold.
            const glm::dvec2 uv=axis==0?glm::dvec2(normal.y,normal.z):axis==1?glm::dvec2(normal.x,normal.z):glm::dvec2(normal.x,normal.y);
            const auto generate=[&](double spacing,int extent,bool grassPatch){
                const glm::ivec2 centre(glm::floor(uv*p.radiusMetres/spacing));
                for(int dy=-extent;dy<=extent;++dy)for(int dx=-extent;dx<=extent;++dx){
                    const int ix=centre.x+dx,iy=centre.y+dy;
                    const unsigned hash=p.seed^static_cast<unsigned>(ix)*73856093u^static_cast<unsigned>(iy)*19349663u^face*83492791u;
                    const double u=(ix+(grassPatch?.5:.15+.7*Unit(hash+3)))*spacing/p.radiusMetres,v=(iy+(grassPatch?.5:.15+.7*Unit(hash+7)))*spacing/p.radiusMetres;
                    if(std::abs(u)>1 || std::abs(v)>1)continue;
                    const auto n=Face(face,u,v);if(glm::length(n)<.5 || std::abs(n[axis])+1e-12<std::max({std::abs(n.x),std::abs(n.y),std::abs(n.z)}))continue;
                    const auto h=Sample(p,n);if(!h.suitable)continue;
                    const auto reference=std::abs(n.y)<.999999?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
                    const auto east=glm::normalize(glm::cross(reference,n)),north=glm::cross(n,east);
                    const auto e=Sample(p,glm::normalize(n+east*(8/p.radiusMetres))),t=Sample(p,glm::normalize(n+north*(8/p.radiusMetres)));
                    const double slopeE=(e.elevation-h.elevation)*p.radiusMetres/8,slopeN=(t.elevation-h.elevation)*p.radiusMetres/8;
                    if(std::hypot(slopeE,slopeN)>.55)continue;
                    if(!grassPatch){
                        if(Unit(hash+23)>std::min(1.f,(h.forest+.12f)/static_cast<float>(std::abs(n[axis]))))continue;
                        trees.push_back({n*(p.radiusMetres*(1+h.elevation)-.25),.7f+.65f*Unit(hash+29),Unit(hash+31)*glm::two_pi<float>(),Unit(hash+37)*6.28f,.8f+.35f*Unit(hash+41),h.species});
                    }else if(h.grass>.05f){
                        for(int j=0;j<8;++j)for(int i=0;i<8;++i){
                            const unsigned seed=hash+i*71+j*1777;
                            if(Unit(seed+9)>std::min(1.f,h.grass/static_cast<float>(std::abs(n[axis]))))continue;
                            const double x=(i-3.5)*2+(Unit(seed+19)-.5)*1.5,z=(j-3.5)*2+(Unit(seed+43)-.5)*1.5;
                            const auto q=Face(face,u+x/p.radiusMetres,v+z/p.radiusMetres);
                            if(glm::length(q)<.5 || std::abs(q[axis])+1e-12<std::max({std::abs(q.x),std::abs(q.y),std::abs(q.z)}))continue;
                            const auto delta=(q-n)*p.radiusMetres;
                            const double elevation=h.elevation+(slopeE*glm::dot(delta,east)+slopeN*glm::dot(delta,north))/p.radiusMetres;
                            grass.push_back({q*(p.radiusMetres*(1+elevation)-.06),.45f+.65f*Unit(seed+59),Unit(seed+13)*6.28f,Unit(seed+61)*6.28f,.75f+.5f*Unit(seed+67),0});
                        }
                    }
                }
            };
            generate(40,28,false);generate(16,10,true);
        }
    }
    static std::vector<Vertex> Cards(bool grass){
        std::vector<Vertex> result;const float width=grass?1.7f:9.f,height=grass?1.15f:15.f,mode=grass?1.f:2.f;
        for(int plane=0;plane<2;++plane){const float angle=plane*glm::half_pi<float>();const glm::vec3 right(std::cos(angle),0,std::sin(angle));
            const std::array<glm::vec2,6> uv={glm::vec2(0,0),{1,0},{1,1},{0,0},{1,1},{0,1}};
            for(auto q:uv)result.push_back({right*((q.x-.5f)*width)+glm::vec3(0,q.y*height,0),grass?glm::vec3(.26,.40,.095):glm::vec3(1),q,mode});}
        return result;
    }
    static std::vector<Vertex> Tree(int species){
        std::vector<Vertex> result;
        const auto tri=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 color){for(auto p:{a,b,c})result.push_back({p,color,{0,0},0});};
        const auto cone=[&](glm::vec3 centre,float base,float top,float height,glm::vec3 color){for(int i=0;i<8;++i){float a=i*glm::two_pi<float>()/8,b=(i+1)*glm::two_pi<float>()/8;
            const auto p=centre+glm::vec3(std::cos(a)*base,0,std::sin(a)*base),q=centre+glm::vec3(std::cos(b)*base,0,std::sin(b)*base);
            const auto r=centre+glm::vec3(std::cos(a)*top,height,std::sin(a)*top),s=centre+glm::vec3(std::cos(b)*top,height,std::sin(b)*top);
            tri(p,q,r,color);if(top>0)tri(q,s,r,color);tri(centre,q,p,color);}}
        ;
        cone({0,0,0},species==2?.23f:.38f,.13f,species==2?11.f:8.f,{.23,.14,.065});
        if(species==1){for(int tier=0;tier<4;++tier)cone({0,3.f+tier*2.4f,0},3.8f-tier*.62f,0,5.f,{.10f,.25f+tier*.025f,.10f});}
        else if(species==2){for(int frond=0;frond<9;++frond){float a=frond*glm::two_pi<float>()/9;const glm::vec3 r(std::cos(a),0,std::sin(a)),side(-r.z,0,r.x);
            for(int segment=0;segment<3;++segment){const float t=segment/3.f,u=(segment+1)/3.f;const auto p=r*(t*5)+glm::vec3(0,11.5f+2.5f*std::sin(t*3.14f)-t*2.2f,0),q=r*(u*5)+glm::vec3(0,11.5f+2.5f*std::sin(u*3.14f)-u*2.2f,0);
                const float w=.8f*(1-t),v=.8f*(1-u);tri(p-side*w,p+side*w,q-side*v,{.25,.40,.10});tri(p+side*w,q+side*v,q-side*v,{.22,.35,.08});}}}
        else{for(const glm::vec3 offset:{glm::vec3(0,9,0),glm::vec3(-2.3,7.5,0),glm::vec3(2.2,8,1),glm::vec3(0,8,-2)}){
            const auto sphere=PlanetTerrain::SphereVertices(4,7);for(auto p:sphere)result.push_back({offset+p*glm::vec3(3.5,3.2,3.5),{.20,.35,.10},{0,0},0});}}
        return result;
    }
    static void Upload(Mesh& mesh,const std::vector<Vertex>& vertices){
        mesh.count=static_cast<int>(vertices.size());glGenVertexArrays(1,&mesh.vao);glGenBuffers(1,&mesh.vertices);glGenBuffers(1,&mesh.instances);
        glBindVertexArray(mesh.vao);glBindBuffer(GL_ARRAY_BUFFER,mesh.vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,color)));glEnableVertexAttribArray(1);
        glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,uv)));glEnableVertexAttribArray(4);
        glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,card)));glEnableVertexAttribArray(5);
        glBindBuffer(GL_ARRAY_BUFFER,mesh.instances);for(unsigned i=0;i<2;++i){glVertexAttribPointer(2+i,4,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(i*sizeof(glm::vec4)));glEnableVertexAttribArray(2+i);glVertexAttribDivisor(2+i,1);}glBindVertexArray(0);
    }
    unsigned program=0;std::array<Mesh,5> meshes{};std::vector<Plant> trees,grass;
    unsigned cachedSeed=~0u;double cachedRadius=0;glm::i64vec3 cachedKey{std::numeric_limits<std::int64_t>::min()};
    size_t lastTrees=0,lastGrass=0;glm::vec3 lightDirection{0,1,0};
};
