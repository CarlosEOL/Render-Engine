#include "glad/glad.h"
#include "BlackHole.h"
#include "OrbitalMechanics.h"
#include "PlanetTerrain.h"

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char* vertexSrc, const char* fragmentSrc);

namespace {
    constexpr double gravitationalParameter = 1.8;
    constexpr float horizonRadius = 0.24f;
    constexpr float innerDiskRadius = 3.0f * horizonRadius;
    constexpr float outerDiskRadius = 1.03f;
    const OrbitalMechanics::Orbit spacecraftOrbit{1.7, 0.46, 0.35, 0.9, glm::pi<double>()};
    constexpr double boosterAcceleration = 0.85;

    enum Surface { Planet = 2, Star = 3 };

    struct Body {
        OrbitalMechanics::Orbit orbit;
        float radius;
        glm::vec3 color;
        Surface surface;
        float temperature;
    };

    const Body bodies[] = {
        {{1.38, 0.16, 0.20, 0.2, 0.8}, 0.085f, {0.22f, 0.60f, 1.0f}, Planet, 0.0f},
        {{2.75, 0.08, 0.32, 1.7, 2.4}, 0.070f, {0.35f, 0.70f, 0.85f}, Planet, 0.0f},
        {{3.50, 0.06, 0.55, 0.7, 4.8}, 0.060f, {0.25f, 0.55f, 0.90f}, Planet, 0.0f},
        {{1.28, 0.03, 0.45, 0.1, 2.2}, 0.041f, {1.0f, 0.77f, 0.45f}, Star, 5200.0f},
        {{1.55, 0.12, 0.83, 1.4, 4.5}, 0.052f, {0.64f, 0.80f, 1.0f}, Star, 10000.0f},
        {{1.76, 0.15, 0.72, 2.3, 0.4}, 0.037f, {1.0f, 0.52f, 0.36f}, Star, 3500.0f},
        {{1.98, 0.11, 1.10, 0.3, 3.4}, 0.050f, {1.0f, 0.92f, 0.77f}, Star, 6200.0f},
        {{2.10, 0.13, 0.57, 2.8, 5.7}, 0.046f, {0.68f, 0.81f, 1.0f}, Star, 8500.0f},
    };

    glm::mat4 DiskOrientation() {
        return glm::rotate(glm::mat4(1.0f), glm::radians(67.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    glm::mat4 AttitudeAlong(const glm::vec3& direction) {
        const auto forward = glm::normalize(direction);
        const glm::vec3 referenceUp = std::abs(forward.y) < 0.95f
            ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const auto right = glm::normalize(glm::cross(referenceUp, forward));
        glm::mat4 orientation(1.0f);
        orientation[0] = glm::vec4(right, 0.0f);
        orientation[1] = glm::vec4(glm::cross(forward, right), 0.0f);
        orientation[2] = glm::vec4(forward, 0.0f);
        return orientation;
    }

    const char* vertexBody = R"(
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 packetColor;
uniform mat4 model;
uniform mat4 viewProjection;
uniform float wavePhase;
uniform int surface;
uniform bool oceanWorld;
out vec3 localPosition;
out vec3 worldPosition;
out vec3 photonColor;

void main() {
    vec3 displaced = position;
    if (surface == 2) {
        displaced = oceanWorld ? position * (1.0 + 0.0015 * oceanWave(position, wavePhase))
            : planetSurface(position, terrainHeight(position), wavePhase);
    }
    vec4 world = model * vec4(displaced, 1.0);
    localPosition = position;
    worldPosition = world.xyz;
    photonColor = packetColor;
    gl_Position = viewProjection * world;
}
)";

    const char* fragmentBody = R"(
in vec3 localPosition;
in vec3 worldPosition;
in vec3 photonColor;
out vec4 fragColor;
uniform int surface;
uniform bool oceanWorld;
uniform vec3 bodyColor;
uniform vec3 cameraPosition;
uniform vec3 velocityBeta;
uniform float temperature;
uniform float wavePhase;
uniform mat4 model;

void main() {
    if (surface == 2) {
        vec3 radial = normalize(localPosition);
        float height = oceanWorld ? seaLevel - 0.3 : terrainHeight(radial);
        float water = oceanWorld ? 1.0 : 1.0 - smoothstep(seaLevel - 0.008, seaLevel + 0.008, height);
        float depth = max(seaLevel - height, 0.0);
        vec3 surfacePosition = oceanWorld ? radial * (1.0 + 0.0015 * oceanWave(radial, wavePhase))
            : planetSurface(radial, height, wavePhase);
        vec3 localNormal = normalize(cross(dFdx(surfacePosition), dFdy(surfacePosition)));
        if (dot(localNormal, radial) < 0.0)
            localNormal = -localNormal;
        vec3 normal = normalize(mat3(transpose(inverse(model))) * localNormal);
        vec3 lightDirection = normalize(-worldPosition);
        vec3 viewDirection = normalize(cameraPosition - worldPosition);
        float light = max(dot(normal, lightDirection), 0.0);
        // The half-vector term stays positive past the terminator at grazing angles, so gate it by the lit side.
        float specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), 100.0) * water
            * smoothstep(0.0, 0.1, dot(normal, lightDirection));
        vec3 ocean = mix(vec3(0.04, 0.42, 0.46), vec3(0.005, 0.025, 0.09),
            1.0 - exp(-12.0 * depth));
        float moisture = 0.5 + 0.5 * terrainNoise(radial * 5.0 + vec3(31.0));
        vec3 land = mix(vec3(0.40, 0.30, 0.15), vec3(0.07, 0.23, 0.09), moisture);
        land = mix(vec3(0.62, 0.54, 0.35), land, smoothstep(seaLevel, seaLevel + 0.045, height));
        land = mix(land, vec3(0.30, 0.29, 0.27), smoothstep(0.12, 0.28, height));
        float snow = smoothstep(0.22, 0.36, height + 0.15 * pow(abs(radial.y), 4.0));
        land = mix(land, vec3(0.86, 0.90, 0.92), snow);
        land *= 0.82 + 0.30 * (0.5 + 0.5 * terrainNoise(radial * 350.0));
        float foam = (1.0 - smoothstep(0.003, 0.018, depth))
            * (0.5 + 0.5 * oceanWave(radial, wavePhase)) * water;
        vec3 albedo = mix(land, ocean, water);
        albedo = mix(albedo, vec3(0.78, 0.87, 0.86), 0.45 * foam);
        fragColor = vec4(albedo * (0.30 + 0.70 * light) + specular, 1.0);
    } else if (surface == 3) {
        float shift = frequencyRatio(length(worldPosition), length(cameraPosition),
            velocityBeta, normalize(cameraPosition - worldPosition));
        fragColor = vec4(blackbody(temperature * shift), 1.0);
    } else if (surface == 8) {
        fragColor = vec4(photonColor, 1.0);
    } else if (surface == 7) {
        float seed = fract(sin(dot(worldPosition, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float distanceSquared = dot(cameraPosition - worldPosition, cameraPosition - worldPosition);
        float flux = (0.0002 + 0.25 * pow(seed, 5.0)) * 2000.0 / max(distanceSquared, 1.0);
        vec3 tint = mix(vec3(1.0, 0.67, 0.40), vec3(0.62, 0.78, 1.0), seed);
        fragColor = vec4(tint * flux, 1.0);
    } else if (surface == 5) {
        vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
        vec3 viewDirection = normalize(cameraPosition - worldPosition);
        if (dot(normal, viewDirection) < 0.0)
            normal = -normal;
        vec3 lightDirection = normalize(vec3(1.0, 1.0, 2.0) - worldPosition);
        float diffuse = max(dot(normal, lightDirection), 0.0);
        float specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), 24.0)
            * smoothstep(0.0, 0.1, dot(normal, lightDirection));
        fragColor = vec4(bodyColor * (0.22 + 0.78 * diffuse) + 0.5 * specular, 1.0);
    } else if (surface == 6) {
        vec3 normal = normalize(mat3(transpose(inverse(model))) * localPosition);
        float facing = max(dot(normal, normalize(cameraPosition - worldPosition)), 0.0);
        float head = clamp(0.5 + 0.5 * localPosition.z, 0.0, 1.0);
        vec3 flame = mix(vec3(0.02, 0.12, 0.8), vec3(0.8, 0.95, 1.0), head);
        fragColor = vec4(flame, 0.8 * pow(facing, 0.7) * smoothstep(0.0, 0.5, head));
    } else {
        fragColor = vec4(bodyColor, 1.0);
    }
}
)";

    const char* screenVertexSource = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0.0, 1.0);
}
)";

    const char* lensFragmentBody = R"(
in vec2 screenPosition;
out vec4 fragColor;
uniform mat4 inverseViewProjection;
uniform mat4 viewProjection;
uniform mat4 inverseDiskOrientation;
uniform vec3 cameraPosition;
uniform vec3 diskAxis;
uniform vec2 diskRadii;
uniform float diskPhase;

void main() {
    vec4 farPoint = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 cameraDirection = normalize(farPoint.xyz / farPoint.w - cameraPosition);
    vec3 direction = observerRayDirection(cameraDirection);
    float observerRadius = length(cameraPosition);
    // Rays whose impact parameters exceed the disk's apparent extent miss the scene.
    if (dot(cameraPosition, direction) > 0.0 && observerRadius > 2.0)
        discard;
    if (length(cross(cameraPosition, direction)) > 2.0)
        discard;

    vec3 position = cameraPosition;
    vec3 tangent = photonInitialTangent(position, direction);
    vec3 momentum = cross(position, tangent);
    float angularMomentumSquared = dot(momentum, momentum);
    float travelled = 0.0;
    bool hit = false;
    vec3 radiance = vec3(0.0);

    for (int step = 0; step < 240; ++step) {
        float radius = length(position);
        if (radius < horizonRadius * 1.015) {
            hit = true;
            break;
        }
        if (radius > max(observerRadius, 3.0) + 4.0 && dot(position, tangent) > 0.0)
            break;

        float interval = clamp(radius * 0.065, 0.004, 0.6);
        vec3 acceleration = photonAcceleration(position, angularMomentumSquared);
        vec3 nextPosition = position + interval * tangent + 0.5 * interval * interval * acceleration;
        vec3 nextTangent = tangent + 0.5 * interval
            * (acceleration + photonAcceleration(nextPosition, angularMomentumSquared));

        float planeBefore = dot(position, diskAxis);
        float planeAfter = dot(nextPosition, diskAxis);
        if (planeBefore * planeAfter < 0.0) {
            float fraction = planeBefore / (planeBefore - planeAfter);
            vec3 intersection = mix(position, nextPosition, fraction);
            float diskRadius = length(intersection);
            float radial = (diskRadius - diskRadii.x) / (diskRadii.y - diskRadii.x);
            float edge = smoothstep(0.0, 0.06, radial) * (1.0 - smoothstep(0.8, 1.0, radial));
            // Where the fade has reached zero the disk is absent: stopping here would write opaque black
            // and depth over the fringe, hiding the galaxies and every later image of the disk.
            if (diskRadius > diskRadii.x && diskRadius < diskRadii.y && edge > 0.001) {
                vec3 diskPosition = (inverseDiskOrientation * vec4(intersection, 1.0)).xyz;
                float angle = atan(diskPosition.y, diskPosition.x) - diskPhase;
                float bands = 0.6 + 0.4 * sin(65.0 * radial + 1.5 * sin(3.0 * angle));
                float spiral = 0.6 + 0.4 * cos(5.0 * angle + 22.0 * radial);
                vec3 toObserver = -localPhotonDirection(intersection, mix(tangent, nextTangent, fraction));
                float shift = frequencyRatio(diskRadius, observerRadius,
                    circularVelocityBeta(intersection, diskAxis), toObserver);
                float temperature = 5200.0 * pow(diskRadius / diskRadii.x, -0.75);
                radiance = 4.0 * blackbody(temperature * shift) * edge * (0.4 + 0.3 * bands + 0.3 * spiral);
                travelled += length(intersection - position);
                hit = true;
                break;
            }
        }
        travelled += length(nextPosition - position);
        position = nextPosition;
        tangent = nextTangent;
    }
    if (!hit)
        discard;

    fragColor = vec4(radiance, 1.0);
    // Approximate depth allows the rasterized orbiting bodies to cross the lensed disk.
    vec4 clip = viewProjection * vec4(cameraPosition + cameraDirection * travelled, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 0.99999);
}
)";

    glm::vec3 SpherePoint(float latitude, float longitude) {
        const float ring = std::cos(latitude);
        return {ring * std::cos(longitude), ring * std::sin(longitude), std::sin(latitude)};
    }

    std::vector<glm::vec3> MakeSphere() {
        constexpr int latitudeSegments = 64;
        constexpr int longitudeSegments = 96;
        std::vector<glm::vec3> vertices;
        for (int row = 0; row < latitudeSegments; ++row) {
            const float lower = -glm::half_pi<float>() + glm::pi<float>() * row / latitudeSegments;
            const float upper = lower + glm::pi<float>() / latitudeSegments;
            for (int col = 0; col < longitudeSegments; ++col) {
                const float left = glm::two_pi<float>() * col / longitudeSegments;
                const float right = glm::two_pi<float>() * (col + 1) / longitudeSegments;
                const auto a = SpherePoint(lower, left);
                const auto b = SpherePoint(lower, right);
                const auto c = SpherePoint(upper, right);
                const auto d = SpherePoint(upper, left);
                vertices.insert(vertices.end(), {a, b, c, a, c, d});
            }
        }
        return vertices;
    }

    std::vector<glm::vec3> MakeCraftRing() {
        std::vector<glm::vec3> vertices;
        const auto point = [](int ring, int tube) {
            const float angle = glm::two_pi<float>() * ring / 48.0f;
            const float section = glm::two_pi<float>() * tube / 8.0f;
            const float radius = 0.045f + 0.005f * std::cos(section);
            return glm::vec3(radius * std::cos(angle), radius * std::sin(angle), 0.005f * std::sin(section));
        };
        for (int ring = 0; ring < 48; ++ring) {
            for (int tube = 0; tube < 8; ++tube) {
                vertices.insert(vertices.end(), {point(ring, tube), point(ring + 1, tube),
                    point(ring + 1, tube + 1), point(ring, tube), point(ring + 1, tube + 1), point(ring, tube + 1)});
            }
        }
        return vertices;
    }

}

bool BlackHole::Initialize() {
    const std::string vertexSource = std::string("#version 330 core\n")
        + PlanetTerrain::ShaderFunctions() + vertexBody;
    const std::string fragmentSource = std::string("#version 330 core\nuniform float horizonRadius;\nuniform vec3 observerBeta;\n")
        + OrbitalMechanics::RelativityShaderFunctions() + PlanetTerrain::ShaderFunctions() + fragmentBody;
    program = compileShaderProgram(vertexSource.c_str(), fragmentSource.c_str());
    const std::string lensSource = std::string("#version 330 core\nuniform float horizonRadius;\nuniform vec3 observerBeta;\n")
        + OrbitalMechanics::RelativityShaderFunctions() + lensFragmentBody;
    lensProgram = compileShaderProgram(screenVertexSource, lensSource.c_str());
    int linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    int lensLinked = GL_FALSE;
    glGetProgramiv(lensProgram, GL_LINK_STATUS, &lensLinked);
    if (linked != GL_TRUE || lensLinked != GL_TRUE) {
        Release();
        return false;
    }

    const auto upload = [](Mesh& mesh, const std::vector<glm::vec3>& vertices) {
        mesh.vertexCount = static_cast<int>(vertices.size());
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.buffer);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glEnableVertexAttribArray(0);
    };
    upload(sphere, MakeSphere());
    upload(craftRing, MakeCraftRing());
    glGenVertexArrays(1, &photonPackets.vao);
    glGenBuffers(1, &photonPackets.buffer);
    glBindVertexArray(photonPackets.vao);
    glBindBuffer(GL_ARRAY_BUFFER, photonPackets.buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(PhotonPacket), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PhotonPacket), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PhotonPacket),
        reinterpret_cast<void*>(offsetof(PhotonPacket, color)));
    glEnableVertexAttribArray(1);
    glGenVertexArrays(1, &craftTrail.vao);
    glGenBuffers(1, &craftTrail.buffer);
    glBindVertexArray(craftTrail.vao);
    glBindBuffer(GL_ARRAY_BUFFER, craftTrail.buffer);
    glBufferData(GL_ARRAY_BUFFER, 2048 * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);
    glGenVertexArrays(1, &screenVao);
    glGenVertexArrays(1,&guides.vao);
    glGenBuffers(1,&guides.buffer);
    glBindVertexArray(guides.vao);
    glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(glm::vec3),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);
    glEnableVertexAttribArray(0);
    std::mt19937 random(618);
    std::uniform_real_distribution<float> component(-1.0f, 1.0f);
    std::vector<glm::vec3> stars;
    while (stars.size() < 1600) {
        const glm::vec3 direction(component(random), component(random), component(random));
        const float squaredRadius = glm::dot(direction, direction);
        if (squaredRadius > 0.01f && squaredRadius <= 1.0f)
            stars.push_back(glm::normalize(direction) * 45.0f);
    }
    upload(backgroundStars, stars);
    // Each cluster has a spiral disk, a finite thickness, and a compact bulge.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int galaxy = 0; galaxy < 3; ++galaxy) {
        for (int star = 0; star < 1800; ++star) {
            const float radius = 11.0f * std::sqrt(unit(random));
            const float arm = glm::two_pi<float>() * (star % 3) / 3.0f;
            const float angle = arm + radius * 0.55f + (unit(random) - 0.5f) * 0.65f;
            const float thickness = (unit(random) - 0.5f) * (0.4f + 1.2f * std::exp(-radius));
            const auto local = glm::vec3(radius * std::cos(angle), thickness, radius * std::sin(angle));
            const auto& group = GalaxyBackground::Groups()[galaxy];
            const auto tilt = glm::rotate(glm::mat4(1), group.tilt, glm::vec3(1, 0, 0));
            stars.push_back(group.center + glm::vec3(tilt * glm::vec4(local, 0)));
        }
    }
    // Reuse the uploaded star buffer for the complete navigable catalogue.
    glBindBuffer(GL_ARRAY_BUFFER, backgroundStars.buffer);
    glBufferData(GL_ARRAY_BUFFER, stars.size() * sizeof(glm::vec3), stars.data(), GL_STATIC_DRAW);
    backgroundStars.vertexCount = static_cast<int>(stars.size());
    destinations = std::move(stars);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!atmosphere.Initialize()) {
        Release();
        return false;
    }
    galaxies.Initialize();
    return true;
}

void BlackHole::ResetSpacecraft(double seconds) {
    spacecraft.position = glm::dvec3(DiskOrientation() * glm::vec4(
        OrbitalMechanics::PositionAtTime(spacecraftOrbit, gravitationalParameter, 0.0), 1.0f));
    spacecraft.velocity = glm::dvec3(DiskOrientation() * glm::vec4(
        OrbitalMechanics::VelocityAtTime(spacecraftOrbit, gravitationalParameter, 0.0), 0.0f));
    spacecraftTime = seconds;
    fuelSeconds = 1.2;
    boosterFiring = false;
    spacecraftCaptured = false;
    trailSampleTime = seconds;
    trail.assign(1, glm::vec3(spacecraft.position));
    missionStart = seconds;
    flightHistory = {{seconds, spacecraft, false, false}};
}

void BlackHole::BoostSpacecraft() {
    if (!spacecraftCaptured && fuelSeconds > 0.0)
        boosterFiring = true;
}

const char* BlackHole::SpacecraftStatus() const {
    if (spacecraftCaptured)
        return "Captured";
    if (boosterFiring && fuelSeconds > 0.0)
        return "Boosting";
    if (fuelSeconds > 0.0)
        return "Coasting / periapsis burn armed";
    const double specificEnergy = 0.5 * glm::dot(spacecraft.velocity, spacecraft.velocity)
        - gravitationalParameter / glm::length(spacecraft.position);
    return specificEnergy > 0.0 ? "Escape trajectory" : "Bound orbit";
}

void BlackHole::Update(double seconds) {
    if (seconds <= spacecraftTime || spacecraftCaptured)
        return;
    while (spacecraftTime < seconds && !spacecraftCaptured) {
        const double radius = glm::length(spacecraft.position);
        if (radius <= horizonRadius * 1.05) {
            spacecraftCaptured = true;
            flightHistory.push_back({spacecraftTime, spacecraft, false, true});
            break;
        }
        // A periapsis-burn controller responds to radius and inbound velocity.
        if (radius < 1.05 && glm::dot(spacecraft.position, spacecraft.velocity) < 0.0)
            BoostSpacecraft();

        double interval = glm::min(seconds - spacecraftTime,
            glm::min(10.0, 0.01 * std::sqrt(radius * radius * radius / gravitationalParameter)));
        const double thrust = boosterFiring && fuelSeconds > 0.0 ? boosterAcceleration : 0.0;
        if (thrust > 0.0)
            interval = glm::min(interval, fuelSeconds);
        OrbitalMechanics::AdvanceParticle(spacecraft, gravitationalParameter, thrust, interval);
        spacecraftTime += interval;
        if (thrust > 0.0) {
            fuelSeconds = glm::max(0.0, fuelSeconds - interval);
            if (fuelSeconds == 0.0)
                boosterFiring = false;
        }
        const double sampleInterval = glm::max(0.02, (spacecraftTime - missionStart) * 0.01);
        if (spacecraftTime - flightHistory.back().time >= sampleInterval || thrust > 0.0)
            flightHistory.push_back({spacecraftTime, spacecraft, boosterFiring, false});
        if (spacecraftTime - trailSampleTime >= 0.04) {
            if (trail.size() == 2048)
                trail.erase(trail.begin(), trail.begin() + 512);
            trail.push_back(glm::vec3(spacecraft.position));
            trailSampleTime = spacecraftTime;
        }
    }
    if (!craftTrail.buffer)
        return;
    glBindBuffer(GL_ARRAY_BUFFER, craftTrail.buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, trail.size() * sizeof(glm::vec3), trail.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

BlackHole::FlightSample BlackHole::FlightAt(double seconds) const {
    if (seconds < missionStart) {
        auto state = OrbitalMechanics::ParticleState{
            glm::dvec3(DiskOrientation() * glm::vec4(OrbitalMechanics::PositionAtTime(
                spacecraftOrbit, gravitationalParameter, seconds - missionStart), 1)),
            glm::dvec3(DiskOrientation() * glm::vec4(OrbitalMechanics::VelocityAtTime(
                spacecraftOrbit, gravitationalParameter, seconds - missionStart), 0))};
        return {seconds, state, false, false};
    }
    auto upper = std::lower_bound(flightHistory.begin(), flightHistory.end(), seconds,
        [](const FlightSample& sample, double time) { return sample.time < time; });
    if (upper == flightHistory.begin())
        return *upper;
    if (upper == flightHistory.end()) {
        const auto& lower = flightHistory.back();
        const double span = spacecraftTime - lower.time;
        const double fraction = span > 0.0 ? glm::clamp((seconds - lower.time) / span, 0.0, 1.0) : 0.0;
        return {seconds, {glm::mix(lower.state.position, spacecraft.position, fraction),
                          glm::mix(lower.state.velocity, spacecraft.velocity, fraction)},
                lower.firing, spacecraftCaptured};
    }
    const auto& lower = *(upper - 1);
    const double fraction = (seconds - lower.time) / (upper->time - lower.time);
    return {seconds, {glm::mix(lower.state.position, upper->state.position, fraction),
                      glm::mix(lower.state.velocity, upper->state.velocity, fraction)},
            lower.firing, lower.captured};
}

BlackHole::FlightSample BlackHole::ObservedFlight(const glm::vec3& camera, double seconds) const {
    // Solve t_observe = t_emit + travelDelay(t_emit) against the simulated history.
    double lower = missionStart - OrbitalMechanics::LightTravelDelay(
        flightHistory.front().state.position, glm::dvec3(camera), gravitationalParameter, horizonRadius) - 10.0;
    double upper = seconds;
    for (int iteration = 0; iteration < 40; ++iteration) {
        const double emission = 0.5 * (lower + upper);
        const auto sample = FlightAt(emission);
        const double arrival = emission + OrbitalMechanics::LightTravelDelay(
            sample.state.position, glm::dvec3(camera), gravitationalParameter, horizonRadius);
        if (arrival < seconds)
            lower = emission;
        else
            upper = emission;
    }
    return FlightAt(0.5 * (lower + upper));
}

double BlackHole::SpacecraftLightAge(const glm::vec3& camera, double seconds) const {
    return seconds - ObservedFlight(camera, seconds).time;
}

glm::vec3 BlackHole::PlanetPosition(double seconds, int planet) {
    return glm::vec3(DiskOrientation() * glm::vec4(
        OrbitalMechanics::PositionAtTime(bodies[planet].orbit, gravitationalParameter, seconds), 1.0f));
}

int BlackHole::NearestPlanet(const glm::vec3& position, double seconds) {
    int nearest = 0;
    float distance = glm::length(position - PlanetPosition(seconds));
    for (int planet = 1; planet < 3; ++planet) {
        const float candidate = glm::length(position - PlanetPosition(seconds, planet));
        if (candidate < distance) {
            distance = candidate;
            nearest = planet;
        }
    }
    return nearest;
}

BlackHole::Target BlackHole::ResolveTarget(int id, double seconds) const {
    if (id == 0)
        return {glm::vec3(0), horizonRadius, "Black hole"};
    if (id >= 1 && id <= 8) {
        const auto& body = bodies[id - 1];
        const auto position = glm::vec3(DiskOrientation() * glm::vec4(
            OrbitalMechanics::PositionAtTime(body.orbit, gravitationalParameter, seconds), 1));
        return {position, body.radius, body.surface == Planet
            ? "Planet " + std::to_string(id) : "Orbiting star " + std::to_string(id - 3)};
    }
    if (id == 9)
        return {SpacecraftPosition(), 0.06f, "Spacecraft"};
    const int star = id - 10;
    if (star >= 0 && star < static_cast<int>(destinations.size())) {
        const auto name = star < 1600 ? "Local star " + std::to_string(star + 1)
            : "Galaxy " + std::to_string((star - 1600) / 1800 + 1)
                + " star " + std::to_string((star - 1600) % 1800 + 1);
        return {destinations[star], 0.012f, name};
    }
    return {glm::vec3(0), horizonRadius, "Black hole"};
}

int BlackHole::PickTarget(const glm::vec3& origin, const glm::vec3& direction, double seconds) const {
    int selected = -1;
    float bestScore = 1.0f;
    for (int id = 0; id < 10 + static_cast<int>(destinations.size()); ++id) {
        const auto target = ResolveTarget(id, seconds);
        const auto offset = target.position - origin;
        const float distance = glm::length(offset);
        const float along = glm::dot(offset, direction);
        if (along <= 0.0f || distance < 0.0001f)
            continue;
        const float separation = glm::length(offset - along * direction) / distance;
        const float tolerance = glm::max(0.012f, target.radius / distance);
        const float score = separation / tolerance;
        if (score < bestScore) {
            bestScore = score;
            selected = id;
        }
    }
    return selected;
}

glm::mat4 BlackHole::PlanetRotation(double seconds) {
    return glm::rotate(glm::mat4(1.0f),
        static_cast<float>(std::remainder(seconds * 0.15, glm::two_pi<double>())),
        glm::vec3(0.0f, 1.0f, 0.0f));
}

float BlackHole::PlanetSurfaceRadius(const glm::vec3& worldDirection, double seconds, int planet) const {
    const auto direction = glm::vec3(glm::transpose(PlanetRotation(seconds)) * glm::vec4(worldDirection, 0.0f));
    const float phase = static_cast<float>(std::remainder(seconds, glm::two_pi<double>()));
    const float wave = (std::sin(35.0f * direction.y + 4.0f * std::sin(10.0f * direction.x) + phase)
        + 0.35f * std::sin(80.0f * direction.y + 8.0f * direction.x - phase)) / 1.35f;
    return bodies[planet].radius * (1.0f + 0.0015f * wave);
}

void BlackHole::Render(const glm::mat4& viewProjection, const glm::vec3& cameraPosition, double seconds,
                      const glm::vec3& observerVelocity, bool trajectories, int selected) const {
    const glm::mat4 identity(1.0f);
    const auto diskTilt = DiskOrientation();
    const float diskPhase = static_cast<float>(std::remainder(
        OrbitalMechanics::MeanMotion(gravitationalParameter, outerDiskRadius) * seconds,
        glm::two_pi<double>()));
    auto observerBeta = observerVelocity / static_cast<float>(std::sqrt(2.0 * gravitationalParameter / horizonRadius));
    if (glm::length(observerBeta) > 0.99f)
        observerBeta = glm::normalize(observerBeta) * 0.99f;

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "oceanWorld"), true);
    glUniform1f(glGetUniformLocation(program, "seaLevel"), 0.035f);
    glUniform1f(glGetUniformLocation(program, "terrainRelief"), 0.1f);
    glUniform1f(glGetUniformLocation(program, "waterWaveHeight"), 0.0015f);
    glUniform3fv(glGetUniformLocation(program, "observerBeta"), 1, glm::value_ptr(observerBeta));
    glUniform1f(glGetUniformLocation(program, "terrainSeed"), static_cast<float>(terrainSeed));
    glUniform3fv(glGetUniformLocation(program, "cameraPosition"), 1, glm::value_ptr(cameraPosition));
    glUniform1f(glGetUniformLocation(program, "horizonRadius"), horizonRadius);
    const auto diskAxis = glm::vec3(diskTilt * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    glUniform1f(glGetUniformLocation(program, "wavePhase"), static_cast<float>(std::remainder(seconds, glm::two_pi<double>())));
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    const int modelLocation = glGetUniformLocation(program, "model");
    const int surfaceLocation = glGetUniformLocation(program, "surface");
    const int colorLocation = glGetUniformLocation(program, "bodyColor");

    glBindVertexArray(backgroundStars.vao);
    glUniform1i(surfaceLocation, 7);
    glUniform3f(colorLocation, 0.52f, 0.58f, 0.68f);
    glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
    glPointSize(1.5f);
    glDrawArrays(GL_POINTS, 0, backgroundStars.vertexCount);

    glUseProgram(lensProgram);
    glUniform3fv(glGetUniformLocation(lensProgram, "observerBeta"), 1, glm::value_ptr(observerBeta));
    const auto inverseViewProjection = glm::inverse(viewProjection);
    const auto inverseDiskOrientation = glm::inverse(diskTilt);
    glUniformMatrix4fv(glGetUniformLocation(lensProgram, "inverseViewProjection"), 1, GL_FALSE, glm::value_ptr(inverseViewProjection));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram, "inverseDiskOrientation"), 1, GL_FALSE, glm::value_ptr(inverseDiskOrientation));
    glUniform3fv(glGetUniformLocation(lensProgram, "cameraPosition"), 1, glm::value_ptr(cameraPosition));
    glUniform3fv(glGetUniformLocation(lensProgram, "diskAxis"), 1, glm::value_ptr(diskAxis));
    glUniform1f(glGetUniformLocation(lensProgram, "horizonRadius"), horizonRadius);
    glUniform1f(glGetUniformLocation(lensProgram, "diskPhase"), diskPhase);
    glUniform2f(glGetUniformLocation(lensProgram, "diskRadii"), innerDiskRadius, outerDiskRadius);
    glBindVertexArray(screenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glUseProgram(program);
    glBindVertexArray(sphere.vao);
    const auto drawSphere = [&](const glm::vec3& center, float radius, Surface surface, const glm::vec3& color) {
        auto model = glm::translate(identity, center);
        if (surface == Planet)
            model *= PlanetRotation(seconds);
        model = glm::scale(model, glm::vec3(radius));
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(surfaceLocation, surface);
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
    };
    int bodyIndex = 0;
    for (const auto& body : bodies) {
        glUniform1f(glGetUniformLocation(program, "terrainSeed"), static_cast<float>((terrainSeed + bodyIndex * 137) % 10000));
        const auto orbitalPosition = OrbitalMechanics::PositionAtTime(body.orbit, gravitationalParameter, seconds);
        const auto worldPosition = glm::vec3(diskTilt * glm::vec4(orbitalPosition, 1.0f));
        const auto velocity = glm::vec3(diskTilt * glm::vec4(
            OrbitalMechanics::VelocityAtTime(body.orbit, gravitationalParameter, seconds), 0.0f));
        const auto beta = velocity / static_cast<float>(std::sqrt(2.0 * gravitationalParameter / horizonRadius));
        glUniform3fv(glGetUniformLocation(program, "velocityBeta"), 1, glm::value_ptr(beta));
        glUniform1f(glGetUniformLocation(program, "temperature"), body.temperature);
        drawSphere(worldPosition, body.radius, body.surface, body.color);
        ++bodyIndex;
    }
    glUniform3f(glGetUniformLocation(program, "velocityBeta"), 0, 0, 0);
    for (int star = 0; star < static_cast<int>(destinations.size()); ++star) {
        if (glm::length(cameraPosition - destinations[star]) < 3.0f) {
            glUniform1f(glGetUniformLocation(program, "temperature"), 3500.0f + (star * 7919 % 6500));
            drawSphere(destinations[star], 0.012f, Star, glm::vec3(1));
        }
    }
    const auto observed = ObservedFlight(cameraPosition, seconds);
    if (!observed.captured) {
        glUniform1i(surfaceLocation, 4);
        glUniform3f(colorLocation, 0.22f, 0.65f, 0.82f);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(craftTrail.vao);
        if (trajectories) glDrawArrays(GL_LINE_STRIP, 0, static_cast<int>(trail.size()));

        // Attitude guidance keeps the ring axis radial while the engine thrusts prograde.
        auto craftModel = glm::translate(identity, glm::vec3(observed.state.position))
            * AttitudeAlong(glm::vec3(observed.state.position));
        craftModel = glm::rotate(craftModel, static_cast<float>(std::remainder(observed.time * 0.4, glm::two_pi<double>())),
            glm::vec3(0.0f, 0.0f, 1.0f));
        glUniform1i(surfaceLocation, 5);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(craftModel));
        glUniform3f(colorLocation, 0.75f, 0.80f, 0.84f);
        glBindVertexArray(craftRing.vao);
        glDrawArrays(GL_TRIANGLES, 0, craftRing.vertexCount);
        glBindVertexArray(sphere.vao);
        for (int module = 0; module < 12; ++module) {
            const float angle = glm::two_pi<float>() * module / 12.0f;
            auto model = glm::translate(craftModel, glm::vec3(0.045f * std::cos(angle), 0.045f * std::sin(angle), 0.0f));
            model = glm::scale(model, glm::vec3(0.008f, 0.008f, 0.012f));
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
        for (int spoke = 0; spoke < 4; ++spoke) {
            auto model = glm::rotate(craftModel, glm::half_pi<float>() * spoke, glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::translate(model, glm::vec3(0.022f, 0.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.023f, 0.002f, 0.002f));
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
        if (observed.firing) {
            glUniform1i(surfaceLocation, 6);
            const auto direction = glm::normalize(glm::vec3(observed.state.velocity));
            auto exhaust = glm::translate(identity, glm::vec3(observed.state.position) - 0.075f * direction)
                * AttitudeAlong(direction);
            exhaust = glm::scale(exhaust, glm::vec3(0.018f, 0.018f, 0.075f));
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(exhaust));
            glUniform3f(colorLocation, 0.25f, 0.65f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
    }
    glBindVertexArray(0);
    galaxies.Render(viewProjection);
    if (trajectories && guides.vao) {
        glUseProgram(program);
        glUniform1i(surfaceLocation,4);
        glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(identity));
        glUniform3f(colorLocation,.06f,.18f,.26f);
        glBindVertexArray(guides.vao);
        glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
        for (const auto& body:bodies) {
            std::vector<glm::vec3> vertices;
            const double period=glm::two_pi<double>()/OrbitalMechanics::MeanMotion(gravitationalParameter,body.orbit.semiMajorAxis);
            for (int point=0;point<=256;++point)
                vertices.push_back(glm::vec3(diskTilt*glm::vec4(OrbitalMechanics::PositionAtTime(body.orbit,gravitationalParameter,period*point/256),1)));
            glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINE_STRIP,0,static_cast<int>(vertices.size()));
        }
        glBindVertexArray(0);
    }
    for (int planet = 0; planet < 3; ++planet)
        atmosphere.Render(viewProjection, cameraPosition, PlanetPosition(seconds, planet), bodies[planet].radius);
    if (photonView) {
        packetCache.clear();
        packetCache.reserve((destinations.size() + 20) * 4 + 1);
        const auto appendStream = [&](const glm::vec3& emitter, const glm::vec3& color, bool historical) {
            const double delay = glm::max(0.001, OrbitalMechanics::LightTravelDelay(
                glm::dvec3(emitter), glm::dvec3(photonDetector), gravitationalParameter, horizonRadius));
            for (int packet = 0; packet < 4; ++packet) {
                const double progress = std::fmod(seconds / delay + packet / 4.0, 1.0);
                const auto source = historical ? glm::vec3(FlightAt(seconds - progress * delay).state.position) : emitter;
                packetCache.push_back({glm::mix(source, photonDetector, static_cast<float>(progress)), color});
            }
        };
        for (int point = 0; point < 8; ++point) {
            const float angle = glm::two_pi<float>() * point / 8.0f;
            appendStream(glm::vec3(DiskOrientation() * glm::vec4(
                outerDiskRadius * std::cos(angle), outerDiskRadius * std::sin(angle), 0, 1)),
                glm::vec3(1.0f, 0.7f, 0.2f), false);
        }
        for (int id = 1; id <= 8; ++id)
            appendStream(ResolveTarget(id, seconds).position,
                id <= 3 ? glm::vec3(0.2f, 0.65f, 1.0f) : glm::vec3(1.0f, 0.9f, 0.65f), false);
        appendStream(glm::vec3(spacecraft.position), glm::vec3(0.2f, 0.9f, 1.0f), true);
        for (const auto& star : destinations)
            appendStream(star, glm::vec3(1.0f, 0.85f, 0.55f), false);
        for (const auto& group : GalaxyBackground::Groups())
            appendStream(group.center, glm::vec3(0.7f, 0.55f, 1.0f), false);
        packetCache.push_back({photonDetector, glm::vec3(0.4f, 1.0f, 0.5f)});
        glUseProgram(program);
        glUniform1i(surfaceLocation, 8);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identity));
        glBindVertexArray(photonPackets.vao);
        glBindBuffer(GL_ARRAY_BUFFER, photonPackets.buffer);
        glBufferData(GL_ARRAY_BUFFER, packetCache.size() * sizeof(PhotonPacket), packetCache.data(), GL_DYNAMIC_DRAW);
        glPointSize(3.0f);
        glDrawArrays(GL_POINTS, 0, static_cast<int>(packetCache.size() - 1));
        glPointSize(9.0f);
        glDrawArrays(GL_POINTS, static_cast<int>(packetCache.size() - 1), 1);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (selected>=0 && guides.vao) {
        const auto target=ResolveTarget(selected,seconds).position;
        const auto delta=target-cameraPosition;
        const float distance=glm::length(delta);
        if (distance>.00001f) {
            const auto forward=delta/distance;
            const auto reference=std::abs(forward.y)<.95f?glm::vec3(0,1,0):glm::vec3(1,0,0);
            const auto right=glm::normalize(glm::cross(forward,reference));
            const auto up=glm::cross(right,forward);
            std::vector<glm::vec3> vertices;
            for (const auto axis:{right,up}) for (int sign:{-1,1}) {
                vertices.push_back(target+axis*(sign*distance*.005f));
                vertices.push_back(target+axis*(sign*distance*.014f));
            }
            glUseProgram(program);
            glUniform1i(surfaceLocation,4);
            glUniform3f(colorLocation,.15f,1,.65f);
            glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(identity));
            glDisable(GL_DEPTH_TEST);
            glBindVertexArray(guides.vao); glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
            glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINES,0,static_cast<int>(vertices.size()));
            glBindVertexArray(0); glEnable(GL_DEPTH_TEST);
        }
    }
}

void BlackHole::Release() {
    atmosphere.Release();
    galaxies.Release();
    for (auto* mesh : {&sphere, &backgroundStars, &craftRing, &craftTrail, &photonPackets, &guides}) {
        if (mesh->vao)
            glDeleteVertexArrays(1, &mesh->vao);
        if (mesh->buffer)
            glDeleteBuffers(1, &mesh->buffer);
        *mesh = {};
    }
    if (program) {
        glDeleteProgram(program);
        program = 0;
    }
    if (lensProgram) {
        glDeleteProgram(lensProgram);
        lensProgram = 0;
    }
    if (screenVao) {
        glDeleteVertexArrays(1, &screenVao);
        screenVao = 0;
    }
}
