#pragma once
#include "Astronomy.h"
#include "PlanetTerrain.h"
#include "PlanetWeather.h"
#include "TreeGeometry.h"
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
    struct Profile {unsigned seed;double radiusMetres,temperature;float seaLevel;bool life;bool earthPoles=false;};
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
        const float height=PlanetTerrain::SurfaceElevation(n,p.seed,0,p.seaLevel,1,0,false,nullptr,p.earthPoles);
        h.elevation=height*.003f;
        if(p.earthPoles && (n.y>.90f || n.y<-.84f))return h;
        const double altitude=(height-p.seaLevel)*.003*p.radiusMetres;
        const double temperature=p.temperature+8-28*n.y*n.y-std::max(altitude,0.)*.005;
        const float moisture=.53f+.22f*std::sin(n.x*13+p.seed*.001f)*std::cos(n.z*17-p.seed*.0007f)
            +.12f*std::sin(n.y*31+n.z*8)-.16f*std::exp(-std::pow((std::abs(n.y)-.43f)/.12f,2));
        const bool mapped=p.earthPoles && PlanetTerrain::HasEarthMap();
        const auto geography=mapped?PlanetTerrain::EarthGeography(n):glm::vec3(0);
        const bool dryLand=mapped?std::max(geography.y,geography.z)<.15f:height>p.seaLevel+.006f;
        h.suitable=dryLand && height<std::max(.18f,p.seaLevel+.11f)
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
layout(location=6) in vec3 vertexNormal;
layout(location=7) in vec2 windBend;
uniform mat4 viewProjection;uniform mat3 localFrame;uniform int species;
out vec3 material,relativeMetres,worldNormal;out vec2 spriteUv;out float mode,fade;
void main(){
 vec3 p=position*centreScale.w;
 vec3 n=vertexNormal;
 float c=cos(variation.x),s=sin(variation.x);
 mat2 turn=mat2(c,-s,s,c);p.xz=turn*p.xz;n.xz=turn*n.xz;
 bool grass=card>.5 && card<1.5;
 float height=grass?1.15:(species==1?15.6:13.3);
 float flex=max(position.y,0.0)/height;
 p.xz+=windBend*(flex*flex*centreScale.w);
 // Transform the smooth normal by the inverse transpose of the bend.
 n.y-=2.0*flex*dot(windBend,n.xz)/height;
 vec3 relative=centreScale.xyz+localFrame*p/149597870700.0;
 gl_Position=viewProjection*vec4(relative,1);relativeMetres=relative*149597870700.0;
 worldNormal=localFrame*n;material=color*variation.z;spriteUv=uv;mode=card;fade=variation.w;
})";
        const char* fragment=R"(#version 330 core
in vec3 material,relativeMetres,worldNormal;in vec2 spriteUv;in float mode,fade;
uniform vec3 sunlight,radial;uniform int species;out vec4 result;
float hash(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
float ellipse(vec2 p,vec2 radii){return 1.0-smoothstep(.87,1.08,length(p/radii));}
float spray(vec2 q,int kind){
 float x=abs(q.x);
 float stem=1.0-smoothstep(.015,.045,x);
 if(kind==1){
  float spread=.90*pow(max(0.0,1.0-q.y),.58)*smoothstep(0.0,.16,q.y);
  float rows=q.y*13.0-x*2.10+.11*sin(q.y*27.0+x*5.0);
  float teeth=1.0-smoothstep(.24,.44,abs(fract(rows)-.5));
  float envelope=1.0-smoothstep(spread-.03,spread+.02,x);
  return max(stem,envelope*mix(teeth,1.0,1.0-smoothstep(.04,spread+.08,x)));
 }
 float leaves=0.0;
 for(int leaf=0;leaf<7;++leaf){
  float t=float(leaf)/6.0;
  float span=(.67+.24*sin(float(leaf)*1.86))*(1.0-.52*t);
  float y=.10+.12*float(leaf)+.017*step(0.0,q.x);
  vec2 d=vec2(x-span*.48,q.y-y-x*.14);
  leaves=max(leaves,ellipse(d,vec2(span*.53,.076-.019*t)));
 }
 return max(stem,leaves);
}
void main(){
 float noise=hash(floor(gl_FragCoord.xy));
 if(fade>=0.0?noise>fade:noise<=-fade)discard;
 vec3 albedo=material;
 if(mode>.5 && mode<3.5){
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
  }else if(mode<2.5){
   float x=q.x-.5,y=q.y;
   trunk=abs(x)<mix(.034,.010,y) && y<.78;
   if(species==1){
    float tiers=.70+.30*fract(y*10.0);
    coverage=(1.0-smoothstep(.0,.021,abs(x)-.56*(1.0-y)*tiers))*smoothstep(.13,.22,y);
   }
   else if(species==2){
    float span=abs(x);
    float fronds=abs(y-(.82+.54*span-1.52*span*span));
    coverage=(1.0-smoothstep(.026,.07,fronds))*(1.0-smoothstep(.39,.49,span));
    coverage=max(coverage,ellipse(vec2(x,y-.83),vec2(.25,.105)));
    trunk=abs(x-.07*y*y)<mix(.032,.010,y) && y<.86;
   }else{
    coverage=ellipse(vec2(x+.19,y-.61),vec2(.25,.23));
    coverage=max(coverage,ellipse(vec2(x-.18,y-.68),vec2(.27,.23)));
    coverage=max(coverage,ellipse(vec2(x+.034,y-.80),vec2(.29,.19)));
   }
   vec3 foliage=species==1?vec3(.095,.225,.097):vec3(.16,.31,.078);
   float mottling=.86+.10*sin(x*53.0+y*21.0)*sin(y*38.0-x*19.0);
   float openings=sin(x*58.0+y*27.0)*sin(y*75.0-x*34.0);
   coverage-=.58*smoothstep(.23,.80,openings)*(1.0-smoothstep(.16,.94,1.0-y));
   if(trunk && coverage<.5){coverage=1.0;albedo*=vec3(.20,.12,.055);}
   else albedo*=foliage*mottling;
  }else{
   vec2 leafUv=vec2(q.x*2.0-1.0,q.y);
   coverage=spray(leafUv,species);
   albedo*=.88+.21*q.y+.10*sin(q.y*39.0+abs(leafUv.x)*3.0);
  }
  if(coverage<.5)discard;
 }
 if(mode<.5){
  float ribs=sin(spriteUv.x*81.681+sin(spriteUv.y*1.7)*.68);
  float grain=sin(spriteUv.x*150.796+spriteUv.y*.64);
  albedo*=.76+.15*smoothstep(-.62,.46,ribs)+.10*smoothstep(-.72,.55,grain);
  if(species==2){
   float rings=spriteUv.y*4.4+.12*sin(spriteUv.x*6.283+spriteUv.y*.73)+.15*sin(spriteUv.y*3.9);
   albedo*=mix(.66,1.0,smoothstep(.03,.28,fract(rings)));
  }
 }
 if(mode>3.5)albedo*=.84+.26*spriteUv.y+.06*(1.0-abs(spriteUv.x*2.0-1.0));
 vec3 up=normalize(radial);
 vec3 view=normalize(-relativeMetres);
 vec3 normal=normalize(worldNormal);
 if(mode>.5 && dot(normal,view)<0.0)normal=-normal;
 if(mode>.5 && mode<2.5)normal=normalize(normal+up*(mode<1.5?.36:1.3));
 vec3 sun=normalize(sunlight);
 float sunHeight=dot(up,sun);
 float diffuse=dot(normal,sun);
 // The ground blocks direct sunlight below the horizon. The sky still
 // contributes blue fill at twilight and a faint base level at night.
 float sky=smoothstep(-.17,.37,sunHeight);
 float daylight=smoothstep(-.004,.024,sunHeight);
 vec3 light=vec3(.007,.010,.016)+sky*vec3(.18,.23,.30);
 vec3 direct=mix(vec3(.48,.22,.085),vec3(1.0,.97,.90),smoothstep(0.0,.38,sunHeight))*daylight;
 light+=direct*max(diffuse,0.0);
 if(mode>.5){
  // A leaf facing away from the sun can glow on the side facing the viewer.
  float transmitted=max(-diffuse,0.0);
  light+=direct*transmitted*vec3(.49,.66,.26);
 }
 if(mode>.5 && mode<1.5)light*=mix(.58,1.0,spriteUv.y);
 result=vec4(albedo*light,0);
 gl_FragDepth=clamp(log2(1.0+length(relativeMetres)/149597870700.0*1e12)/log2(1.0+20000.0*1e12),0.0,.999998);
})";
        program=compileShaderProgram(vertex,fragment);int linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){Release();return false;}
        for(int species=0;species<3;++species){
            Upload(meshes[species],TreeGeometry::Make(species,true));
            Upload(meshes[species+3],TreeGeometry::Make(species,false));
            Upload(meshes[species+6],Cards(false,species));
        }
        Upload(meshes[9],Cards(true,0));return true;
    }
    void Render(const Profile& p,const glm::dmat3& rotation,const glm::dvec3& centreRelativeAu,
        const glm::mat4& vp,const glm::vec3& worldFront,double seconds,const PlanetWeather::State& weather){
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
        const auto surfaceWind=glm::dvec3(PlanetWeather::SurfaceWind(weather,glm::vec3(normal)));
        const glm::vec2 wind(glm::dot(surfaceWind,east),glm::dot(surfaceWind,north));
        const float speed=glm::length(wind);
        const float load=speed*speed/(speed*speed+144.f);
        const glm::vec2 downwind=wind/std::max(speed,1e-6f),crosswind(-downwind.y,downwind.x);
        // Anchor gusts in planet coordinates so they travel across stationary plants.
        const auto prevailing=glm::dvec3(weather.spectrum[0]);
        const double tau=glm::two_pi<double>();
        const double gustPhase=std::remainder(seconds*.58,tau);
        const double flutterPhase=std::remainder(seconds*2.23,tau);
        std::array<std::vector<Instance>,10> batches;
        const auto submit=[&](const Plant& plant,bool grass){
            const auto relative=rotation*(plant.root-cameraLocal);const double distance=glm::length(relative);
            const double renderDistance=grass?85.:900.;if(distance>renderDistance)return;
            if(glm::dot(relative,glm::dvec3(worldFront))<-(grass?2.:18.))return;
            const auto clip=vp*glm::vec4(glm::vec3(relative/Astronomy::metresPerAu),1);
            if(clip.w>0 && (std::abs(clip.x)>clip.w*1.18 || std::abs(clip.y)>clip.w*1.35))return;
            if(!grass && lastTrees>=2000)return;
            glm::vec2 bend(0);
            if(load>0){
                const double space=glm::dot(plant.root,prevailing);
                const float gust=static_cast<float>(std::sin(std::remainder(space*2.60,tau)-gustPhase));
                const float flutter=static_cast<float>(std::sin(std::remainder(space*7.23,tau)-flutterPhase+plant.phase));
                const float flex=grass?.74f:plant.species==1?1.10f:plant.species==2?2.45f:1.65f;
                bend=(downwind*(.65f+.29f*gust+.10f*flutter)+crosswind*(.14f*flutter))*(load*flex);
            }
            const auto emit=[&](int batch,float fade){
                auto& visible=batches[batch];
                if(visible.size()>=static_cast<size_t>(grass?6000:1200))return false;
                visible.push_back({glm::vec4(glm::vec3(relative/Astronomy::metresPerAu),plant.scale),
                    {plant.angle,plant.phase,plant.tint,fade},bend});
                return true;
            };
            const float metres=static_cast<float>(distance);
            if(grass){
                if(emit(9,1-glm::smoothstep(50.f,85.f,metres)))++lastGrass;
                return;
            }
            const float close=1-glm::smoothstep(75.f,105.f,metres);
            const float medium=1-glm::smoothstep(275.f,340.f,metres);
            bool submitted=false;
            // A negative threshold selects the other half of the same screen-space mask.
            if(close>0)submitted=emit(plant.species,close);
            if(close<1 && medium>0)submitted=emit(plant.species+3,close>0?-close:medium)||submitted;
            if(medium<1){
                const float fade=1-glm::smoothstep(650.f,900.f,metres);
                submitted=emit(plant.species+6,medium>0?-medium:fade)||submitted;
            }
            if(submitted)++lastTrees;
        };
        for(const auto& plant:trees)submit(plant,false);for(const auto& plant:grass)submit(plant,true);
        const bool culling=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
        glUseProgram(program);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
        glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
        glUniformMatrix3fv(glGetUniformLocation(program,"localFrame"),1,GL_FALSE,glm::value_ptr(frame));
        const auto radial=glm::vec3(rotation*normal);
        // The host is at the system origin; caller supplies its planet-to-star vector.
        glUniform3fv(glGetUniformLocation(program,"radial"),1,glm::value_ptr(radial));
        glUniform3fv(glGetUniformLocation(program,"sunlight"),1,glm::value_ptr(lightDirection));
        for(int batch=0;batch<10;++batch){
            auto& visible=batches[batch];if(visible.empty())continue;
            auto& mesh=meshes[batch];
            glUniform1i(glGetUniformLocation(program,"species"),batch%3);
            glBindVertexArray(mesh.vao);glBindBuffer(GL_ARRAY_BUFFER,mesh.instances);
            glBufferData(GL_ARRAY_BUFFER,visible.size()*sizeof(Instance),visible.data(),GL_DYNAMIC_DRAW);
            glDrawArraysInstanced(GL_TRIANGLES,0,mesh.count,static_cast<int>(visible.size()));
        }
        glBindVertexArray(0);
        if(culling)glEnable(GL_CULL_FACE);
    }
    void SetSunlight(const glm::vec3& value){lightDirection=value;}
    size_t LastTrees()const{return lastTrees;}size_t LastGrass()const{return lastGrass;}
    void ClearCounts(){lastTrees=lastGrass=0;}
    void Release(){for(auto& mesh:meshes){if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao);if(mesh.vertices)glDeleteBuffers(1,&mesh.vertices);if(mesh.instances)glDeleteBuffers(1,&mesh.instances);mesh={};}if(program)glDeleteProgram(program);program=0;trees.clear();grass.clear();cachedSeed=~0u;}
private:
    using Vertex=TreeGeometry::Vertex;
    struct Instance{glm::vec4 centreScale,variation;glm::vec2 bend;};
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
    static std::vector<Vertex> Cards(bool grass,int species){
        const float width=grass?1.7f:species==1?9.f:12.f;
        const float height=grass?1.15f:species==1?15.6f:13.3f;
        const float mode=grass?1.f:2.f;
        std::vector<Vertex> result;
        for(int plane=0;plane<2;++plane){const float angle=plane*glm::half_pi<float>();const glm::vec3 right(std::cos(angle),0,std::sin(angle));
            const std::array<glm::vec2,6> uv={glm::vec2(0,0),{1,0},{1,1},{0,0},{1,1},{0,1}};
            const auto normal=glm::cross(right,glm::vec3(0,1,0));
            for(auto q:uv)result.push_back({right*((q.x-.5f)*width)+glm::vec3(0,q.y*height,0),grass?glm::vec3(.26,.40,.095):glm::vec3(1),q,mode,normal});}
        return result;
    }
    static void Upload(Mesh& mesh,const std::vector<Vertex>& vertices){
        mesh.count=static_cast<int>(vertices.size());glGenVertexArrays(1,&mesh.vao);glGenBuffers(1,&mesh.vertices);glGenBuffers(1,&mesh.instances);
        glBindVertexArray(mesh.vao);glBindBuffer(GL_ARRAY_BUFFER,mesh.vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,color)));glEnableVertexAttribArray(1);
        glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,uv)));glEnableVertexAttribArray(4);
        glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,card)));glEnableVertexAttribArray(5);
        glVertexAttribPointer(6,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));glEnableVertexAttribArray(6);
        glBindBuffer(GL_ARRAY_BUFFER,mesh.instances);for(unsigned i=0;i<2;++i){glVertexAttribPointer(2+i,4,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(i*sizeof(glm::vec4)));glEnableVertexAttribArray(2+i);glVertexAttribDivisor(2+i,1);}
        glVertexAttribPointer(7,2,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(offsetof(Instance,bend)));
        glEnableVertexAttribArray(7);glVertexAttribDivisor(7,1);glBindVertexArray(0);
    }
    unsigned program=0;std::array<Mesh,10> meshes{};std::vector<Plant> trees,grass;
    unsigned cachedSeed=~0u;double cachedRadius=0;glm::i64vec3 cachedKey{std::numeric_limits<std::int64_t>::min()};
    size_t lastTrees=0,lastGrass=0;glm::vec3 lightDirection{0,1,0};
};
