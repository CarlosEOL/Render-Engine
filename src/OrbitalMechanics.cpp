#include "OrbitalMechanics.h"

#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace {
    constexpr double pi = 3.14159265358979323846;

    double EccentricAnomaly(double meanAnomaly, double eccentricity) {
        // Kepler's equation is monotonic for an elliptical orbit (0 <= e < 1).
        double lower = -pi;
        double upper = pi;
        double anomaly=eccentricity<.8?meanAnomaly:std::copysign(pi,meanAnomaly);
        for (int iteration=0;iteration<64;++iteration) {
            const double residual=anomaly-eccentricity*std::sin(anomaly)-meanAnomaly;
            if (std::abs(residual)<1e-14)return anomaly;
            if(residual<0)lower=anomaly;else upper=anomaly;
            const double candidate=anomaly-residual/(1-eccentricity*std::cos(anomaly));
            anomaly=candidate>lower && candidate<upper?candidate:(lower+upper)*.5;
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

double OrbitalMechanics::SchwarzschildClockRate(const ParticleState& state,double horizon,double c){
    const double r=glm::length(state.position),f=1-horizon/r;
    if(f<=0)return 0;
    const auto radial=state.position/r;
    const double vr=glm::dot(state.velocity,radial);
    const double vt2=glm::dot(state.velocity,state.velocity)-vr*vr;
    return std::sqrt(glm::max(0.0,f-vr*vr/(f*c*c)-vt2/(c*c)));
}

double OrbitalMechanics::SchwarzschildEnergy(const ParticleState& state,double horizon,double c){
    const double rate=SchwarzschildClockRate(state,horizon,c);
    if(rate<=0)throw std::invalid_argument("Energy requires an exterior timelike state");
    return (1-horizon/glm::length(state.position))/rate;
}

glm::dvec3 OrbitalMechanics::SchwarzschildAcceleration(const ParticleState& state,double mu,double c,double thrust){
    const double r=glm::length(state.position),rs=2*mu/(c*c),f=1-rs/r;
    if(f<=0 || !std::isfinite(r))throw std::invalid_argument("Schwarzschild dynamics require exterior coordinates");
    const auto er=state.position/r;
    const double vr=glm::dot(state.velocity,er);
    const auto tangent=state.velocity-er*vr;
    const double vt2=glm::dot(tangent,tangent);
    // Timelike geodesic with Schwarzschild coordinate time as the parameter.
    auto acceleration=er*(-mu*f/(r*r)+1.5*rs*vr*vr/(r*r*f)-rs*vt2/(r*r))
        +tangent*(rs*vr/(r*r*f));
    if(thrust>0){
        const auto localVelocity=er*(vr/f)+tangent/std::sqrt(f);
        const double v=glm::length(localVelocity);
        if(v>0){
            const double inverseGamma=std::sqrt(glm::max(0.0,1-v*v/(c*c)));
            const auto localAcceleration=localVelocity/v*(thrust*inverseGamma*inverseGamma*inverseGamma);
            const auto ar=er*glm::dot(localAcceleration,er);
            acceleration+=ar*(f*std::sqrt(f))+(localAcceleration-ar)*f;
        }
    }
    return acceleration;
}

double OrbitalMechanics::AdvanceSchwarzschild(ParticleState& state,double mu,double c,double thrust,double dt){
    if(!std::isfinite(dt) || dt<0 || mu<=0 || c<=0 || thrust<0)
        throw std::invalid_argument("Invalid relativistic step");
    const double horizon=2*mu/(c*c);
    const auto derivative=[&](const ParticleState& p){return ParticleState{p.velocity,SchwarzschildAcceleration(p,mu,c,thrust)};};
    const auto offset=[](const ParticleState& p,const ParticleState& k,double t){return ParticleState{p.position+k.position*t,p.velocity+k.velocity*t};};
    const auto k1=derivative(state),s2=offset(state,k1,dt*.5),k2=derivative(s2);
    const auto s3=offset(state,k2,dt*.5),k3=derivative(s3),s4=offset(state,k3,dt),k4=derivative(s4);
    const double proper=dt*(SchwarzschildClockRate(state,horizon,c)+2*SchwarzschildClockRate(s2,horizon,c)
        +2*SchwarzschildClockRate(s3,horizon,c)+SchwarzschildClockRate(s4,horizon,c))/6;
    state.position+=dt*(k1.position+2.0*k2.position+2.0*k3.position+k4.position)/6.0;
    state.velocity+=dt*(k1.velocity+2.0*k2.velocity+2.0*k3.velocity+k4.velocity)/6.0;
    if(SchwarzschildClockRate(state,horizon,c)<=0)throw std::runtime_error("Integration left the timelike domain");
    return proper;
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
    const double emitterR=glm::length(emitter),observerR=glm::length(observer);
    if(emitterR>horizonRadius && observerR>horizonRadius){
        if(distance<.001*std::min(emitterR,observerR)){
            const auto midpoint=(emitter+observer)*.5;
            const double f=1-horizonRadius/glm::length(midpoint);
            const double radial=distance>0?glm::dot((observer-emitter)/distance,glm::normalize(midpoint)):0;
            return distance/lightSpeed*std::sqrt((1+(1/f-1)*radial*radial)/f);
        }
        const double impact=distance>0?glm::length(glm::cross(emitter,observer))/distance:0;
        if(impact<1e-8*std::min(emitterR,observerR) && glm::dot(emitter,observer)>0)
            return distance/lightSpeed+horizonRadius/lightSpeed*std::abs(std::log((observerR-horizonRadius)/(emitterR-horizonRadius)));
    }
    // First-order Schwarzschild (Shapiro) delay for a direct path.
    const double denominator = glm::max(radii - distance, horizonRadius * 0.0001);
    double delay=distance / lightSpeed + horizonRadius / lightSpeed
        * std::log(glm::max(1.0, (radii + distance) / denominator));
    if(emitterR>horizonRadius && observerR>horizonRadius){
        const auto correction=[&](double r){return std::log1p(distance/(r-horizonRadius))-std::log1p(distance/r);};
        delay+=horizonRadius/lightSpeed*(correction(emitterR)+correction(observerR));
    }
    return delay;
}

const char* OrbitalMechanics::RelativityShaderFunctions() {
    // GLSL counterparts live beside the CPU equations to keep the physics in this file.
    return R"(
float frequencyRatio(float emitterRadius, float observerRadius, vec3 velocityBeta, vec3 toObserver) {
    float emitterTerm = max(1.0 - horizonRadius / emitterRadius, 0.0001);
    float observerTerm = observerRadius > horizonRadius
        ? max(1.0 - horizonRadius / observerRadius, 0.0001) : 0.0001;
    float gravity = sqrt(emitterTerm / observerTerm);
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
    float radius = length(position);
    vec3 radial = normalize(position);
    vec3 radialDirection = radial * dot(direction, radial);
    float radialScale = radius > horizonRadius
        ? sqrt(max(1.0 - horizonRadius / radius, 0.0)) : 1.0;
    return direction - radialDirection + radialScale * radialDirection;
}

vec3 photonAcceleration(vec3 position, float angularMomentumSquared) {
    float radius = length(position);
    // Cartesian form of the Schwarzschild null-orbit equation, affine parameter.
    return -1.5 * horizonRadius * angularMomentumSquared * position / pow(radius, 5.0);
}

vec3 localPhotonDirection(vec3 position, vec3 tangent) {
    float radius = length(position);
    vec3 radial = normalize(position);
    vec3 radialTangent = radial * dot(tangent, radial);
    float radialScale = radius > horizonRadius
        ? 1.0 / sqrt(max(1.0 - horizonRadius / radius, 0.0001)) : 1.0;
    return normalize(tangent - radialTangent + radialTangent * radialScale);
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
