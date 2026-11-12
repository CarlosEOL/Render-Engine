#pragma once
#include <glm/glm.hpp>

namespace KerrSpacetime {
struct Ray {
  glm::dvec3 position, momentum;
  double timeMomentum;
};
double Radius(const glm::dvec3 &position, double horizonReference, double spin);
double Horizon(double horizonReference, double spin);
double ProgradeIsco(double horizonReference, double spin);
Ray PastRay(const glm::dvec3 &position, const glm::dvec3 &direction,
            double horizonReference, double spin);
double Hamiltonian(const Ray &ray, double horizonReference, double spin);
void AdvanceRay(Ray &ray, double horizonReference, double spin,
                double affineStep);
const char *ShaderFunctions();
} // namespace KerrSpacetime
