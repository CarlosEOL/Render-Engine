#include "InterstellarWorlds.h"
#include "Astronomy.h"
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

const InterstellarWorlds::Planet &InterstellarWorlds::Get(int index) {
  // Physical AU/radii; outer orbital elements and masses are explicit model
  // choices. No precise canonical ephemeris exists for these fictional worlds.
  static const Planet planets[] = {
      {"Miller's planet",
       "Ocean world",
       {Astronomy::millerOrbitAu, 0, .20, .2, .8},
       1,
       1.3,
       {.12, .32, .52},
       Surface::Ocean},
      {"Mann's planet",
       "Frozen terrestrial world",
       {350, 300.0 / 350, .32, 1.7, glm::pi<double>()},
       .9,
       .648,
       {.72, .81, .88},
       Surface::Ice},
      {"Edmunds' planet",
       "Rocky desert world",
       {1200, .05, .4, .7, 4.8},
       1.05,
       1.1,
       {.54, .32, .18},
       Surface::Desert}};
  return planets[glm::clamp(index, 0, 2)];
}
float InterstellarWorlds::Elevation(const glm::vec3 &d, int planet,
                                    float phase) {
  if (planet == 0) {
    const float wave = (std::sin(35 * d.y + 4 * std::sin(10 * d.x) + phase) +
                        .35f * std::sin(80 * d.y + 8 * d.x - phase)) /
                       1.35f;
    return static_cast<float>(2.0 / (Get(0).radiusEarth * 6371000)) * wave;
  }
  const float folded =
      std::sin(d.x * 13 + std::sin(d.z * 7) * 2) * std::cos(d.y * 17 - d.z * 5);
  const float basins =
      std::sin(d.x * 5 + d.z * 8) * std::cos(d.y * 6 + d.x * 3);
  if (planet == 1)
    return .0018f * (.25f * basins + .75f * std::pow(std::abs(folded), 4.f));
  return .0015f * (.65f * basins + .35f * folded);
}
OrbitalMechanics::Orbit InterstellarWorlds::DepartureOrbit() {
  const auto mann = Get(1).orbit;
  const double apo = mann.semiMajorAxis * (1 + mann.eccentricity);
  const double peri = MissionPeriapsisHorizons * Astronomy::gargantuaHorizonAu;
  return {(apo + peri) * .5, (apo - peri) / (apo + peri), mann.inclination,
          mann.ascendingNode, glm::pi<double>()};
}
glm::dmat3 InterstellarWorlds::Orientation() {
  return glm::dmat3(
      glm::rotate(glm::dmat4(1), glm::radians(67.0), glm::dvec3(1, 0, 0)));
}
OrbitalMechanics::ParticleState
InterstellarWorlds::DepartureState(double seconds) {
  const auto position =
      Orientation() * OrbitalMechanics::PositionAtTimeDouble(
                          Get(1).orbit, Astronomy::gargantuaGravityAu, seconds);
  const auto velocity =
      Orientation() *
      glm::dvec3(OrbitalMechanics::VelocityAtTime(
          Get(1).orbit, Astronomy::gargantuaGravityAu, seconds));
  const double r = glm::length(position),
               peri = MissionPeriapsisHorizons * Astronomy::gargantuaHorizonAu;
  const auto radial = position / r,
             normal = glm::normalize(glm::cross(position, velocity));
  const double rs = Astronomy::gargantuaHorizonAu, fa = 1 - rs / r,
               fp = 1 - rs / peri;
  const double angularSquared = (fp - fa) / (fa / (r * r) - fp / (peri * peri));
  const double energy = std::sqrt(fa * (1 + angularSquared / (r * r)));
  const double speed =
      Astronomy::lightSpeedAu * std::sqrt(angularSquared) * fa / (energy * r);
  return {position + radial * (Get(1).radiusEarth * Astronomy::earthRadiusAu +
                               100000 / Astronomy::metresPerAu),
          glm::normalize(glm::cross(normal, radial)) * speed};
}
double InterstellarWorlds::SpecificEnergy(
    const OrbitalMechanics::ParticleState &state) {
  return (OrbitalMechanics::SchwarzschildEnergy(
              state, Astronomy::gargantuaHorizonAu, Astronomy::lightSpeedAu) -
          1) *
         Astronomy::lightSpeedAu * Astronomy::lightSpeedAu;
}
std::array<OrbitalMechanics::ParticleState, 2>
InterstellarWorlds::SeparateRangers(
    const OrbitalMechanics::ParticleState &ship) {
  const auto axis = glm::normalize(glm::cross(ship.position, ship.velocity));
  const double lapse =
      std::sqrt(1 - Astronomy::gargantuaHorizonAu / glm::length(ship.position));
  std::array<OrbitalMechanics::ParticleState, 2> result;
  for (int i = 0; i < 2; ++i) {
    const double sign = i == 0 ? -1 : 1;
    result[i] = {ship.position + axis * (sign * 20 / Astronomy::metresPerAu),
                 ship.velocity +
                     axis * (sign * 3 * lapse / Astronomy::metresPerAu)};
  }
  return result;
}

double
InterstellarWorlds::AdvanceMission(OrbitalMechanics::ParticleState &state,
                                   double &fuel, bool &firing, bool manual,
                                   double remaining, double *properElapsed) {
  if (remaining <= 0)
    return 0;
  const double mu = Astronomy::gargantuaGravityAu,
               r = glm::length(state.position);
  const double peri = MissionPeriapsisHorizons * Astronomy::gargantuaHorizonAu;
  if (!manual) {
    if (r < 1.4 * peri && glm::dot(state.position, state.velocity) < 0 &&
        fuel > 0)
      firing = true;
  }
  double dt = std::min(remaining, .005 * std::sqrt(r * r * r / mu));
  const double thrust = firing && fuel > 0
                            ? EngineAccelerationMetres / Astronomy::metresPerAu
                            : 0;
  if (thrust > 0)
    dt = std::min(dt, fuel / OrbitalMechanics::SchwarzschildClockRate(
                                 state, Astronomy::gargantuaHorizonAu,
                                 Astronomy::lightSpeedAu));
  const double proper = OrbitalMechanics::AdvanceSchwarzschild(
      state, mu, Astronomy::lightSpeedAu, thrust, dt);
  if (properElapsed)
    *properElapsed = proper;
  if (thrust > 0)
    fuel = std::max(0.0, fuel - proper);
  if (fuel < 1e-8)
    fuel = 0;
  if (fuel == 0)
    firing = false;
  return dt;
}
const char *InterstellarWorlds::ShaderFunctions() {
  return R"(
uniform int interstellarPlanet;
float interstellarElevation(vec3 d,float phase){
    if(interstellarPlanet==0)return waterWaveHeight*oceanWave(d,phase);
    float folded=sin(d.x*13.0+sin(d.z*7.0)*2.0)*cos(d.y*17.0-d.z*5.0);
    float basins=sin(d.x*5.0+d.z*8.0)*cos(d.y*6.0+d.x*3.0);
    if(interstellarPlanet==1)return .0018*(.25*basins+.75*pow(abs(folded),4.0));
    return .0015*(.65*basins+.35*folded);
}
)";
}
