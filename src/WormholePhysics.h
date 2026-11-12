#pragma once
#include "OrbitalMechanics.h"

// Fictional Saturn satellite. Optical metric parameters do not contribute
// gravitational mass to the Solar System's prescribed two-body hierarchy.
namespace WormholePhysics {
inline constexpr int SaturnIndex = 5;
inline constexpr double ThroatRadiusKm = 1000;
inline constexpr double OrbitRadiusKm = 3000000;
inline constexpr double Spin = .8;
inline constexpr double Deformation = .35;
double ThroatRadiusAu();
double MassLength(); // Geometric M / throat radius, not a dynamical body mass.
double SpinLength(); // a / throat radius
OrbitalMechanics::Orbit SaturnOrbit();
const char* RayShaderFunctions();
}
