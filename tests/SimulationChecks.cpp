#include "Astronomy.h"
#include "CameraMotion.h"
#include "QuantumStates.h"
#include "StarSystem.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

// The catalogue is exercised without creating an OpenGL context.
unsigned int compileShaderProgram(const char *, const char *)
{
    return 0;
}
void Require(bool result, const char *message)
{
    if (!result)
        throw std::runtime_error(message);
}
int main()
{
    Require(Astronomy::gargantuaHorizonAu>1.97 && Astronomy::gargantuaHorizonAu<1.98,"Black hole mass-to-horizon scale incorrect");
    Require(Astronomy::millerOrbitAu>Astronomy::gargantuaHorizonAu*3,"Miller model orbit is inside Schwarzschild ISCO");
    Require(std::abs(Astronomy::millerRadiusAu*Astronomy::metresPerAu-6371000)<.001,"Miller radius is not Earth scale");
    Require(std::abs(Astronomy::observerClearanceAu*Astronomy::metresPerAu-6.6)<1e-10,"Telescope observer clearance incorrect");
    for(double eccentricity:{0.0,.1,.8,.96714,.999999})for(double phase:{0.0,1e-9,.1,1.5,3.141592653589793}) {
        OrbitalMechanics::Orbit orbit{1,eccentricity,.1,.3,phase};
        const auto p=OrbitalMechanics::PositionAtTimeDouble(orbit,1,0);
        Require(std::isfinite(glm::length(p)),"Fast Kepler solver produced nonfinite orbit");
        Require(glm::length(p)>=1-eccentricity-1e-12 && glm::length(p)<=1+eccentricity+1e-12,"Fast Kepler solver left orbital bounds");
    }
    using namespace QuantumStates;
    for (const auto &example : Examples())
    {
        if (example.family == Family::Box || example.family == Family::Oscillator || example.family == Family::Packet)
        {
            for (double time : {0.0, .7, 4.0})
            {
                double norm = 0;
                constexpr double dx = .004;
                for (int i = 0; i < 40000; ++i)
                {
                    const double x = -80 + (i + .5) * dx;
                    norm += std::norm(Evaluate(example, x, 0, time)) * dx;
                }
                if (std::abs(norm - 1) > 1e-5)
                    std::cerr << example.name << " norm=" << norm << "\n";
                Require(std::abs(norm - 1) < 1e-5, "Analytic wavefunction normalization failed");
            }
        }
        if (example.family == Family::Spin || example.family == Family::Bell)
        {
            for (double time : {0.0, .7, 4.0})
            {
                double norm = 0;
                for (double p : Probabilities(example, time))
                {
                    Require(p >= 0 && p <= 1, "Invalid probability");
                    norm += p;
                }
                Require(std::abs(norm - 1) < 1e-12, "Qubit probabilities not normalized");
            }
        }
    }
    for (int potential = 0; potential < 3; ++potential)
    {
        Solver solver;
        solver.Reset(potential);
        Require(std::abs(solver.Norm() - 1) < 1e-12, "Initial numerical norm failed");
        solver.Advance(5);
        Require(std::abs(solver.Norm() - 1) < 1e-10, "Crank-Nicolson lost probability");
        if (potential == 0)
        {
            double left = 0, right = 0;
            for (int i = 0; i < Solver::size; ++i)
            {
                const double x = -Solver::extent + i * Solver::dx;
                if (x < -.5)
                    left += std::norm(solver.Wave()[i]) * Solver::dx;
                if (x > .5)
                    right += std::norm(solver.Wave()[i]) * Solver::dx;
            }
            Require(left > .1 && right > .01, "Barrier did not produce reflected and transmitted waves");
            std::cout << "Barrier R=" << left << " T=" << right << " norm=" << solver.Norm() << "\n";
        }
    }
    StarSystem solar;
    solar.LoadSolarSystem();
    for (int orbital = 0; orbital < 7; ++orbital)
    {
        double norm = 0, angular = 0;
        for (int i = 0; i < 20000; ++i)
            norm += std::pow(HydrogenRadial(orbital, (i + .5) * .01), 2) * .01;
        Require(std::abs(norm - 1) < 1e-7, "Hydrogen radial probability not normalized");
        const double radialAtOne = HydrogenRadial(orbital, 1);
        for (int i = 0; i < 4096; ++i)
        {
            const double z = 1 - 2 * (i + .5) / 4096, angle = i * 2.399963229728653;
            const double x = std::sqrt(1 - z * z) * std::cos(angle), y = std::sqrt(1 - z * z) * std::sin(angle);
            angular +=
                std::norm(Hydrogen3D(orbital, x, y, z, 0)) / (radialAtOne * radialAtOne) * 12.566370614359172 / 4096;
        }
        Require(std::abs(angular - 1) < 1e-4, "Hydrogen angular probability not normalized");
    }
    Require(solar.Planets().size() == 20 && solar.AsteroidCount() == 4096, "Solar system catalogue incomplete");
    const auto &earth = solar.Planets()[2];
    const double year =
        6.283185307179586 / OrbitalMechanics::MeanMotion(Astronomy::solarGravityAu, earth.orbit.semiMajorAxis);
    Require(year / 86400 > 365 && year / 86400 < 366, "Earth year not physical");
    Require(std::abs(earth.radiusEarth * Astronomy::earthRadiusAu * Astronomy::metresPerAu - 6371008.4) < .01,
            "Earth radius scale incorrect");
    Require(glm::length(solar.Position(2, 0) - solar.Position(2, year)) < 1e-9, "Earth orbit not closed");
    const double moonDistance = glm::length(solar.Position(13, 0) - solar.Position(2, 0));
    Require(moonDistance * Astronomy::metresPerAu > 350e6 && moonDistance * Astronomy::metresPerAu < 410e6,
            "Moon did not orbit Earth at physical distance");
    glm::vec3 front(0, 0, -1), up(0, 1, 0);
    for (int i = 0; i < 10000; ++i)
    {
        CameraMotion::Roll(front, up, .013f);
        CameraMotion::Look(front, up, .007f, -.005f, false);
    }
    Require(std::abs(glm::dot(front, up)) < 1e-5f && std::abs(glm::length(up) - 1) < 1e-5f, "Camera basis drifted");
    solar.SelectPlanet(2);
    const glm::dvec3 offset(earth.radiusEarth * Astronomy::earthRadiusAu * 5, 0, 0);
    solar.camera = solar.Position(2, 0) + offset;
    solar.ToggleRotationFollow(0);
    const double quarterDay = earth.daySeconds * .25;
    solar.Update(quarterDay, front, up);
    const auto tilt = glm::rotate(glm::dmat4(1), static_cast<double>(earth.tilt), glm::dvec3(1, 0, 0));
    const glm::dmat3 delta(tilt * glm::rotate(glm::dmat4(1), 1.5707963267948966, glm::dvec3(0, 1, 0)) *
                           glm::transpose(tilt));
    Require(glm::length(solar.camera - solar.Position(2, quarterDay) - delta * offset) < 1e-12,
            "Camera did not follow object rotation");
    const auto pausedCamera = solar.camera;
    solar.Update(quarterDay, front, up);
    Require(glm::length(solar.camera - pausedCamera) < 1e-12, "Paused rotation-follow moved camera");
    StarSystem a, b, c;
    Require(StarSystem::DescribeStar(5,"Orbiting star 2").temperature==10000,"Orbiting star metadata diverged from rendered temperature");
    a.Generate(64, "A");
    b.Generate(64, "A");
    c.Generate(65, "B");
    Require(a.Planets().size() == b.Planets().size(), "Generation not deterministic");
    Require(a.Planets()[0].seed == b.Planets()[0].seed && a.Planets()[0].seed != c.Planets()[0].seed,
            "Planet seeds not deterministic and distinct");
    for (int i = 0; i < static_cast<int>(a.Planets().size()); ++i)
        Require(glm::length(a.Position(i, 12345) - b.Position(i, 12345)) < 1e-12,
                "Generated positions not deterministic");
    std::cout << Examples().size()
              << " quantum presets checked; solar scale, hierarchy and generation checks passed.\n";
}
