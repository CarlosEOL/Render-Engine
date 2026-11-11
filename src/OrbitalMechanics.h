#pragma once

#include <glm/vec3.hpp>

namespace OrbitalMechanics {
    struct ParticleState {
        glm::dvec3 position;
        glm::dvec3 velocity;
    };

    struct Orbit {
        // Scaled scene units and radians. Ellipses require a > 0 and 0 <= e < 1.
        double semiMajorAxis;
        double eccentricity;
        double inclination;
        double ascendingNode;
        double initialMeanAnomaly;
    };

    double MeanMotion(double gravitationalParameter, double semiMajorAxis);
    glm::vec3 PositionAtTime(const Orbit& orbit, double gravitationalParameter, double seconds);
    glm::vec3 VelocityAtTime(const Orbit& orbit, double gravitationalParameter, double seconds);
    double GravitationalFrequencyRatio(double emitterRadius, double observerRadius, double horizonRadius);
    void AdvanceParticle(ParticleState& state, double gravitationalParameter, double thrustAcceleration, double seconds);
    double ObserverClockRate(double radius, double speed, double gravitationalParameter, double horizonRadius);
    double LightTravelDelay(const glm::dvec3& emitter, const glm::dvec3& observer,
                           double gravitationalParameter, double horizonRadius);
    const char* RelativityShaderFunctions();
}
