#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <array>
#include <cmath>
#include <vector>

// Original procedural interpretation of the twelve-module Endurance silhouette.
// The Sketchfab reference is viewed, not imported. Coordinates are physical metres.
namespace EnduranceModel {
struct Vertex { glm::vec3 position,color; };
inline constexpr float moduleRadiusMetres=25.f;

inline std::vector<Vertex> Build(){
    std::vector<Vertex> vertices;vertices.reserve(30000);
    const glm::vec3 hull(.77f,.79f,.78f),frame(.25f,.28f,.29f),panel(.045f,.061f,.069f);
    const glm::vec3 foil(.54f,.40f,.19f),trim(.50f,.53f,.52f),black(.017f,.022f,.025f);
    glm::vec3 origin(0),x(1,0,0),y(0,1,0),z(0,0,1);
    const auto transform=[&](const glm::vec3& p){return origin+x*p.x+y*p.y+z*p.z;};
    const auto triangle=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,const glm::vec3& color){
        vertices.push_back({transform(a),color});vertices.push_back({transform(b),color});vertices.push_back({transform(c),color});
    };
    const auto quad=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,const glm::vec3& color){
        triangle(a,b,c,color);triangle(a,c,d,color);
    };
    const auto box=[&](glm::vec3 centre,glm::vec3 size,const glm::vec3& color){
        const auto a=centre-size*.5f,b=centre+size*.5f;
        quad({a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},color);
        quad({a.x,a.y,b.z},{a.x,b.y,b.z},{b.x,b.y,b.z},{b.x,a.y,b.z},color);
        quad({a.x,a.y,a.z},{a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,a.y,a.z},color);
        quad({a.x,b.y,a.z},{b.x,b.y,a.z},{b.x,b.y,b.z},{a.x,b.y,b.z},color);
        quad({a.x,a.y,a.z},{a.x,b.y,a.z},{a.x,b.y,b.z},{a.x,a.y,b.z},color);
        quad({b.x,a.y,a.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{b.x,b.y,a.z},color);
    };
    const auto beveledBox=[&](glm::vec3 centre,glm::vec3 size,float bevel,const glm::vec3& color){
        const float hx=size.x*.5f,hy=size.y*.5f,hz=size.z*.5f;
        const std::array<glm::vec2,8> outline={glm::vec2(-hx+bevel,-hy),{hx-bevel,-hy},{hx,-hy+bevel},{hx,hy-bevel},
            {hx-bevel,hy},{-hx+bevel,hy},{-hx,hy-bevel},{-hx,-hy+bevel}};
        for(int i=0;i<8;++i){const auto a=outline[i],b=outline[(i+1)%8];
            const auto p=centre+glm::vec3(a,-hz),q=centre+glm::vec3(b,-hz);
            const auto r=centre+glm::vec3(b,hz),s=centre+glm::vec3(a,hz);
            quad(p,q,r,s,color);triangle(centre+glm::vec3(0,0,hz),s,r,color);
            triangle(centre-glm::vec3(0,0,hz),q,p,color);
        }
    };
    const auto tube=[&](glm::vec3 a,glm::vec3 b,float ra,float rb,const glm::vec3& color,int segments=10){
        const auto axis=glm::normalize(b-a);
        const auto u=glm::normalize(glm::cross(axis,std::abs(axis.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0)));
        const auto v=glm::cross(axis,u);
        for(int i=0;i<segments;++i){
            const float angle=glm::two_pi<float>()*i/segments,next=glm::two_pi<float>()*(i+1)/segments;
            const auto p=u*std::cos(angle)+v*std::sin(angle),q=u*std::cos(next)+v*std::sin(next);
            quad(a+p*ra,a+q*ra,b+q*rb,b+p*rb,color);
        }
    };
    const auto cap=[&](glm::vec3 centre,float radius,const glm::vec3& color){
        for(int i=0;i<16;++i){const float a=glm::two_pi<float>()*i/16,b=glm::two_pi<float>()*(i+1)/16;
            triangle(centre,centre+glm::vec3(std::cos(a),std::sin(a),0)*radius,centre+glm::vec3(std::cos(b),std::sin(b),0)*radius,color);}
    };
    // A module is a hard-sided pressure/equipment box, rather than a sphere.
    for(int module=0;module<12;++module){
        const float angle=glm::two_pi<float>()*(module+.5f)/12;
        x={std::cos(angle),std::sin(angle),0};y={-x.y,x.x,0};z={0,0,1};origin=x*moduleRadiusMetres;
        const auto tint=hull*(module%3==0?.96f:module%3==1?1.f:1.035f);
        beveledBox({0,0,0},{10.8f,7.5f,7.8f},.65f,tint);
        // Axial equipment faces: recessed dark radiators, bright perimeter rails,
        // seam strips, hatches and small service boxes on both faces.
        for(float side:{-1.f,1.f}){
            box({-.15f,0,side*3.94f},{7.5f,5.9f,.12f},frame);
            box({-.15f,0,side*4.025f},{6.7f,5.2f,.06f},panel);
            for(int slat=0;slat<9;++slat)box({-.15f,-2.30f+slat*.575f,side*4.065f},{6.60f,.055f,.04f},trim*.57f);
            for(float edge:{-1.f,1.f}){
                box({-.15f,edge*2.87f,side*4.07f},{7.65f,.23f,.17f},tint*1.08f);
                box({edge*3.80f,0,side*4.07f},{.23f,5.55f,.17f},tint*1.08f);
            }
            box({4.65f,-1.65f,side*3.98f},{.73f,1.75f,.16f},frame);
            box({4.65f,-1.65f,side*4.09f},{.46f,1.4f,.08f},trim);
            box({-4.62f,1.65f,side*4.05f},{.72f,1.80f,.28f},tint*.85f);
            box({-4.63f,1.65f,side*4.22f},{.40f,1.22f,.07f},black);
            // Discrete ochre corner marks keep the mostly white modules readable.
            box({4.70f,2.85f,side*4.05f},{.70f,.33f,.10f},foil);
        }
        // Peripheral panel seams, recessed side services and external tanks.
        for(float side:{-1.f,1.f}){
            for(float seam:{-3.6f,0.f,3.6f})box({seam,side*3.765f,0},{.065f,.04f,7.6f},frame);
            box({0,side*3.79f,-.4f},{6.4f,.09f,3.8f},panel);
            for(int rib=0;rib<5;++rib)box({-2.8f+rib*1.4f,side*3.86f,-.4f},{.08f,.08f,3.85f},trim);
            tube({-3.3f,side*3.97f,2.65f},{3.2f,side*3.97f,2.65f},.23f,.23f,trim);
        }
        // Inner radial airlock and surrounding insulation patches.
        box({-5.43f,0,0},{.11f,3.4f,4.3f},frame);
        box({-5.51f,0,0},{.10f,2.55f,3.25f},trim);
        box({5.43f,0,-2.3f},{.13f,6.1f,1.1f},foil*.75f);
    }
    origin={0,0,0};x={1,0,0};y={0,1,0};z={0,0,1};
    // Narrow paired ring rails and open X braces connect the separate boxes.
    // They leave a visible gap between modules instead of forming a solid torus.
    for(int link=0;link<12;++link){
        const float a=glm::two_pi<float>()*link/12;
        const auto radial=glm::vec3(std::cos(a),std::sin(a),0),tangent=glm::vec3(-radial.y,radial.x,0);
        for(float axial:{-1.8f,1.8f}){
            const auto c=radial*moduleRadiusMetres+glm::vec3(0,0,axial);
            tube(c-tangent*4.6f,c+tangent*4.6f,.37f,.37f,trim);
        }
        tube(radial*24.5f-tangent*4.6f+glm::vec3(0,0,-1.8f),radial*24.5f+tangent*4.6f+glm::vec3(0,0,1.8f),.15f,.15f,frame);
        tube(radial*24.5f-tangent*4.6f+glm::vec3(0,0,1.8f),radial*24.5f+tangent*4.6f+glm::vec3(0,0,-1.8f),.15f,.15f,frame);
        if(link!=0 && link!=6){
            // Engine throat, flared nozzle and dark open mouth in each gap.
            const auto centre=radial*25.8f;
            tube(centre+glm::vec3(0,0,-2.8f),centre+glm::vec3(0,0,-.5f),1.0f,1.0f,hull);
            tube(centre+glm::vec3(0,0,-.5f),centre+glm::vec3(0,0,2.5f),.72f,1.36f,trim);
            tube(centre+glm::vec3(0,0,2.5f),centre+glm::vec3(0,0,1.65f),1.36f,.70f,black);
            cap(centre+glm::vec3(0,0,1.64f),.71f,black);
        }else{
            // These two clear ports retain the existing +/-25m Ranger docks.
            const auto centre=radial*25.f;
            tube(centre+glm::vec3(0,0,-2.8f),centre+glm::vec3(0,0,-1.3f),1.75f,1.75f,trim);
        }
    }
    // Slender lattice docking spine; the centre remains largely open.
    for(float side:{-1.f,1.f})tube({side*.75f,-21.f,-2.2f},{side*.75f,21.f,-2.2f},.28f,.28f,hull);
    for(int section=0;section<9;++section){
        const float a=-20.5f+section*4.55f,b=a+4.55f;
        tube({-.75f,a,-2.2f},{.75f,b,-2.2f},.13f,.13f,frame);
        tube({.75f,a,-2.2f},{-.75f,b,-2.2f},.13f,.13f,frame);
    }
    tube({0,0,-5.1f},{0,0,3.6f},1.45f,1.45f,hull,16);
    tube({0,0,3.6f},{0,0,4.0f},1.65f,1.65f,frame,16);cap({0,0,4.02f},1.25f,black);
    for(float side:{-1.f,1.f}){
        beveledBox({side*2.5f,0,-2.2f},{2.2f,5.2f,2.0f},.3f,foil);
        tube({side*2.f,0,-2.2f},{side*5.5f,0,-2.2f},.22f,.22f,trim);
    }
    // Communications mast and shallow dish offset from one outer module.
    const auto mast=glm::vec3(-21.7f,-12.5f,4.3f);
    tube(mast,mast+glm::vec3(0,0,3.9f),.12f,.12f,trim);
    tube(mast+glm::vec3(0,0,3.9f),mast+glm::vec3(0,0,4.45f),.20f,1.55f,hull,20);
    cap(mast+glm::vec3(0,0,4.46f),1.50f,trim);
    tube(mast+glm::vec3(0,0,4.5f),mast+glm::vec3(0,0,5.35f),.07f,.07f,frame,8);
    return vertices;
}
} // namespace EnduranceModel
