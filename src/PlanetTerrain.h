#pragma once
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <filesystem>
#include <vector>
namespace PlanetWeather {struct State;}

namespace PlanetTerrain {
    const char* ShaderFunctions();
    const char* SurfaceDetailShaderFunctions();
    std::filesystem::path EarthAssetPath(const char* filename);
    bool HasEarthMap();
    glm::vec3 EarthGeography(const glm::vec3& direction,bool detailed=true);
    unsigned LoadEarthReliefTexture();
    void BindEarthReliefDetail(unsigned program,const glm::vec3& direction,unsigned& texture,glm::ivec2& origin);
    float ArcticWinter(double seconds);
    float WaterCoverage(const glm::vec3& direction,unsigned seed,float height,float seaLevel,bool earth,float arcticWinter);
    float WaterDepthMetres(const glm::vec3& direction,unsigned seed,float seaLevel,double radiusMetres,
        bool earth=false,float arcticWinter=0,bool layeredTerrain=false);
    float SurfaceElevation(const glm::vec3& direction, unsigned int seed, float phase,
        float seaLevel, float relief, float waves, bool oceanOnly=false,const PlanetWeather::State* weather=nullptr,
        bool earth=false,float arcticWinter=0,bool layeredTerrain=false);
    std::vector<glm::vec3> SurfaceLodVertices(double radiusMetres);
    void ConfigureSurfacePatch(unsigned int program,const glm::dvec3& anchor,
        const glm::dmat3& rotation,const glm::dvec3& centerRelativeAu,double radiusMetres,float elevation);
    float SurfaceRadius(const glm::vec3& direction, unsigned int seed, float wavePhase,
                        float seaLevel = 0.035f, float relief = 0.10f, float waves = 0.0015f);
    std::vector<glm::vec3> SphereVertices(int latitudeSegments, int longitudeSegments);
    std::vector<glm::vec3> SpherePatch(const glm::vec3& direction,int segments=96);
}
