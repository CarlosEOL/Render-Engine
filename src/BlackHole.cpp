#include "glad/glad.h"
#include "BlackHole.h"
#include "OrbitalMechanics.h"
#include "PlanetTerrain.h"
#include "StarSystem.h"

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
    constexpr double gravitationalParameter = Astronomy::gargantuaGravityAu;
    constexpr float horizonRadius = static_cast<float>(Astronomy::gargantuaHorizonAu);
    constexpr float innerDiskRadius = 3.0f * horizonRadius;
    constexpr float outerDiskRadius = horizonRadius * (1.03f/.24f);
    constexpr double distanceScale = Astronomy::gargantuaHorizonAu / .24;
    const double timeScale = std::sqrt(distanceScale*distanceScale*distanceScale*1.8/gravitationalParameter);
    const OrbitalMechanics::Orbit spacecraftOrbit{1.7*distanceScale, 0.46, 0.35, 0.9, glm::pi<double>()};
    const double boosterAcceleration = .85*distanceScale/(timeScale*timeScale);
    constexpr float craftScale = static_cast<float>(25.0/Astronomy::metresPerAu/.045);

    enum Surface { Planet = 2, Star = 3 };

    struct Body {
        OrbitalMechanics::Orbit orbit;
        float radius;
        glm::vec3 color;
        Surface surface;
        float temperature;
    };

    const Body bodies[] = {
        {{Astronomy::millerOrbitAu, 0.0, 0.20, 0.2, 0.8}, static_cast<float>(Astronomy::millerRadiusAu), {0.22f, 0.60f, 1.0f}, Planet, 0.0f},
        {{8.0*horizonRadius, 0.03, 0.32, 1.7, 2.4}, static_cast<float>(Astronomy::earthRadiusAu*.85), {0.35f, 0.70f, 0.85f}, Planet, 0.0f},
        {{14.0*horizonRadius, 0.04, 0.55, 0.7, 4.8}, static_cast<float>(Astronomy::earthRadiusAu*1.12), {0.25f, 0.55f, 0.90f}, Planet, 0.0f},
        {{50, 0.03, 0.45, 0.1, 2.2}, static_cast<float>(Astronomy::solarRadiusAu), {1.0f, 0.77f, 0.45f}, Star, 5200.0f},
        {{80, 0.12, 0.83, 1.4, 4.5}, static_cast<float>(Astronomy::solarRadiusAu*2), {0.64f, 0.80f, 1.0f}, Star, 10000.0f},
        {{120, 0.15, 0.72, 2.3, 0.4}, static_cast<float>(Astronomy::solarRadiusAu*.65), {1.0f, 0.52f, 0.36f}, Star, 3500.0f},
        {{180, 0.11, 1.10, 0.3, 3.4}, static_cast<float>(Astronomy::solarRadiusAu*1.2), {1.0f, 0.92f, 0.77f}, Star, 6200.0f},
        {{250, 0.13, 0.57, 2.8, 5.7}, static_cast<float>(Astronomy::solarRadiusAu*1.8), {0.68f, 0.81f, 1.0f}, Star, 8500.0f},
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
layout (location = 2) in float stellarTemperature;
layout (location = 3) in float stellarLuminosity;
uniform mat4 model;
uniform bool patchEnabled;
uniform bool patchDrawing;
uniform vec3 patchCenter;
uniform mat4 viewProjection;
uniform float wavePhase;
uniform int surface;
uniform bool oceanWorld;
out vec3 localPosition;
out vec3 worldPosition;
out vec3 relativePosition;
uniform vec3 cameraPosition;
out vec3 photonColor;
out float sourceTemperature;
out float sourceLuminosity;

void main() {
    vec3 displaced = position;
    if (surface == 2) {
        displaced = oceanWorld ? position * (1.0 + waterWaveHeight * oceanWave(position, wavePhase))
            : planetSurface(position, terrainHeight(position), wavePhase);
    }
    vec4 world = model * vec4(displaced, 1.0);
    localPosition = position;
    relativePosition = world.xyz;
    worldPosition = world.xyz + cameraPosition;
    photonColor = packetColor;
    sourceTemperature=stellarTemperature;
    sourceLuminosity=stellarLuminosity;
    gl_Position = viewProjection * world;
    if(surface==7)gl_Position.z=gl_Position.w*.999999;
}
)";

    const char* fragmentBody = R"(
in vec3 localPosition;
in vec3 worldPosition;
in vec3 relativePosition;
in vec3 photonColor;
in float sourceTemperature;
in float sourceLuminosity;
out vec4 fragColor;
uniform int surface;
uniform bool oceanWorld;
uniform vec3 bodyColor;
uniform vec3 cameraPosition;
uniform vec3 velocityBeta;
uniform float temperature;
uniform float wavePhase;
uniform mat4 model;
uniform bool patchEnabled;
uniform bool patchDrawing;
uniform vec3 patchCenter;

void main() {
    gl_FragDepth=surface==7?0.999999:clamp(log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12),0.0,.999998);
    if (surface == 2) {
        vec3 radial = normalize(localPosition);
        if(patchEnabled && ((dot(radial,patchCenter)>cos(.020))!=patchDrawing))discard;
        float height = oceanWorld ? seaLevel - 0.3 : terrainHeight(radial);
        float water = oceanWorld ? 1.0 : 1.0 - smoothstep(seaLevel - 0.008, seaLevel + 0.008, height);
        float depth = max(seaLevel - height, 0.0);
        vec3 surfacePosition = oceanWorld ? radial * (1.0 + waterWaveHeight * oceanWave(radial, wavePhase))
            : planetSurface(radial, height, wavePhase);
        vec3 localNormal = oceanWorld?radial:normalize(cross(dFdx(surfacePosition), dFdy(surfacePosition)));
        if(oceanWorld) {
            localNormal=radial;
            vec3 reference=abs(radial.y)<.9?vec3(0,1,0):vec3(1,0,0);
            vec3 tangent=normalize(cross(reference,radial)),bitangent=cross(radial,tangent);
            vec2 ripple=vec2(dot(radial,vec3(1,.31,.17)),dot(radial,vec3(.19,1,.47)))*500000.0;
            float fade=1.0-smoothstep(.25,1.0,max(fwidth(ripple.x),fwidth(ripple.y)));
            localNormal=normalize(localNormal+.018*fade*(tangent*sin(ripple.x+wavePhase)+bitangent*cos(ripple.y-wavePhase*.7)));
        }
        if (dot(localNormal, radial) < 0.0)
            localNormal = -localNormal;
        vec3 normal = normalize(mat3(transpose(inverse(model))) * localNormal);
        vec3 lightDirection = normalize(-worldPosition);
        vec3 viewDirection = normalize(-relativePosition);
        float light = max(dot(normal, lightDirection), 0.0);
        float specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), 100.0) * water;
        if(oceanWorld) {
            float fresnel=.02+.98*pow(1.0-max(dot(normal,viewDirection),0.0),5.0);
            vec3 ocean=mix(vec3(.006,.025,.065),vec3(.045,.10,.15),fresnel);
            fragColor=vec4(ocean*(.03+.97*light)+specular*.65,1);
            return;
        }
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
            velocityBeta, normalize(-relativePosition));
        fragColor = vec4(blackbody(temperature * shift), 1.0);
    } else if (surface == 8) {
        fragColor = vec4(photonColor, 1.0);
    } else if (surface == 7) {
        float seed = fract(sin(dot(worldPosition, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        float distanceSquared = dot(relativePosition,relativePosition);
        vec2 psf=gl_PointCoord*2.0-1.0;
        float pointRadius=dot(psf,psf);
        if(pointRadius>1.0)discard;
        float flux = max(sourceLuminosity,.001) * 1.0e9 / max(distanceSquared, 1.0)*exp(-pointRadius*2.0);
        vec3 tint = blackbody(max(sourceTemperature,2500.0));
        tint/=max(max(tint.r,tint.g),max(tint.b,.00001));
        fragColor = vec4(tint * flux, 1.0);
    } else if (surface == 5) {
        vec3 normal = normalize(cross(dFdx(relativePosition), dFdy(relativePosition)));
        vec3 viewDirection = normalize(-relativePosition);
        if (dot(normal, viewDirection) < 0.0)
            normal = -normal;
        vec3 lightDirection = normalize(vec3(1.0, 1.0, 2.0) - worldPosition);
        float diffuse = max(dot(normal, lightDirection), 0.0);
        float specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), 24.0);
        fragColor = vec4(bodyColor * (0.22 + 0.78 * diffuse) + 0.5 * specular, 1.0);
    } else if (surface == 6) {
        vec3 normal = normalize(mat3(transpose(inverse(model))) * localPosition);
        float facing = max(dot(normal, normalize(-relativePosition)), 0.0);
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
uniform float plasmaTime;
uniform sampler2D sourceScene;
uniform sampler2D sourceDepth;
uniform samplerCube sky;

float depthDistance(float depth) {
    return (exp2(depth*log2(1.0+20000.0*1e12))-1.0)/1e12;
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(cell + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(cell + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(cell + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(cell + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(cell + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(cell + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(cell + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(cell + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.55;
    for (int octave = 0; octave < 4; ++octave) {
        value += amplitude * valueNoise(p);
        p = p * 2.07 + vec3(11.3, 7.7, 5.1);
        amplitude *= 0.48;
    }
    return value;
}

void main() {
    vec4 farPoint = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 cameraDirection = normalize(farPoint.xyz);
    vec3 direction = observerRayDirection(cameraDirection);
    vec2 originalUv=screenPosition*.5+.5;
    float originalDepth=texture(sourceDepth,originalUv).r;
    if(originalDepth<.99999 && depthDistance(originalDepth)<max(0.0,length(cameraPosition)-3.0*horizonRadius)) {
        fragColor=texture(sourceScene,originalUv);gl_FragDepth=originalDepth;return;
    }
    float observerRadius = length(cameraPosition);
    bool insideHorizon = observerRadius < horizonRadius;
    // Rays whose impact parameters exceed the disk's apparent extent miss the scene.
    if (dot(cameraPosition, direction) > 0.0 && observerRadius > diskRadii.y*1.5)
        discard;
    if (length(cross(cameraPosition, direction)) > diskRadii.y*1.5)
        discard;

    vec3 position = cameraPosition;
    vec3 tangent = photonInitialTangent(position, direction);
    vec3 momentum = cross(position, tangent);
    float angularMomentumSquared = dot(momentum, momentum);
    float travelled = 0.0;
    bool hit = false;
    vec3 radiance = vec3(0.0);
    vec3 plasma = vec3(0.0);

    for (int step = 0; step < 360; ++step) {
        float radius = length(position);
        if (!insideHorizon && radius < horizonRadius * 1.015) {
            hit = true;
            break;
        }
        if (insideHorizon && radius < horizonRadius * 0.012) {
            hit = true;
            break;
        }
        if (radius > max(observerRadius, 3.0) + 4.0 && dot(position, tangent) > 0.0)
            break;

        float interval = clamp(radius * 0.055, 0.003, 0.45);
        float diskFraction = (radius - diskRadii.x) / (diskRadii.y - diskRadii.x);
        float planeDistance = abs(dot(position, diskAxis));
        // Resolve the vertical plasma structure with fine steps through the slab.
        if (diskFraction > -0.08 && diskFraction < 1.12)
            interval = min(interval, max(0.0025, planeDistance * 0.35 + 0.0025));

        vec3 acceleration = photonAcceleration(position, angularMomentumSquared);
        vec3 nextPosition = position + interval * tangent + 0.5 * interval * interval * acceleration;
        vec3 nextTangent = tangent + 0.5 * interval
            * (acceleration + photonAcceleration(nextPosition, angularMomentumSquared));

        vec3 segment = nextPosition - position;
        float segmentLength = length(segment);
        vec3 midpoint = position + 0.5 * segment;
        float midRadius = length(midpoint);
        float midFraction = (midRadius - diskRadii.x) / (diskRadii.y - diskRadii.x);
        float midPlane = dot(midpoint, diskAxis);

        // Volumetric plasma clouds above and below the thin disk.
        if (midFraction > 0.0 && midFraction < 1.0) {
            float flare = 0.012 + 0.065 * pow(midFraction, 1.35);
            float vertical = exp(-0.5 * midPlane * midPlane / (flare * flare));
            if (vertical > 0.02) {
                vec3 diskPosition = (inverseDiskOrientation * vec4(midpoint, 1.0)).xyz;
                float cloudAngle = diskPhase * 0.35;
                float cloudCos = cos(cloudAngle);
                float cloudSin = sin(cloudAngle);
                vec2 rotated = vec2(cloudCos * diskPosition.x - cloudSin * diskPosition.y,
                                    cloudSin * diskPosition.x + cloudCos * diskPosition.y);
                vec3 cloudPosition = vec3(rotated * 7.5, diskPosition.z * 26.0)
                    + vec3(0.0, 0.0, plasmaTime * 0.3);
                float turbulence = fbm(cloudPosition);
                float clumps = smoothstep(0.30, 0.78, turbulence);
                float density = vertical * (0.20 + 1.8 * clumps);
                vec3 toObserver = -localPhotonDirection(midpoint, tangent);
                float shift = clamp(frequencyRatio(midRadius, observerRadius,
                    circularVelocityBeta(midpoint, diskAxis), toObserver), 0.05, 6.0);
                float temperature = 5200.0 * pow(max(midRadius / diskRadii.x, 1.0), -0.75);
                float beaming = pow(shift, 3.2);
                plasma += blackbody(temperature * shift) * beaming * density * segmentLength * 0.55;
            }
        }

        float planeBefore = dot(position, diskAxis);
        float planeAfter = dot(nextPosition, diskAxis);
        if (planeBefore * planeAfter < 0.0) {
            float fraction = planeBefore / (planeBefore - planeAfter);
            vec3 intersection = mix(position, nextPosition, fraction);
            float diskRadius = length(intersection);
            if (diskRadius > diskRadii.x && diskRadius < diskRadii.y) {
                vec3 diskPosition = (inverseDiskOrientation * vec4(intersection, 1.0)).xyz;
                float radial = (diskRadius - diskRadii.x) / (diskRadii.y - diskRadii.x);
                float angle = atan(diskPosition.y, diskPosition.x) - diskPhase;
                float bands = 0.6 + 0.4 * sin(120.0 * radial + 2.2 * sin(4.0 * angle));
                float fineBands = 0.72 + 0.28 * sin(260.0 * radial + 4.0 * sin(6.0 * angle));
                float spiral = 0.6 + 0.4 * cos(9.0 * angle + 34.0 * radial);
                vec3 toObserver = -localPhotonDirection(intersection, mix(tangent, nextTangent, fraction));
                float shift = clamp(frequencyRatio(diskRadius, observerRadius,
                    circularVelocityBeta(intersection, diskAxis), toObserver), 0.05, 6.0);
                float temperature = 5200.0 * pow(diskRadius / diskRadii.x, -0.75);
                float edge = smoothstep(0.0, 0.06, radial) * (1.0 - smoothstep(0.82, 1.0, radial));
                float beaming = pow(shift, 3.6);
                radiance = 3.2 * blackbody(temperature * shift) * beaming * edge
                    * (0.4 + 0.3 * bands + 0.3 * spiral) * fineBands;
                travelled += length(intersection - position);
                hit = true;
                break;
            }
        }
        travelled += segmentLength;
        position = nextPosition;
        tangent = nextTangent;
    }
    if(!hit) {
        vec3 incoming=normalize(tangent);
        radiance=texture(sky,incoming).rgb;
        vec4 mapped=viewProjection*vec4(incoming,0);
        if(mapped.w>0) {
            vec2 mappedUv=mapped.xy/mapped.w*.5+.5;
            if(min(mappedUv.x,mappedUv.y)>0 && max(mappedUv.x,mappedUv.y)<1) {
                float mappedDepth=texture(sourceDepth,mappedUv).r;
                if(mappedDepth>=.99999 || depthDistance(mappedDepth)>max(0.0,length(cameraPosition)-3.0*horizonRadius))
                    radiance=texture(sourceScene,mappedUv).rgb;
            }
        }
    } else if(originalDepth<.99999 && depthDistance(originalDepth)<travelled) {
        fragColor=texture(sourceScene,originalUv);gl_FragDepth=originalDepth;return;
    }
    fragColor = vec4(radiance + plasma, 1.0);
    // Approximate depth allows the rasterized orbiting bodies to cross the lensed disk.
    gl_FragDepth = hit?clamp(log2(1.0+travelled*1e12)/log2(1.0+20000.0*1e12),0.0,.999998):.999999;
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
    glGenVertexArrays(1,&waterPatch.vao);glGenBuffers(1,&waterPatch.buffer);
    glBindVertexArray(waterPatch.vao);glBindBuffer(GL_ARRAY_BUFFER,waterPatch.buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(glm::vec3),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);glEnableVertexAttribArray(0);
    std::mt19937 random(618);
    std::uniform_real_distribution<float> component(-1.0f, 1.0f);
    std::vector<glm::vec3> stars;
    while (stars.size() < 1600) {
        const glm::vec3 direction(component(random), component(random), component(random));
        const float squaredRadius = glm::dot(direction, direction);
        if (squaredRadius > 0.01f && squaredRadius <= 1.0f)
            stars.push_back(glm::normalize(direction) * static_cast<float>(45.0*Astronomy::auPerLightYear));
    }
    upload(backgroundStars, stars);
    // Each cluster has a spiral disk, a finite thickness, and a compact bulge.
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::normal_distribution<float> elliptical(0.0f,.24f);
    for (int galaxy = 0; galaxy < 3; ++galaxy) {
        const auto& group = GalaxyBackground::Groups()[galaxy];
        const float diskRadius = group.radius * GalaxyBackground::StarDiskRadiusFraction;
        for (int star = 0; star < 1800; ++star) {
            const float radius = diskRadius * std::sqrt(unit(random));
            const float arm = glm::two_pi<float>() * (star % 3) / 3.0f;
            const float angle = arm + radius / diskRadius * 6.0f + (unit(random) - 0.5f) * 0.65f;
            const float thickness = (unit(random) - 0.5f)
                * (0.4f + 1.2f * std::exp(-radius / diskRadius)) * (group.radius / 12.2f);
            auto local = glm::vec3(radius * std::cos(angle), thickness, radius * std::sin(angle));
            if(galaxy==1 && star%5==0) {
                const float u=local.x,v=local.z*.12f;
                local.x=u*std::cos(.8f)-v*std::sin(.8f);
                local.z=u*std::sin(.8f)+v*std::cos(.8f);
            }else if(galaxy==2) {
                local={elliptical(random)*diskRadius,elliptical(random)*diskRadius*.75f,elliptical(random)*diskRadius};
            }
            const auto tilt = glm::rotate(glm::mat4(1), group.tilt, glm::vec3(1, 0, 0));
            stars.push_back(group.center + glm::vec3(tilt * glm::vec4(local, 0)));
        }
    }
    // Reuse the uploaded star buffer for the complete navigable catalogue.
    glBindBuffer(GL_ARRAY_BUFFER, backgroundStars.buffer);
    glBufferData(GL_ARRAY_BUFFER, stars.size() * sizeof(glm::vec3), stars.data(), GL_STATIC_DRAW);
    backgroundStars.vertexCount = static_cast<int>(stars.size());
    destinations = std::move(stars);
    std::vector<glm::vec2> properties;properties.reserve(destinations.size());
    for(int star=0;star<static_cast<int>(destinations.size());++star) {
        const auto metadata=StarSystem::DescribeStar(star+10,"");
        properties.push_back({static_cast<float>(metadata.temperature),static_cast<float>(metadata.luminositySolar)});
    }
    glGenBuffers(1,&stellarProperties);glBindBuffer(GL_ARRAY_BUFFER,stellarProperties);
    glBufferData(GL_ARRAY_BUFFER,properties.size()*sizeof(glm::vec2),properties.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),reinterpret_cast<void*>(sizeof(float)));glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ResetSpacecraft(0.0);
    if (!atmosphere.Initialize()) {
        Release();
        return false;
    }
    galaxies.Initialize();
    glGenTextures(1,&skyCube);
    glGenTextures(1,&sourceColor);
    glGenTextures(1,&sourceDepth);
    BuildSkyCube();
    return true;
}

void BlackHole::ResetSpacecraft(double seconds) {
    spacecraft.position = glm::dvec3(DiskOrientation() * glm::vec4(
        OrbitalMechanics::PositionAtTime(spacecraftOrbit, gravitationalParameter, 0.0), 1.0f));
    spacecraft.velocity = glm::dvec3(DiskOrientation() * glm::vec4(
        OrbitalMechanics::VelocityAtTime(spacecraftOrbit, gravitationalParameter, 0.0), 0.0f));
    spacecraftTime = seconds;
    fuelSeconds = 1.2*timeScale;
    boosterFiring = false;
    spacecraftCaptured = false;
    trailSampleTime = seconds;
    trail.assign(1, spacecraft.position);
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
        if (radius < 1.05*distanceScale && glm::dot(spacecraft.position, spacecraft.velocity) < 0.0)
            BoostSpacecraft();

        double interval = glm::min(seconds - spacecraftTime,
            glm::min(10.0*timeScale, 0.01 * std::sqrt(radius * radius * radius / gravitationalParameter)));
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
        const double sampleInterval = glm::max(0.02*timeScale, (spacecraftTime - missionStart) * 0.01);
        if (spacecraftTime - flightHistory.back().time >= sampleInterval || thrust > 0.0)
            flightHistory.push_back({spacecraftTime, spacecraft, boosterFiring, false});
        if (spacecraftTime - trailSampleTime >= 0.04*timeScale) {
            if (trail.size() == 2048)
                trail.erase(trail.begin(), trail.begin() + 512);
            trail.push_back(spacecraft.position);
            trailSampleTime = spacecraftTime;
        }
    }
}

BlackHole::FlightSample BlackHole::FlightAt(double seconds) const {
    if(seconds<=spacecraftTime && seconds>=spacecraftTime-1.0) {
        const double dt=spacecraftTime-seconds,radius=glm::length(spacecraft.position);
        auto acceleration=-gravitationalParameter*spacecraft.position/(radius*radius*radius);
        if(boosterFiring && glm::length(spacecraft.velocity)>0)acceleration+=boosterAcceleration*glm::normalize(spacecraft.velocity);
        return {seconds,{spacecraft.position-spacecraft.velocity*dt+.5*acceleration*dt*dt,
            spacecraft.velocity-acceleration*dt},boosterFiring,spacecraftCaptured};
    }
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

BlackHole::FlightSample BlackHole::ObservedFlight(const glm::dvec3& camera, double seconds) const {
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

double BlackHole::SpacecraftLightAge(const glm::dvec3& camera, double seconds) const {
    return seconds - ObservedFlight(camera, seconds).time;
}

glm::dvec3 BlackHole::PlanetPosition(double seconds, int planet) {
    return glm::dmat3(DiskOrientation())*OrbitalMechanics::PositionAtTimeDouble(bodies[planet].orbit,gravitationalParameter,seconds);
}

int BlackHole::NearestPlanet(const glm::dvec3& position, double seconds) {
    int nearest = 0;
    double distance = glm::length(position - PlanetPosition(seconds));
    for (int planet = 1; planet < 3; ++planet) {
        const double candidate = glm::length(position - PlanetPosition(seconds, planet));
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
        const auto position=glm::dmat3(DiskOrientation())*OrbitalMechanics::PositionAtTimeDouble(body.orbit,gravitationalParameter,seconds);
        return {position, body.radius, body.surface == Planet
            ? "Planet " + std::to_string(id) : "Orbiting star " + std::to_string(id - 3)};
    }
    if (id == 9)
        return {SpacecraftPosition(), static_cast<float>(50.0/Astronomy::metresPerAu), "Spacecraft"};
    const int star = id - 10;
    if (star >= 0 && star < static_cast<int>(destinations.size())) {
        const auto name = star < 1600 ? "Local star " + std::to_string(star + 1)
            : "Galaxy " + std::to_string((star - 1600) / 1800 + 1)
                + " star " + std::to_string((star - 1600) % 1800 + 1);
        return {destinations[star], 0.012f, name};
    }
    return {glm::vec3(0), horizonRadius, "Black hole"};
}

glm::mat3 BlackHole::TargetRotation(int id,double seconds) const {
    if (id>=1 && id<=3) return glm::mat3(PlanetRotation(seconds));
    if (id==9) {
        const auto flight=FlightAt(seconds);
        return glm::mat3(AttitudeAlong(glm::vec3(flight.state.position))
            *glm::rotate(glm::mat4(1),static_cast<float>(std::remainder(flight.time*.4,glm::two_pi<double>())),glm::vec3(0,0,1)));
    }
    if (id>=4) return glm::mat3(glm::rotate(glm::mat4(1),
        static_cast<float>(std::remainder(seconds*.08,glm::two_pi<double>())),glm::vec3(0,1,0)));
    return glm::mat3(1); // Schwarzschild's central object has no intrinsic spin.
}

int BlackHole::PickTarget(const glm::dvec3& origin, const glm::vec3& direction, double seconds) const {
    int selected = -1;
    float bestScore = 1.0f;
    for (int id = 0; id < 10 + static_cast<int>(destinations.size()); ++id) {
        const auto target = ResolveTarget(id, seconds);
        const auto offset = target.position - origin;
        const double distance = glm::length(offset);
        const double along = glm::dot(offset, glm::dvec3(direction));
        if (along <= 0.0 || distance < 1e-14)
            continue;
        const float separation = static_cast<float>(glm::length(offset - along * glm::dvec3(direction)) / distance);
        const float tolerance = glm::max(0.012f, static_cast<float>(target.radius / distance));
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
        static_cast<float>(std::remainder(seconds/Astronomy::secondsPerDay,1.0)*glm::two_pi<double>()),
        glm::vec3(0.0f, 1.0f, 0.0f));
}

float BlackHole::PlanetSurfaceRadius(const glm::vec3& worldDirection, double seconds, int planet) const {
    const auto direction = glm::vec3(glm::transpose(PlanetRotation(seconds)) * glm::vec4(worldDirection, 0.0f));
    const float phase = static_cast<float>(std::remainder(seconds, glm::two_pi<double>()));
    const float wave = (std::sin(35.0f * direction.y + 4.0f * std::sin(10.0f * direction.x) + phase)
        + 0.35f * std::sin(80.0f * direction.y + 8.0f * direction.x - phase)) / 1.35f;
    return bodies[planet].radius + static_cast<float>(2.0/Astronomy::metresPerAu)*wave;
}

void BlackHole::Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition, double seconds,
                      const glm::vec3& observerVelocity, bool trajectories, int selected) const {
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const glm::mat4 identity(1.0f);
    const glm::vec3 cameraFloat(cameraPosition);
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
    glUniform3fv(glGetUniformLocation(program, "cameraPosition"), 1, glm::value_ptr(cameraFloat));
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
    const auto backgroundModel=glm::translate(identity,-cameraFloat);
    glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(backgroundModel));
    glPointSize(1.5f);
    glDrawArrays(GL_POINTS, 0, backgroundStars.vertexCount);

    // Background galaxies are drawn before the lens pass so the black hole's
    // shadow and disk correctly occlude them instead of showing the PNG on top.
    glBindVertexArray(0);
    galaxies.Render(viewProjection, cameraPosition);

    glUseProgram(program);
    glBindVertexArray(sphere.vao);
    const auto drawSphere = [&](const glm::dvec3& center, float radius, Surface surface, const glm::vec3& color) {
        auto model = glm::translate(identity, glm::vec3(center-cameraPosition));
        if (surface == Planet)
            model *= PlanetRotation(seconds);
        else if (surface == Star)
            model *= glm::mat4(TargetRotation(4,seconds));
        model = glm::scale(model, glm::vec3(radius));
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(surfaceLocation, surface);
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        const bool patch=surface==Planet && glm::length(cameraPosition-center)<radius*1.15;
        glUniform1i(glGetUniformLocation(program,"patchEnabled"),patch);
        glUniform1i(glGetUniformLocation(program,"patchDrawing"),false);
        if(patch) {
            const auto direction=glm::normalize(glm::mat3(glm::transpose(PlanetRotation(seconds)))*glm::vec3(cameraPosition-center));
            if(!waterPatch.vertexCount || glm::length(direction-waterPatchDirection)>.001f) {
                waterPatchDirection=direction;
                const auto vertices=PlanetTerrain::SpherePatch(direction);
                waterPatch.vertexCount=static_cast<int>(vertices.size());
                glBindBuffer(GL_ARRAY_BUFFER,waterPatch.buffer);
                glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
            }
            glUniform3fv(glGetUniformLocation(program,"patchCenter"),1,glm::value_ptr(waterPatchDirection));
        }
        glBindVertexArray(sphere.vao);
        glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        if(patch) {
            glUniform1i(glGetUniformLocation(program,"patchDrawing"),true);
            glBindVertexArray(waterPatch.vao);glDrawArrays(GL_TRIANGLES,0,waterPatch.vertexCount);
            glBindVertexArray(sphere.vao);
        }
        glUniform1i(glGetUniformLocation(program,"patchEnabled"),false);
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
        glUniform1f(glGetUniformLocation(program,"waterWaveHeight"),static_cast<float>(2.0/Astronomy::metresPerAu/body.radius));
        const auto preciseCenter=glm::dmat3(diskTilt)*OrbitalMechanics::PositionAtTimeDouble(body.orbit,gravitationalParameter,seconds);
        drawSphere(preciseCenter, body.radius, body.surface, body.color);
        ++bodyIndex;
    }
    glUniform3f(glGetUniformLocation(program, "velocityBeta"), 0, 0, 0);
    for (int star = 0; star < static_cast<int>(destinations.size()); ++star) {
        if (glm::length(cameraPosition - glm::dvec3(destinations[star])) < 3.0) {
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
        std::vector<glm::vec3> relativeTrail;
        for(const auto& point:trail)relativeTrail.push_back(glm::vec3(point-cameraPosition));
        glBindBuffer(GL_ARRAY_BUFFER,craftTrail.buffer);
        glBufferData(GL_ARRAY_BUFFER,relativeTrail.size()*sizeof(glm::vec3),relativeTrail.data(),GL_DYNAMIC_DRAW);
        if (trajectories) glDrawArrays(GL_LINE_STRIP, 0, static_cast<int>(trail.size()));

        // Attitude guidance keeps the ring axis radial while the engine thrusts prograde.
        auto craftModel = glm::translate(identity, glm::vec3(observed.state.position-cameraPosition))
            * AttitudeAlong(glm::vec3(observed.state.position));
        craftModel = glm::rotate(craftModel, static_cast<float>(std::remainder(observed.time * 0.4, glm::two_pi<double>())),
            glm::vec3(0.0f, 0.0f, 1.0f));
        craftModel=glm::scale(craftModel,glm::vec3(craftScale));
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
            auto exhaust = glm::translate(identity, glm::vec3(observed.state.position-cameraPosition) - 0.075f*craftScale * direction)
                * AttitudeAlong(direction);
            exhaust = glm::scale(exhaust, glm::vec3(0.018f, 0.018f, 0.075f)*craftScale);
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(exhaust));
            glUniform3f(colorLocation, 0.25f, 0.65f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, sphere.vertexCount);
        }
    }
    glBindVertexArray(0);
    if (trajectories && guides.vao) {
        if(orbitGuides.empty()) {
            orbitGuides.resize(std::size(bodies));
            for(size_t body=0;body<std::size(bodies);++body) {
                const double period=glm::two_pi<double>()/OrbitalMechanics::MeanMotion(gravitationalParameter,bodies[body].orbit.semiMajorAxis);
                for(int point=0;point<=256;++point)orbitGuides[body].push_back(glm::dmat3(diskTilt)*OrbitalMechanics::PositionAtTimeDouble(bodies[body].orbit,gravitationalParameter,period*point/256));
            }
        }
        glUseProgram(program);
        glUniform1i(surfaceLocation,4);
        glUniformMatrix4fv(modelLocation,1,GL_FALSE,glm::value_ptr(identity));
        glUniform3f(colorLocation,.06f,.18f,.26f);
        glBindVertexArray(guides.vao);
        glBindBuffer(GL_ARRAY_BUFFER,guides.buffer);
        for (const auto& orbit:orbitGuides) {
            std::vector<glm::vec3> vertices;
            for(const auto& point:orbit)vertices.push_back(glm::vec3(point-cameraPosition));
            glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINE_STRIP,0,static_cast<int>(vertices.size()));
        }
        glBindVertexArray(0);
    }
    for (int planet = 0; planet < 3; ++planet)
        atmosphere.Render(viewProjection, glm::vec3(0), glm::vec3(PlanetPosition(seconds, planet)-cameraPosition), bodies[planet].radius,true,-cameraFloat);
    if (photonView) {
        packetCache.clear();
        packetCache.reserve((destinations.size() + 20) * 4 + 1);
        const auto appendStream = [&](const glm::dvec3& emitter, const glm::vec3& color, bool historical) {
            const double delay = glm::max(0.001, OrbitalMechanics::LightTravelDelay(
                glm::dvec3(emitter), glm::dvec3(photonDetector), gravitationalParameter, horizonRadius));
            for (int packet = 0; packet < 4; ++packet) {
                const double progress = std::fmod(seconds / delay + packet / 4.0, 1.0);
                const auto source = historical ? FlightAt(seconds - progress * delay).state.position : emitter;
                packetCache.push_back({glm::vec3(glm::mix(source, photonDetector, progress)-cameraPosition), color});
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
        packetCache.push_back({glm::vec3(photonDetector-cameraPosition), glm::vec3(0.4f, 1.0f, 0.5f)});
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
    RenderLensing(viewProjection,cameraPosition,seconds,observerBeta);
    if (selected>=0 && guides.vao) {
        const auto target=glm::vec3(ResolveTarget(selected,seconds).position-cameraPosition);
        const auto delta=target;
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

void BlackHole::BuildSkyCube() {
    int viewport[4],framebuffer;
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    unsigned int capture;glGenFramebuffers(1,&capture);glBindFramebuffer(GL_FRAMEBUFFER,capture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_CUBE_MAP,skyCube);
    for(int face=0;face<6;++face)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA16F,1024,1024,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
    const glm::vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const glm::vec3 ups[]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    const auto projection=glm::perspective(glm::half_pi<float>(),1.0f,.01f,1e15f);
    glViewport(0,0,1024,1024);glDisable(GL_DEPTH_TEST);
    for(int face=0;face<6;++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,skyCube,0);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        const auto vp=projection*glm::lookAt(glm::vec3(0),directions[face],ups[face]);
        glUseProgram(program);
        const glm::mat4 identity(1);
        glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
        glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(identity));
        glUniform3f(glGetUniformLocation(program,"cameraPosition"),0,0,0);
        glUniform1i(glGetUniformLocation(program,"surface"),7);
        glBindVertexArray(backgroundStars.vao);glPointSize(2);glDrawArrays(GL_POINTS,0,backgroundStars.vertexCount);
        galaxies.Render(vp,glm::dvec3(0));
    }
    glBindVertexArray(0);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glDeleteFramebuffers(1,&capture);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
}

void BlackHole::RenderLensing(const glm::mat4& vp,const glm::dvec3& camera,double seconds,const glm::vec3& beta) const {
    int viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sourceColor);
    if(sourceWidth!=viewport[2] || sourceHeight!=viewport[3]) {
        sourceWidth=viewport[2];sourceHeight=viewport[3];
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,sourceWidth,sourceHeight,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D,sourceDepth);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,sourceWidth,sourceHeight,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D,sourceColor);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],sourceWidth,sourceHeight);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,sourceDepth);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],sourceWidth,sourceHeight);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_CUBE_MAP,skyCube);
    glUseProgram(lensProgram);
    const auto inverse=glm::inverse(vp),diskInverse=glm::inverse(DiskOrientation());
    const auto cameraFloat=glm::vec3(camera),axis=glm::vec3(DiskOrientation()*glm::vec4(0,0,1,0));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram,"inverseViewProjection"),1,GL_FALSE,glm::value_ptr(inverse));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    glUniformMatrix4fv(glGetUniformLocation(lensProgram,"inverseDiskOrientation"),1,GL_FALSE,glm::value_ptr(diskInverse));
    glUniform3fv(glGetUniformLocation(lensProgram,"cameraPosition"),1,glm::value_ptr(cameraFloat));
    glUniform3fv(glGetUniformLocation(lensProgram,"observerBeta"),1,glm::value_ptr(beta));
    glUniform3fv(glGetUniformLocation(lensProgram,"diskAxis"),1,glm::value_ptr(axis));
    glUniform1f(glGetUniformLocation(lensProgram,"horizonRadius"),horizonRadius);
    glUniform2f(glGetUniformLocation(lensProgram,"diskRadii"),innerDiskRadius,outerDiskRadius);
    glUniform1f(glGetUniformLocation(lensProgram,"diskPhase"),static_cast<float>(std::remainder(OrbitalMechanics::MeanMotion(gravitationalParameter,outerDiskRadius)*seconds,glm::two_pi<double>())));
    glUniform1f(glGetUniformLocation(lensProgram,"plasmaTime"),static_cast<float>(seconds/timeScale));
    glUniform1i(glGetUniformLocation(lensProgram,"sourceScene"),0);glUniform1i(glGetUniformLocation(lensProgram,"sourceDepth"),1);glUniform1i(glGetUniformLocation(lensProgram,"sky"),2);
    glDisable(GL_DEPTH_TEST);glBindVertexArray(screenVao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindVertexArray(0);glEnable(GL_DEPTH_TEST);glActiveTexture(GL_TEXTURE0);
}

void BlackHole::Release() {
    if(stellarProperties)glDeleteBuffers(1,&stellarProperties);stellarProperties=0;
    for(auto texture:{skyCube,sourceColor,sourceDepth})if(texture)glDeleteTextures(1,&texture);
    skyCube=sourceColor=sourceDepth=0;sourceWidth=sourceHeight=0;
    atmosphere.Release();
    galaxies.Release();
    for (auto* mesh : {&sphere, &backgroundStars, &craftRing, &craftTrail, &photonPackets, &guides, &waterPatch}) {
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
