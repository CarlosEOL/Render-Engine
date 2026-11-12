#include "KerrSpacetime.h"
#include "Astronomy.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
void Require(bool test, const char *message) {
  if (!test)
    throw std::runtime_error(message);
}
int main() {
  try {
    for (double spin : {0.0, .8, .98, -.8}) {
      constexpr double rs = 2;
      auto observer =
          KerrSpacetime::MakeStationaryObserver({6, .2, .1}, rs, spin);
      Require(glm::length(KerrSpacetime::ObserverCoordinateVelocity(
                  observer, rs, spin)) < 1e-12,
              "Stationary initialization has coordinate motion");
      const double energy = observer.state.timeMomentum;
      double residual = 0;
      for (int i = 0; i < 60000 &&
                      KerrSpacetime::Radius(observer.state.position, rs, spin) >
                          KerrSpacetime::Horizon(rs, spin) * .85;
           ++i) {
        const double before = observer.coordinateSeconds;
        KerrSpacetime::AdvanceObserver(observer, rs, spin, .5,
                                       Astronomy::lightSpeedAu);
        Require(std::isfinite(glm::length(observer.state.position)) &&
                    observer.coordinateSeconds > before,
                "Horizon crossing is nonfinite or reverses time");
        residual = std::max(residual, std::abs(KerrSpacetime::Hamiltonian(
                                                   observer.state, rs, spin) +
                                               .5));
      }
      Require(KerrSpacetime::Radius(observer.state.position, rs, spin) <
                  KerrSpacetime::Horizon(rs, spin),
              "Observer failed to cross outer horizon");
      Require(residual < 1e-8 && observer.state.timeMomentum == energy,
              "Mass shell or stationary energy drifted");
      const auto beta = KerrSpacetime::ObserverBeta(observer, rs, spin);
      Require(glm::length(beta) < 1, "Physical observer became superluminal");
      for (const auto &direction : {glm::dvec3(1, 0, 0), glm::dvec3(-1, 0, 0),
                                    glm::normalize(glm::dvec3(.2, .3, 1))}) {
        const auto ray = KerrSpacetime::ObserverPastRay(
            observer.state.position, direction, rs, spin, beta);
        Require(std::abs(KerrSpacetime::Hamiltonian(ray, rs, spin)) < 1e-10,
                "Observer ray is not null");
        Require(
            std::abs(KerrSpacetime::ReceivedEnergy(ray, observer, rs, spin) -
                     1) < 1e-10,
            "Observer ray lacks unit received energy");
      }
      auto ray = KerrSpacetime::ObserverPastRay(
          observer.state.position, glm::normalize(observer.state.position), rs,
          spin, beta);
      for (int i = 0; i < 10000 && glm::length(ray.position) < 20; ++i)
        KerrSpacetime::AdvanceRay(ray, rs, spin, .01);
      Require(glm::length(ray.position) >= 20,
              "Exterior light did not reach the interior observer");
      std::cout << "spin=" << spin << " crossed; mass-shell error=" << residual
                << "; proper=" << observer.properSeconds
                << " s; coordinate=" << observer.coordinateSeconds
                << " s; exterior light visible\n";
    }
    std::cout << "Kerr observer checks PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
