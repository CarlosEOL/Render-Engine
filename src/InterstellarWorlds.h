#pragma once
#include "OrbitalMechanics.h"
#include <array>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
namespace InterstellarWorlds {
enum class Surface { Ocean, Ice, Desert };
struct Planet {
  const char *name;
  const char *classification;
  OrbitalMechanics::Orbit orbit;
  double radiusEarth, massEarth;
  glm::vec3 color;
  Surface surface;
};
const Planet &Get(int index);
float Elevation(const glm::vec3 &direction, int planet, float phase);
const char *ShaderFunctions();
OrbitalMechanics::Orbit DepartureOrbit();
glm::dmat3 Orientation();
OrbitalMechanics::ParticleState DepartureState(double worldSeconds);
double SpecificEnergy(const OrbitalMechanics::ParticleState &state);
double AdvanceMission(OrbitalMechanics::ParticleState &state,
                      double &fuelSeconds, bool &firing, bool manualBoost,
                      double remainingSeconds, double *properElapsed = nullptr);
std::array<OrbitalMechanics::ParticleState, 2>
SeparateRangers(const OrbitalMechanics::ParticleState &endurance);
inline constexpr double MissionPeriapsisHorizons = 8;
inline constexpr double EngineAccelerationMetres = 30;
inline constexpr double FuelSeconds = 72000;
} // namespace InterstellarWorlds
