#pragma once
#include <algorithm>
#include <cmath>
namespace CameraZoom {
inline constexpr double Maximum=20;
inline double Magnification(double baseDegrees,double fovDegrees){
    return std::tan(baseDegrees*.008726646259971648)/std::tan(std::max(.00001,fovDegrees)*.008726646259971648);
}
inline float MinimumFieldOfView(double baseDegrees){
    return static_cast<float>(std::atan(std::tan(baseDegrees*.008726646259971648)/Maximum)*114.59155902616465);
}
// Deliberate camera visibility aid, not a claim that zoom changes aperture.
inline float MagnitudeLimit(float base,double zoom){
    return std::clamp(base+static_cast<float>(5*std::log10(std::clamp(zoom,1.0,Maximum))),-2.f,30.f);
}
}
