#pragma once
#include <glm/vec3.hpp>
#include <vector>

namespace PlanetTerrain {
    const char* ShaderFunctions();
    float SurfaceRadius(const glm::vec3& direction, unsigned int seed, float wavePhase,
                        float seaLevel = 0.035f, float relief = 0.10f, float waves = 0.0015f);
    std::vector<glm::vec3> SphereVertices(int latitudeSegments, int longitudeSegments);
    std::vector<glm::vec3> SpherePatch(const glm::vec3& direction,int segments=96);
}
