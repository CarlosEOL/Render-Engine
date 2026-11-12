#pragma once
#include <glm/glm.hpp>

// Observation-constrained structural model, not a catalogue of measured stars.
// See docs/MILKY_WAY_AND_NAVIGATION_2026-11-12.md for sources/assumptions.
namespace MilkyWayModel {
inline constexpr double auPerParsec = 206264.80624709636;
inline constexpr double auPerKpc = 1000 * auPerParsec;
inline constexpr double solarRadiusKpc = 8.277;
inline constexpr double diskRadiusKpc = 16.0;
inline constexpr double diskScaleKpc = 2.6;
inline constexpr double barHalfLengthKpc = 5.0;
inline constexpr double barAngleDegrees = 30.0;
inline constexpr double centralMassSolar = 4.30e6;

glm::dvec3 EquatorialToEngine(const glm::dvec3& equatorial);
// Engine axes use ecliptic X, north/up Y, minus ecliptic Y along Z.
// Model axes are Galactic anti-centre X, north Y, Galactic east Z.
glm::dmat3 Orientation();
glm::dvec3 CenterAu();
glm::dvec3 ToModelKpc(const glm::dvec3& worldAu);
glm::dvec3 ToWorldAu(const glm::dvec3& modelKpc);
double WarpKpc(const glm::dvec3& modelKpc);
double ArmStrength(const glm::dvec3& modelKpc);
// Old stellar disk+halo, young disk, bar+bulge, and absorbing dust.
glm::dvec4 Densities(const glm::dvec3& modelKpc);
const char* ShaderFunctions();
}
