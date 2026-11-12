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
        double argumentOfPeriapsis = 0.0;
    };

    double MeanMotion(double gravitationalParameter, double semiMajorAxis);
    glm::vec3 PositionAtTime(const Orbit& orbit, double gravitationalParameter, double seconds);
    glm::dvec3 PositionAtTimeDouble(const Orbit& orbit, double gravitationalParameter, double seconds);
    glm::vec3 VelocityAtTime(const Orbit& orbit, double gravitationalParameter, double seconds);
    double GravitationalFrequencyRatio(double emitterRadius, double observerRadius, double horizonRadius);
    void AdvanceParticle(ParticleState& state, double gravitationalParameter, double thrustAcceleration, double seconds);
    double ObserverClockRate(double radius, double speed, double gravitationalParameter, double horizonRadius);
    double SchwarzschildClockRate(const ParticleState& state,double horizonRadius,double lightSpeed);
    double SchwarzschildEnergy(const ParticleState& state,double horizonRadius,double lightSpeed);
    glm::dvec3 SchwarzschildAcceleration(const ParticleState& state,double gravitationalParameter,
        double lightSpeed,double properThrust=0);
    double AdvanceSchwarzschild(ParticleState& state,double gravitationalParameter,double lightSpeed,
        double properThrust,double coordinateSeconds);
    double LightTravelDelay(const glm::dvec3& emitter, const glm::dvec3& observer,
                           double gravitationalParameter, double horizonRadius);
    const char* RelativityShaderFunctions();
}
