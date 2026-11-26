#pragma once

#include "Astronomy.h"
#include "FlockSteering.h"
#include "MarineAnimalGeometry.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif
#include "glad/glad.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstddef>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

unsigned int compileShaderProgram(const char*,const char*);

class OceanFish {
public:
    bool Initialize(){
        const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 skin;
layout(location=3) in vec4 centreScale;
layout(location=4) in vec4 heading;
layout(location=5) in vec4 motion;
uniform mat4 viewProjection;
uniform vec3 localUp;
uniform int animalKind;
out vec3 fishPosition,relativeMetres,surfaceNormal;
out vec3 skinColor;
out float tint;
void main(){
    vec3 point=position;
    float flex=max(0.0,-point.x-.08);
    float undulation=sin(motion.y+point.x*6.0);
    vec3 bentNormal=normal;
    if(animalKind==2){
        point.y+=flex*flex*.48*undulation;
        bentNormal.x+=normal.y*flex*.72*undulation;
    }else{
        point.z+=flex*flex*.52*undulation;
        bentNormal.x+=normal.z*flex*.72*undulation;
    }
    vec3 forward=normalize(heading.xyz);
    vec3 sideAxis=abs(dot(forward,localUp))>.995?vec3(0,1,0):localUp;
    if(abs(dot(forward,sideAxis))>.995)sideAxis=vec3(1,0,0);
    vec3 across=normalize(cross(forward,sideAxis));
    mat3 swimFrame=mat3(forward,cross(across,forward),across);
    relativeMetres=centreScale.xyz+swimFrame*(point*centreScale.w);
    surfaceNormal=swimFrame*bentNormal;
    fishPosition=position;
    tint=motion.z;
    skinColor=skin;
    gl_Position=viewProjection*vec4(relativeMetres/149597870700.0,1);
}
)";
        const char* fragment=R"(#version 330 core
in vec3 fishPosition,relativeMetres,surfaceNormal;
in vec3 skinColor;
in float tint;
uniform vec3 lightDirection;
uniform float daylight;
uniform int animalKind;
out vec4 color;
void main(){
    vec3 p=fishPosition,n=normalize(surfaceNormal);
    if(!gl_FrontFacing)n=-n;
    float back=smoothstep(-.12,.19,p.y);
    vec3 albedo=mix(vec3(.68,.83,.86),mix(vec3(.037,.15,.21),vec3(.18,.35,.24),tint),back);
    float stripe=1.0-smoothstep(.012,.040,abs(p.y+.041));
    albedo*=1.0-.34*stripe;
    if(p.x<-.42 || p.y>.205)albedo=mix(albedo,vec3(.26,.40,.37),.56);
    float eye=length((vec3(p.x,p.y,abs(p.z))-vec3(.385,.060,.072))*vec3(1,1,.52));
    albedo=mix(albedo,vec3(.02,.037,.029),1.0-smoothstep(.031,.039,eye));
    albedo=mix(albedo,vec3(.48,.51,.30),smoothstep(.022,.029,eye)*(1.0-smoothstep(.031,.037,eye)));
    if(animalKind>0)albedo=skinColor;
    float diffuse=max(dot(n,lightDirection),0.0);
    vec3 view=normalize(-relativeMetres);
    float glint=pow(max(dot(n,normalize(view+lightDirection)),0.0),34.0)*.28;
    color=vec4(albedo*(.05+daylight*(.38+.57*diffuse))+daylight*glint,0);
    gl_FragDepth=log2(1.0+length(relativeMetres)*(1e12/149597870700.0))/log2(1.0+20000.0*1e12);
}
)";
        program=compileShaderProgram(vertex,fragment);
        GLint linked=0;
        glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked){Release();return false;}
        auto mesh=MakeMesh();
        vertexCount[0]=static_cast<int>(mesh.size());
        for(unsigned i=1;i<vertexCount.size();++i){
            const auto model=MarineAnimalGeometry::Build(i==2);
            vertexStart[i]=static_cast<int>(mesh.size());
            vertexCount[i]=static_cast<int>(model.size());
            for(const auto& v:model)mesh.push_back({v.position,v.normal,v.color});
        }
        glGenVertexArrays(1,&vao);
        glGenBuffers(1,&vertices);
        glGenBuffers(1,&instances);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,vertices);
        glBufferData(GL_ARRAY_BUFFER,mesh.size()*sizeof(Vertex),mesh.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,color)));
        glEnableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER,instances);
        for(unsigned i=0;i<3;++i){
            glVertexAttribPointer(3+i,4,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(i*sizeof(glm::vec4)));
            glEnableVertexAttribArray(3+i);
            glVertexAttribDivisor(3+i,1);
        }
        glBindVertexArray(0);
        return true;
    }

    template<class WaterDepth>
    void Render(const glm::mat4& viewProjection,const glm::dmat3& rotation,const glm::dvec3& relativeCameraAu,
        double surfaceRadiusMetres,const glm::vec3& toLight,double seconds,unsigned seed,
        bool submerged,WaterDepth waterDepth){
        const auto cameraLocal=glm::transpose(rotation)*relativeCameraAu*Astronomy::metresPerAu;
        const float depth=static_cast<float>(surfaceRadiusMetres-glm::length(cameraLocal));
        if(depth>155.f || depth< -85.f)return;
        const auto up=glm::normalize(cameraLocal);
        if(haveTime && (seconds<lastTime || seconds-lastTime>10.0))Reset();
        stepRemainder+=haveTime?std::min(seconds-lastTime,.24):0.0;
        haveTime=true;
        lastTime=seconds;
#ifdef RENDER_ENGINE_DX12
        const bool rayAnimals=NativeDX12::CapturingRayScene();
#else
        constexpr bool rayAnimals=false;
#endif
        if(submerged || rayAnimals)
            Populate(submerged?cameraLocal:up*(surfaceRadiusMetres-5.),surfaceRadiusMetres,seed,waterDepth);
        constexpr double step=.025;
        while(stepRemainder>=step){
            for(auto& school:schools)Advance(school,static_cast<float>(step));
            stepRemainder-=step;
        }
        for(auto& batch:visible)batch.clear();
        const auto queue=[&](unsigned animal,const glm::dvec3& location,const glm::vec3& heading,float scale,float phase,float tint){
            const auto relative=glm::vec3(rotation*(location-cameraLocal));
            const auto clip=viewProjection*glm::vec4(relative/static_cast<float>(Astronomy::metresPerAu),1);
            if(!rayAnimals && (clip.w<=0 || std::abs(clip.x)>clip.w*1.10f || std::abs(clip.y)>clip.w*1.10f))return;
            visible[animal].push_back({glm::vec4(relative,scale),glm::vec4(glm::vec3(rotation*glm::dvec3(heading)),0),
                glm::vec4(0,phase,tint,0)});
        };
        if(submerged || rayAnimals){
            for(const auto& school:schools){
                if(glm::length(school.origin-cameraLocal)>39)continue;
                for(std::size_t i=0;i<school.count;++i){
                    const auto& fish=school.fish[i];
                    const glm::vec3 p=fish.position+fish.velocity*static_cast<float>(stepRemainder);
                    const auto location=school.origin+school.frame*glm::dvec3(p);
                    const double water=waterDepth(glm::vec3(glm::normalize(location)));
                    if(surfaceRadiusMetres-glm::length(location)>water-.20 || water<1)continue;
                    queue(0,location,glm::vec3(school.frame*glm::dvec3(FlockSteering::Direction(fish.velocity))),
                        fish.scale,fish.phase,fish.tint);
                }
                if(school.hasShark){
                    const auto& shark=school.shark;
                    const auto location=school.origin+school.frame*glm::dvec3(shark.position);
                    const double water=waterDepth(glm::vec3(glm::normalize(location)));
                    if(surfaceRadiusMetres-glm::length(location)<water-.6 && water>3)
                        queue(1,location,glm::vec3(school.frame*glm::dvec3(FlockSteering::Direction(shark.velocity))),
                            shark.scale,shark.phase,shark.tint);
                }
            }
        }
        AddDolphins(cameraLocal,surfaceRadiusMetres,seconds,seed,submerged || rayAnimals,waterDepth,queue);
        if(std::all_of(visible.begin(),visible.end(),[](const auto& batch){return batch.empty();}))return;
        const bool blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
        glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(viewProjection));
        const auto worldUp=glm::vec3(rotation*up);
        glUniform3fv(glGetUniformLocation(program,"localUp"),1,glm::value_ptr(worldUp));
        glUniform3fv(glGetUniformLocation(program,"lightDirection"),1,glm::value_ptr(toLight));
        glUniform1f(glGetUniformLocation(program,"daylight"),glm::smoothstep(-.16f,.25f,glm::dot(worldUp,toLight)));
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,instances);
        for(unsigned animal=0;animal<visible.size();++animal){
            if(visible[animal].empty())continue;
            glUniform1i(glGetUniformLocation(program,"animalKind"),animal);
            glBufferData(GL_ARRAY_BUFFER,visible[animal].size()*sizeof(Instance),visible[animal].data(),GL_DYNAMIC_DRAW);
#ifdef RENDER_ENGINE_DX12
            const glm::vec3 colors[]={{.18f,.31f,.31f},{.47f,.49f,.50f},{.25f,.36f,.43f}};
            if(rayAnimals)NativeDX12::SetRayMaterial(
                {animal==2?NativeDX12::RayKind::Solid:NativeDX12::RayKind::Submerged,
                 NativeDX12::RaySpace::RelativeMetres,colors[animal],.06f},animal<2 && !submerged);
#endif
            glDrawArraysInstanced(GL_TRIANGLES,vertexStart[animal],vertexCount[animal],static_cast<int>(visible[animal].size()));
            if(submerged || animal==2)lastDrawn[animal]=visible[animal].size();
        }
#ifdef RENDER_ENGINE_DX12
        if(rayAnimals)NativeDX12::SetRayMaterial({});
#endif
        glBindVertexArray(0);
        if(blend)glEnable(GL_BLEND);
        if(cull)glEnable(GL_CULL_FACE);
    }

    size_t LastDrawn() const {return lastDrawn[0];}
    size_t LastSharks() const {return lastDrawn[1];}
    size_t LastDolphins() const {return lastDrawn[2];}
    unsigned FishCaught() const {return caught;}
    void ClearCount(){lastDrawn.fill(0);}
    void Reset(){schools.clear();haveTime=haveCell=false;stepRemainder=0;caught=0;ClearCount();}
    void Release(){
        if(program)glDeleteProgram(program);
        if(vao)glDeleteVertexArrays(1,&vao);
        if(vertices)glDeleteBuffers(1,&vertices);
        if(instances)glDeleteBuffers(1,&instances);
        program=vao=vertices=instances=0;
        Reset();
    }

private:
    struct Vertex {glm::vec3 position,normal,color{0};};
    struct Instance {glm::vec4 centreScale,heading,motion;};
    struct swimmer {
        glm::vec3 position{},velocity{};
        float phase=0,scale=1,tint=0;
    };
    struct School {
        glm::ivec3 cell;
        glm::dvec3 origin;
        glm::dmat3 frame;
        float depth,maxDepth,age=0,biteCooldown=0;
        std::array<swimmer,10> fish{};
        std::size_t count=10;
        swimmer shark;
        bool hasShark=false;
    };
    static std::uint32_t Key(unsigned seed,const glm::ivec3& cell){
        return seed^static_cast<std::uint32_t>(cell.x)*374761393u
            ^static_cast<std::uint32_t>(cell.y)*668265263u^static_cast<std::uint32_t>(cell.z)*2246822519u;
    }
    static glm::dmat3 Frame(const glm::dvec3& up){
        const auto reference=std::abs(up.y)<.94?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
        const auto across=glm::normalize(glm::cross(reference,up));
        return glm::dmat3(across,up,glm::cross(across,up));
    }

    template<class WaterDepth>
    void Populate(const glm::dvec3& camera,double surfaceRadius,unsigned seed,WaterDepth waterDepth){
        constexpr double cellSize=14.0;
        const glm::ivec3 current(glm::floor(camera/cellSize));
        if(haveCell && current==cameraCell)return;
        haveCell=true;
        cameraCell=current;
        schools.erase(std::remove_if(schools.begin(),schools.end(),[&](const School& school){
            return glm::length(school.origin-camera)>54;
        }),schools.end());
        for(int z=-2;z<=2;++z)for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){
            if(schools.size()>=8)return;
            const auto cell=current+glm::ivec3(x,y,z);
            const auto key=Key(seed,cell);
            if(Unit(key)<.33f)continue;
            const auto centre=(glm::dvec3(cell)+glm::dvec3(.25+.5*Unit(key+1),.25+.5*Unit(key+2),.25+.5*Unit(key+3)))*cellSize;
            if(glm::length(centre-camera)>36)continue;
            const double depth=surfaceRadius-glm::length(centre);
            if(depth<1.45 || depth>150)continue;
            if(std::any_of(schools.begin(),schools.end(),[&](const School& s){return s.cell==cell;}))continue;
            const auto up=glm::normalize(centre);
            const double bottom=waterDepth(glm::vec3(up));
            if(bottom<depth+1.0)continue;
            School school{};
            school.cell=cell;
            school.origin=centre;
            school.frame=Frame(up);
            school.depth=static_cast<float>(depth);
            school.maxDepth=static_cast<float>(bottom);
            school.hasShark=bottom>7 && depth>2.2 && Unit(key+9)<.32f
                && std::none_of(schools.begin(),schools.end(),[](const School& s){return s.hasShark;});
            const float direction=Unit(key+4)*glm::two_pi<float>();
            for(unsigned i=0;i<school.count;++i){
                const unsigned member=key+51*i;
                school.fish[i]={{(Unit(member+6)-.5f)*5.6f,(Unit(member+7)-.5f)*1.15f,(Unit(member+8)-.5f)*5.6f},
                    {1.4f*std::cos(direction),0,1.4f*std::sin(direction)},Unit(member+12)*6,
                    .48f+.50f*Unit(member+15),Unit(member+17)};
            }
            school.shark={{std::sin(direction)*9.f,0,std::cos(direction)*9.f},
                {std::cos(direction),0,-std::sin(direction)},Unit(key+11)*6,2.7f+.65f*Unit(key+12),0};
            schools.push_back(school);
        }
    }

    void Advance(School& school,float step){
        using namespace FlockSteering;
        school.age+=step;
        const auto snapshot=school.fish;
        const float low=school.depth-school.maxDepth+.65f, high=school.depth-.85f;
        for(std::size_t i=0;i<school.count;++i){
            const auto& previous=snapshot[i];
            auto& fish=school.fish[i];
            const glm::vec3 horizontal(previous.position.x,0,previous.position.z);
            const float angle=school.age*.53f+snapshot[0].tint*glm::two_pi<float>();
            glm::vec3 desired=glm::vec3(std::cos(angle),0,std::sin(angle))*1.65f
                +Neighbors(snapshot.data(),school.count,i,9.f,1.45f,1.7f)-horizontal*.27f;
            desired.y+=glm::clamp(-previous.position.y*.75f,-1.f,1.f);
            if(school.hasShark){
                const auto away=previous.position-school.shark.position;
                desired+=Direction(away)*(5.1f*(1-glm::smoothstep(1.f,10.f,glm::length(away))));
            }
            fish.velocity=previous.velocity+Limit(desired-previous.velocity,6.4f)*step;
            fish.velocity=Direction(fish.velocity)*glm::clamp(glm::length(fish.velocity),.65f,3.65f);
            fish.position=previous.position+fish.velocity*step;
            fish.position.y=glm::clamp(fish.position.y,low,high);
            fish.phase=std::remainder(fish.phase+step*(8.f+glm::length(fish.velocity)),glm::two_pi<float>());
        }
        if(!school.hasShark)return;
        auto& shark=school.shark;
        const float angle=school.age*.28f;
        glm::vec3 target(std::cos(angle)*11.f,0,std::sin(angle)*11.f);
        std::size_t prey=school.count;
        float closest=19.f;
        school.biteCooldown=std::max(0.f,school.biteCooldown-step);
        if(school.biteCooldown<=0)for(std::size_t i=0;i<school.count;++i){
            const auto& fish=school.fish[i];
            const float distance=glm::length(fish.position-shark.position);
            if(distance>=closest)continue;
            closest=distance;prey=i;target=fish.position+fish.velocity*.36f;
        }
        const float cruise=prey<school.count?4.9f:2.6f;
        shark.velocity+=Limit(Direction(target-shark.position)*cruise-shark.velocity,7.9f)*step;
        shark.position+=shark.velocity*step;
        shark.position.y=glm::clamp(shark.position.y,low,std::max(low,school.depth-1.35f));
        shark.phase=std::remainder(shark.phase+step*4.1f,glm::two_pi<float>());
        if(prey<school.count && glm::length(shark.position+Direction(shark.velocity)*1.05f-school.fish[prey].position)<.80f){
            school.fish[prey]=school.fish[--school.count];
            school.biteCooldown=3.4f;
            ++caught;
        }
    }

    template<class WaterDepth,class Queue>
    static void AddDolphins(const glm::dvec3& camera,double surfaceRadius,double seconds,unsigned seed,
        bool submerged,WaterDepth waterDepth,Queue queue){
        if(surfaceRadius-glm::length(camera)>47)return;
        const auto surface=glm::normalize(camera)*surfaceRadius;
        constexpr double cellSize=68.0;
        const glm::ivec3 current(glm::floor(surface/cellSize));
        int pods=0;
        for(int z=-2;z<=2;++z)for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){
            if(pods>=3)return;
            const auto cell=current+glm::ivec3(x,y,z);
            const auto key=Key(seed+1949,cell);
            const auto sample=(glm::dvec3(cell)+glm::dvec3(.5))*cellSize;
            if(Unit(key)<.63f || std::abs(glm::length(sample)-surfaceRadius)>cellSize*.54
                || glm::length(sample-surface)>124)continue;
            const auto up=glm::normalize(sample);
            if(waterDepth(glm::vec3(up))<5.5 || std::abs(up.y)>.93)continue;
            ++pods;
            const auto frame=Frame(up);
            for(unsigned i=0;i<3;++i){
                const unsigned member=key+37*i;
                const double period=22+16*Unit(member+4), leap=1.58;
                double cycle=std::fmod(seconds+period*Unit(member+5),period);
                if(cycle<0)cycle+=period;
                const double angle=std::remainder(seconds*.27+6.28*Unit(member+6),glm::two_pi<double>());
                const double r=15+9*Unit(member+7);
                const bool breach=cycle<leap;
                const double stage=cycle/leap, swim=(cycle-leap)/(period-leap);
                const double height=breach?-.79+12.2*stage*(1-stage):-.79-3.7*std::sin(swim*glm::pi<double>());
                const double rise=breach?12.2*(1-2*stage)/leap:-3.7*glm::pi<double>()*std::cos(swim*glm::pi<double>())/(period-leap);
                const auto direction=glm::normalize(up+frame*glm::dvec3(r*std::cos(angle),0,r*std::sin(angle))/surfaceRadius);
                const double depth=waterDepth(glm::vec3(direction));
                if(depth<5.4 || (!submerged && height<.05))continue;
                const auto location=direction*(surfaceRadius+height);
                const auto velocity=frame*glm::dvec3(-r*.27*std::sin(angle),rise,r*.27*std::cos(angle));
                queue(2,location,glm::vec3(glm::normalize(velocity)),2.15f+.67f*Unit(member+8),
                    static_cast<float>(std::remainder(seconds*3.7,glm::two_pi<double>())),0.f);
            }
        }
    }

    static float Unit(std::uint32_t value){
        value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;
        return static_cast<float>(value&0xffffffu)/16777216.f;
    }
    static std::vector<Vertex> MakeMesh(){
        constexpr int rings=14,sides=16;
        const auto skin=[](int ring,int side){
            const float theta=glm::pi<float>()*ring/rings,phi=glm::two_pi<float>()*side/sides;
            const glm::vec3 sphere(-std::cos(theta),std::sin(theta)*std::cos(phi),std::sin(theta)*std::sin(phi));
            const float taper=.84f+.16f*sphere.x;
            return Vertex{{.045f+sphere.x*.49f,.204f*taper*sphere.y,.104f*taper*sphere.z},
                glm::normalize(sphere/glm::vec3(.49f,.204f*taper,.104f*taper))};
        };
        std::vector<Vertex> mesh;
        for(int ring=0;ring<rings;++ring)for(int side=0;side<sides;++side){
            const auto a=skin(ring,side),b=skin(ring+1,side),c=skin(ring+1,side+1),d=skin(ring,side+1);
            if(ring>0)mesh.insert(mesh.end(),{a,d,b});
            if(ring+1<rings)mesh.insert(mesh.end(),{d,c,b});
        }
        const auto fin=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c){
            const auto n=glm::normalize(glm::cross(b-a,c-a));
            mesh.insert(mesh.end(),{{a,n},{b,n},{c,n}});
        };
        fin({-.38f,0,0},{-.68f,.185f,0},{-.59f,0,0});
        fin({-.38f,0,0},{-.59f,0,0},{-.68f,-.165f,0});
        fin({.13f,.15f,0},{-.19f,.285f,0},{-.27f,.12f,0});
        fin({-.01f,-.095f,.07f},{-.235f,-.17f,.19f},{-.17f,-.085f,.09f});
        fin({-.01f,-.095f,-.07f},{-.17f,-.085f,-.09f},{-.235f,-.17f,-.19f});
        return mesh;
    }
    unsigned int program=0,vao=0,vertices=0,instances=0;
    std::array<int,3> vertexStart{},vertexCount{};
    std::array<size_t,3> lastDrawn{};
    std::array<std::vector<Instance>,3> visible;
    std::vector<School> schools;
    glm::ivec3 cameraCell{};
    double lastTime=0,stepRemainder=0;
    unsigned caught=0;
    bool haveTime=false,haveCell=false;
};
