#pragma once
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <vector>

namespace PlanetTerrain {
    const char* ShaderFunctions();
    const char* SurfaceDetailShaderFunctions();
    float SurfaceElevation(const glm::vec3& direction, unsigned int seed, float phase,
        float seaLevel, float relief, float waves, bool oceanOnly=false);
    std::vector<glm::vec3> SurfaceLodVertices(double radiusMetres);
    void ConfigureSurfacePatch(unsigned int program,const glm::dvec3& anchor,
        const glm::dmat3& rotation,const glm::dvec3& centerRelativeAu,double radiusMetres,float elevation);
    float SurfaceRadius(const glm::vec3& direction, unsigned int seed, float wavePhase,
                        float seaLevel = 0.035f, float relief = 0.10f, float waves = 0.0015f);
    std::vector<glm::vec3> SphereVertices(int latitudeSegments, int longitudeSegments);
    std::vector<glm::vec3> SpherePatch(const glm::vec3& direction,int segments=96);
}
