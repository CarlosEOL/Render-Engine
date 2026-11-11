#include "OrbitalMechanics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace {
    constexpr double pi = 3.14159265358979323846;

    double EccentricAnomaly(double meanAnomaly, double eccentricity) {
        // f(E) = E - e sin E - M is increasing for 0 <= e < 1, so the root is bracketed by [-pi, pi] when
        // M is. Newton's method from Danby's starting guess converges in a handful of steps; whenever a step
        // would leave the bracket (e near 1 flattens f'), that iteration bisects instead.
        constexpr int maxIterations = 60;
        constexpr double newtonTolerance = 1e-9; // Newton then leaves an error of about step^2 / (1 - e), below 1e-14
        double lower = -pi;
        double upper = pi;
        double anomaly = std::clamp(meanAnomaly + 0.85 * eccentricity * (meanAnomaly < 0.0 ? -1.0 : 1.0), lower, upper);
        for (int iteration = 0; iteration < maxIterations; ++iteration) {
            const double residual = anomaly - eccentricity * std::sin(anomaly) - meanAnomaly;
            if (residual == 0.0)
                break;
            if (residual > 0.0)
                upper = anomaly;
            else
                lower = anomaly;
            const double step = residual / (1.0 - eccentricity * std::cos(anomaly));
            if (std::abs(step) < newtonTolerance) {
                anomaly -= step;
                break;
            }
            const double next = anomaly - step;
            anomaly = next > lower && next < upper ? next : 0.5 * (lower + upper);
        }
        return anomaly;
    }

    double OrbitAnomaly(const OrbitalMechanics::Orbit& orbit, double gravitationalParameter, double seconds) {
        if (!std::isfinite(orbit.eccentricity) || orbit.eccentricity < 0.0 || orbit.eccentricity >= 1.0
            || !std::isfinite(seconds) || !std::isfinite(orbit.inclination)
            || !std::isfinite(orbit.ascendingNode) || !std::isfinite(orbit.initialMeanAnomaly)
            || !std::isfinite(orbit.argumentOfPeriapsis))
            throw std::invalid_argument("Elliptical orbit requires finite angles, time, and 0 <= eccentricity < 1.");
        const double meanAnomaly = std::remainder(
            orbit.initialMeanAnomaly + OrbitalMechanics::MeanMotion(gravitationalParameter, orbit.semiMajorAxis) * seconds,
            2.0 * pi);
        return EccentricAnomaly(meanAnomaly, orbit.eccentricity);
    }

    glm::dvec3 RotateOrbitPlane(const OrbitalMechanics::Orbit& orbit, double x, double y) {
        const double rotatedX = x * std::cos(orbit.argumentOfPeriapsis) - y * std::sin(orbit.argumentOfPeriapsis);
        y = x * std::sin(orbit.argumentOfPeriapsis) + y * std::cos(orbit.argumentOfPeriapsis);
        x = rotatedX;
        const double projectedY = y * std::cos(orbit.inclination);
        const double nodeCos = std::cos(orbit.ascendingNode);
        const double nodeSin = std::sin(orbit.ascendingNode);
        return glm::dvec3(x * nodeCos - projectedY * nodeSin,
                         x * nodeSin + projectedY * nodeCos,
                         y * std::sin(orbit.inclination));
    }
}

double OrbitalMechanics::MeanMotion(double gravitationalParameter, double semiMajorAxis) {
    if (!std::isfinite(gravitationalParameter) || gravitationalParameter <= 0.0
        || !std::isfinite(semiMajorAxis) || semiMajorAxis <= 0.0)
        throw std::invalid_argument("Orbital mass parameter and semi-major axis must be finite and positive.");
    return std::sqrt(gravitationalParameter / (semiMajorAxis * semiMajorAxis * semiMajorAxis));
}

glm::vec3 OrbitalMechanics::PositionAtTime(const Orbit& orbit, double gravitationalParameter, double seconds) {
    return glm::vec3(PositionAtTimeDouble(orbit, gravitationalParameter, seconds));
}

glm::dvec3 OrbitalMechanics::PositionAtTimeDouble(const Orbit& orbit, double gravitationalParameter, double seconds) {
    const double anomaly = OrbitAnomaly(orbit, gravitationalParameter, seconds);
    const double x = orbit.semiMajorAxis * (std::cos(anomaly) - orbit.eccentricity);
    const double y = orbit.semiMajorAxis * std::sqrt(1.0 - orbit.eccentricity * orbit.eccentricity)
        * std::sin(anomaly);
    return RotateOrbitPlane(orbit, x, y);
}

glm::vec3 OrbitalMechanics::VelocityAtTime(const Orbit& orbit, double gravitationalParameter, double seconds) {
    const double anomaly = OrbitAnomaly(orbit, gravitationalParameter, seconds);
    const double anomalyRate = MeanMotion(gravitationalParameter, orbit.semiMajorAxis)
        / (1.0 - orbit.eccentricity * std::cos(anomaly));
    return glm::vec3(RotateOrbitPlane(orbit,
        -orbit.semiMajorAxis * std::sin(anomaly) * anomalyRate,
        orbit.semiMajorAxis * std::sqrt(1.0 - orbit.eccentricity * orbit.eccentricity)
            * std::cos(anomaly) * anomalyRate));
}

double OrbitalMechanics::GravitationalFrequencyRatio(double emitterRadius, double observerRadius, double horizonRadius) {
    if (!std::isfinite(horizonRadius) || horizonRadius <= 0.0
        || !std::isfinite(emitterRadius) || emitterRadius <= horizonRadius
        || !std::isfinite(observerRadius) || observerRadius <= horizonRadius)
        throw std::invalid_argument("Static observers and emitters must be outside a positive horizon radius.");
    return std::sqrt((1.0 - horizonRadius / emitterRadius) / (1.0 - horizonRadius / observerRadius));
}

void OrbitalMechanics::AdvanceParticle(ParticleState& state, double gravitationalParameter, double thrustAcceleration, double seconds) {
    if (!std::isfinite(gravitationalParameter) || gravitationalParameter <= 0.0
        || !std::isfinite(thrustAcceleration) || thrustAcceleration < 0.0
        || !std::isfinite(seconds) || seconds < 0.0
        || !std::isfinite(glm::dot(state.velocity, state.velocity)))
        throw std::invalid_argument("Particle integration requires finite state, positive gravity, and nonnegative thrust and time.");
    const auto acceleration = [&](const glm::dvec3& position, const glm::dvec3& velocity) {
        const double radius = glm::length(position);
        if (!std::isfinite(radius) || radius <= 0.0)
            throw std::invalid_argument("Particle position must be finite and outside the central singularity.");
        auto result = -gravitationalParameter * position / (radius * radius * radius);
        const double speed = glm::length(velocity);
        if (thrustAcceleration > 0.0 && speed > 0.0)
            result += thrustAcceleration * velocity / speed;
        return result;
    };
    // Kick-drift-kick integration. Small adaptive substeps resolve close approaches.
    const auto halfVelocity = state.velocity + 0.5 * seconds * acceleration(state.position, state.velocity);
    const auto nextPosition = state.position + seconds * halfVelocity;
    const auto nextVelocity = halfVelocity + 0.5 * seconds * acceleration(nextPosition, halfVelocity);
    state.position = nextPosition;
    state.velocity = nextVelocity;
}

double OrbitalMechanics::ObserverClockRate(double radius, double speed, double gravitationalParameter, double horizonRadius) {
    const double lightSpeedSquared = 2.0 * gravitationalParameter / horizonRadius;
    const double gravity = glm::max(0.0, 1.0 - horizonRadius / radius);
    const double motion = glm::max(0.0001, 1.0 - speed * speed / lightSpeedSquared);
    return std::sqrt(gravity * motion);
}

double OrbitalMechanics::LightTravelDelay(const glm::dvec3& emitter, const glm::dvec3& observer,
                                        double gravitationalParameter, double horizonRadius) {
    const double lightSpeed = std::sqrt(2.0 * gravitationalParameter / horizonRadius);
    const double distance = glm::length(emitter - observer);
    const double radii = glm::length(emitter) + glm::length(observer);
    // First-order Schwarzschild (Shapiro) delay for a direct path.
    const double denominator = glm::max(radii - distance, horizonRadius * 0.0001);
    return distance / lightSpeed + horizonRadius / lightSpeed
        * std::log(glm::max(1.0, (radii + distance) / denominator));
}

const char* OrbitalMechanics::RelativityShaderFunctions() {
    // GLSL counterparts live beside the CPU equations to keep the physics in this file.
    return R"(
float frequencyRatio(float emitterRadius, float observerRadius, vec3 velocityBeta, vec3 toObserver) {
    float gravity = sqrt((1.0 - horizonRadius / emitterRadius)
                       / (1.0 - horizonRadius / observerRadius));
    float betaSquared = min(dot(velocityBeta, velocityBeta), 0.99);
    float observerFactor = (1.0 - dot(observerBeta, toObserver))
        / sqrt(max(1.0 - dot(observerBeta, observerBeta), 0.0001));
    return gravity * sqrt(1.0 - betaSquared) * observerFactor
        / max(1.0 - dot(velocityBeta, toObserver), 0.01);
}

vec3 observerRayDirection(vec3 cameraDirection) {
    float betaSquared = dot(observerBeta, observerBeta);
    if (betaSquared < 0.000001)
        return cameraDirection;
    float gamma = inversesqrt(1.0 - betaSquared);
    float projection = dot(observerBeta, cameraDirection);
    return normalize(cameraDirection + ((gamma - 1.0) * projection / betaSquared - gamma) * observerBeta);
}

vec3 circularVelocityBeta(vec3 position, vec3 axis) {
    float radius = length(position);
    float beta = sqrt(horizonRadius / (2.0 * (radius - horizonRadius)));
    return normalize(cross(axis, position)) * beta;
}

vec3 photonInitialTangent(vec3 position, vec3 direction) {
    vec3 radial = normalize(position);
    vec3 radialDirection = radial * dot(direction, radial);
    return direction - radialDirection
        + sqrt(max(1.0 - horizonRadius / length(position), 0.0)) * radialDirection;
}

vec3 photonAcceleration(vec3 position, float angularMomentumSquared) {
    float radius = length(position);
    // Cartesian form of the Schwarzschild null-orbit equation, affine parameter.
    return -1.5 * horizonRadius * angularMomentumSquared * position / pow(radius, 5.0);
}

vec3 localPhotonDirection(vec3 position, vec3 tangent) {
    vec3 radial = normalize(position);
    vec3 radialTangent = radial * dot(tangent, radial);
    return normalize(tangent - radialTangent
        + radialTangent / sqrt(max(1.0 - horizonRadius / length(position), 0.0001)));
}

vec3 blackbody(float temperature) {
    // Planck radiance sampled at red, green, and blue wavelengths (metres).
    // This is a three-band display approximation with a fixed exposure.
    vec3 wavelength = vec3(610e-9, 550e-9, 460e-9);
    vec3 scaled = wavelength / 550e-9;
    return 160.0 / (pow(scaled, vec3(5.0))
        * (exp(vec3(0.01438777) / (wavelength * max(temperature, 100.0))) - 1.0));
}
)";
}
