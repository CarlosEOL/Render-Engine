#include "PlanetTerrain.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <glm/gtc/constants.hpp>

namespace {
    float Hash(glm::vec3 cell, unsigned int seed) {
        std::uint32_t hash = seed ^ static_cast<std::uint32_t>(static_cast<int>(cell.x)) * 374761393u
            ^ static_cast<std::uint32_t>(static_cast<int>(cell.y)) * 668265263u
            ^ static_cast<std::uint32_t>(static_cast<int>(cell.z)) * 2246822519u;
        hash = (hash ^ (hash >> 13)) * 1274126177u;
        hash ^= hash >> 16;
        return static_cast<float>(hash & 0x00ffffffu) / 16777215.0f;
    }

    float Noise(const glm::vec3& position, unsigned int seed) {
        const auto cell = glm::floor(position);
        const auto f = glm::fract(position);
        const auto b = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
        const float lower = glm::mix(
            glm::mix(Hash(cell, seed), Hash(cell + glm::vec3(1, 0, 0), seed), b.x),
            glm::mix(Hash(cell + glm::vec3(0, 1, 0), seed), Hash(cell + glm::vec3(1, 1, 0), seed), b.x), b.y);
        const float upper = glm::mix(
            glm::mix(Hash(cell + glm::vec3(0, 0, 1), seed), Hash(cell + glm::vec3(1, 0, 1), seed), b.x),
            glm::mix(Hash(cell + glm::vec3(0, 1, 1), seed), Hash(cell + glm::vec3(1, 1, 1), seed), b.x), b.y);
        return 2.0f * glm::mix(lower, upper, b.z) - 1.0f;
    }
}

std::vector<glm::vec3> PlanetTerrain::SpherePatch(const glm::vec3& direction,int segments) {
    const auto reference=std::abs(direction.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0);
    const auto right=glm::normalize(glm::cross(reference,direction)),up=glm::cross(direction,right);
    const auto point=[&](int x,int y) {
        return glm::normalize(direction+right*(.025f*(2.0f*x/segments-1))+up*(.025f*(2.0f*y/segments-1)));
    };
    std::vector<glm::vec3> vertices;vertices.reserve(segments*segments*6);
    for(int y=0;y<segments;++y)for(int x=0;x<segments;++x)
        vertices.insert(vertices.end(),{point(x,y),point(x+1,y),point(x+1,y+1),point(x,y),point(x+1,y+1),point(x,y+1)});
    return vertices;
}

float PlanetTerrain::SurfaceRadius(const glm::vec3& direction, unsigned int seed, float wavePhase,
                                  float seaLevel, float relief, float waves) {
    auto position = direction * 2.6f;
    float continents = 0.0f;
    float amplitude = 0.5f;
    for (int octave = 0; octave < 5; ++octave) {
        continents += amplitude * Noise(position, seed);
        position = position * 2.03f + glm::vec3(7.1f, 3.7f, 1.9f);
        amplitude *= 0.5f;
    }
    continents /= 0.96875f;
    const float ridges = 1.0f - std::abs(Noise(direction * 11.0f + glm::vec3(13.0f), seed));
    const float height = 0.75f * continents + 0.22f * (ridges * ridges - 0.5f)
        * glm::smoothstep(-0.1f, 0.25f, continents);
    const float water = 1.0f - glm::smoothstep(seaLevel - 0.008f, seaLevel + 0.008f, height);
    const float wave = (std::sin(35.0f * direction.y + 4.0f * std::sin(10.0f * direction.x) + wavePhase)
        + 0.35f * std::sin(80.0f * direction.y + 8.0f * direction.x - wavePhase)) / 1.35f;
    return 1.0f + relief * glm::max(height, seaLevel) + water * waves * wave;
}

std::vector<glm::vec3> PlanetTerrain::SphereVertices(int latitudeSegments, int longitudeSegments) {
    const auto point = [](float latitude, float longitude) {
        const float ring = std::cos(latitude);
        return glm::vec3(ring * std::cos(longitude), ring * std::sin(longitude), std::sin(latitude));
    };
    std::vector<glm::vec3> vertices;
    vertices.reserve(latitudeSegments * longitudeSegments * 6);
    for (int row = 0; row < latitudeSegments; ++row) {
        const float lower = -glm::half_pi<float>() + glm::pi<float>() * row / latitudeSegments;
        const float upper = lower + glm::pi<float>() / latitudeSegments;
        for (int col = 0; col < longitudeSegments; ++col) {
            const float left = glm::two_pi<float>() * col / longitudeSegments;
            const float right = glm::two_pi<float>() * (col + 1) / longitudeSegments;
            const auto a = point(lower, left), b = point(lower, right);
            const auto c = point(upper, right), d = point(upper, left);
            vertices.insert(vertices.end(), {a,b,c,a,c,d});
        }
    }
    return vertices;
}

const char* PlanetTerrain::ShaderFunctions() {
    return R"(
uniform float terrainSeed;
uniform float seaLevel;
uniform float terrainRelief;
uniform float waterWaveHeight;

float terrainHash(vec3 cell) {
    uvec3 coordinate = uvec3(ivec3(cell));
    uint hash = uint(terrainSeed) ^ coordinate.x * 374761393u
        ^ coordinate.y * 668265263u ^ coordinate.z * 2246822519u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return float(hash & 0x00ffffffu) / 16777215.0;
}

float terrainNoise(vec3 position) {
    vec3 cell = floor(position);
    vec3 fraction = fract(position);
    vec3 blend = fraction * fraction * fraction * (fraction * (fraction * 6.0 - 15.0) + 10.0);
    float lower = mix(
        mix(terrainHash(cell), terrainHash(cell + vec3(1, 0, 0)), blend.x),
        mix(terrainHash(cell + vec3(0, 1, 0)), terrainHash(cell + vec3(1, 1, 0)), blend.x), blend.y);
    float upper = mix(
        mix(terrainHash(cell + vec3(0, 0, 1)), terrainHash(cell + vec3(1, 0, 1)), blend.x),
        mix(terrainHash(cell + vec3(0, 1, 1)), terrainHash(cell + vec3(1, 1, 1)), blend.x), blend.y);
    return 2.0 * mix(lower, upper, blend.z) - 1.0;
}

float terrainFbm(vec3 position) {
    float height = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        height += amplitude * terrainNoise(position);
        position = position * 2.03 + vec3(7.1, 3.7, 1.9);
        amplitude *= 0.5;
    }
    return height / 0.96875;
}

float terrainHeight(vec3 direction) {
    float continents = terrainFbm(direction * 2.6);
    float ridges = 1.0 - abs(terrainNoise(direction * 11.0 + vec3(13.0)));
    return 0.75 * continents + 0.22 * (ridges * ridges - 0.5)
        * smoothstep(-0.1, 0.25, continents);
}

float oceanWave(vec3 direction, float phase) {
    return (sin(35.0 * direction.y + 4.0 * sin(10.0 * direction.x) + phase)
        + 0.35 * sin(80.0 * direction.y + 8.0 * direction.x - phase)) / 1.35;
}

vec3 planetSurface(vec3 direction, float height, float phase) {
    float water = 1.0 - smoothstep(seaLevel - 0.008, seaLevel + 0.008, height);
    float radius = 1.0 + terrainRelief * max(height, seaLevel);
    radius += water * waterWaveHeight * oceanWave(direction, phase);
    return direction * radius;
}
)";
}
