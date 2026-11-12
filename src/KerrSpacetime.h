#pragma once
#include <glm/glm.hpp>

namespace KerrSpacetime {
struct Ray {
  glm::dvec3 position, momentum;
  double timeMomentum;
};
double Radius(const glm::dvec3 &position, double horizonReference, double spin);
double Horizon(double horizonReference, double spin);
// Innermost spherical photon orbit (prograde equatorial limit).
double MinimumPhotonOrbit(double horizonReference, double spin);
double ProgradeIsco(double horizonReference, double spin);
Ray PastRay(const glm::dvec3 &position, const glm::dvec3 &direction,
            double horizonReference, double spin);
// beta is the observer's physical velocity in the orthonormal frame normal
// to the regular Kerr-Schild slices. The launched ray has unit received energy.
Ray ObserverPastRay(const glm::dvec3 &position, const glm::dvec3 &direction,
                    double horizonReference, double spin,
                    const glm::dvec3 &beta);
struct Observer {
  Ray state;
  double properSeconds = 0, coordinateSeconds = 0;
};
Observer MakeObserver(const glm::dvec3 &position, double horizonReference,
                      double spin, const glm::dvec3 &localBeta = glm::dvec3(0));
Observer MakeStationaryObserver(const glm::dvec3 &position,
                                double horizonReference, double spin);
glm::dvec3 ObserverBeta(const Observer &observer, double horizonReference,
                        double spin);
glm::dvec3 ObserverCoordinateVelocity(const Observer &observer,
                                      double horizonReference, double spin);
double ObserverTimeComponent(const Observer &observer, double horizonReference,
                             double spin);
double ReceivedEnergy(const Ray &photon, const Observer &observer,
                      double horizonReference, double spin);
void AdvanceObserver(Observer &observer, double horizonReference, double spin,
                     double properSeconds, double lightSpeedAu);
double Hamiltonian(const Ray &ray, double horizonReference, double spin);
void AdvanceRay(Ray &ray, double horizonReference, double spin,
                double affineStep);
const char *ShaderFunctions();
} // namespace KerrSpacetime
