#pragma once
#include <glm/glm.hpp>
#include <cmath>

// A fixed fictional interior chart at Gargantua's origin. The same linear
// conversion is used before, during and after the reveal; no observer rebase.
namespace TesseractSpace {
inline double Scale(double horizonRadiusAu){return horizonRadiusAu*.01;}
inline constexpr glm::dvec3 roomPitch(6.5,4.6,5.5);
inline constexpr glm::dvec2 roomHalfSize(2.2,1.8);
inline constexpr double ceilingHeight=2.65;
inline constexpr double deckTop=0,eyeHeight=1.65;
inline constexpr double defaultFlightSpeed=3.5;
inline double Periodic(double value,double period){return value-period*std::floor((value+period*.5)/period);}
inline bool OnDeck(double x,double z){
    return std::abs(Periodic(x,roomPitch.x))<=roomHalfSize.x
        && std::abs(Periodic(z,roomPitch.z))<=roomHalfSize.y;
}
inline bool Land(const glm::dvec3& before,glm::dvec3& after){
    if(after.y>before.y || !OnDeck(after.x,after.z))return false;
    const double feet=before.y-eyeHeight;
    const double floor=std::floor((feet-deckTop+1e-8)/roomPitch.y)*roomPitch.y+deckTop;
    if(after.y-eyeHeight>floor)return false;
    const double dy=before.y-after.y;
    const double t=dy>1e-10?glm::clamp((before.y-eyeHeight-floor)/dy,0.0,1.0):0;
    const auto contact=before+(after-before)*t;
    if(!OnDeck(contact.x,contact.z))return false;
    after.y=floor+eyeHeight;
    return true;
}
}
