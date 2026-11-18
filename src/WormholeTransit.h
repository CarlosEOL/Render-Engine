#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// Swept throat entry catches fast flight without depending on frame rate.
// The mouths may move between the two samples. All coordinates are in AU.
namespace WormholeTransit {
struct Crossing {bool entered=false;glm::dvec3 position{};};
inline Crossing Sweep(const glm::dvec3& before,const glm::dvec3& after,
    const glm::dvec3& sourceBefore,const glm::dvec3& sourceAfter,
    const glm::dvec3& destination,const glm::dmat3& rotation,double radius){
    const auto a=before-sourceBefore,b=after-sourceAfter,step=b-a;
    const double aa=glm::dot(step,step),cc=glm::dot(a,a)-radius*radius;
    if(radius<=0 || aa<=1e-40 || cc<=0)return {};
    const double ab=glm::dot(a,step),discriminant=ab*ab-aa*cc;
    if(ab>=0 || discriminant<=0)return {};
    const double t=(-ab-std::sqrt(discriminant))/aa;
    if(t<0 || t>1)return {};
    const auto radial=glm::normalize(a+step*t);
    // Arrive across the throat, looking along the mapped incoming direction.
    // Bound a warp-speed remainder so one frame cannot skip the destination.
    const double remainder=std::min(glm::length(step)*(1-t),radius*4);
    return {true,destination-rotation*radial*(radius*1.6)+rotation*glm::normalize(step)*remainder};
}
}
