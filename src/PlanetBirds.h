#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <vector>

namespace PlanetWeather { struct State; }

class PlanetBirds {
public:
    struct Habitat {
        unsigned seed;
        double radiusMetres, temperature;
        float seaLevel;
        bool earth, layeredTerrain;
    };

    bool Initialize();
    void Release();
    void Reset();
    void ClearCounts() { visibleCount = 0; visibleSpecies = 0; }
    std::size_t VisibleCount() const { return visibleCount; }
    unsigned VisibleSpecies() const { return visibleSpecies; }

    void Render(const Habitat& habitat, const glm::dmat3& rotation,
                const glm::dvec3& cameraFromPlanetAu, const glm::mat4& viewProjection,
                const glm::vec3& light, double seconds, const PlanetWeather::State& weather);

private:
    enum class Species : unsigned { Gull, Swallow, Goose, Count };
    static constexpr auto speciesCount = static_cast<unsigned>(Species::Count);
    static constexpr std::size_t maxFlocks = 8, maxFlockSize = 14;
    struct Flight {
        glm::vec3 position, velocity;
        float phase, scale, bank = 0;
    };
    struct Flock {
        glm::ivec3 cell;
        glm::dvec3 origin;
        glm::dmat3 frame;
        Species species;
        std::size_t count;
        double age = 0;
        float course, flightHeight;
        std::array<Flight, maxFlockSize> birds{};
    };
    struct Vertex {
        glm::vec3 position, normal, color;
        float wing;
    };
    struct Instance {
        glm::vec4 positionScale, headingBank, wingPose;
    };

    static std::vector<Vertex> BuildMesh(Species species);
    static float GroundElevation(const Habitat& habitat, const glm::vec3& normal);
    void UpdateFlocks(const Habitat& habitat, const glm::dvec3& cameraLocal);
    void Simulate(const Habitat& habitat, float step, const PlanetWeather::State& weather);

    unsigned program = 0, vao = 0, vertices = 0, instances = 0;
    std::array<int, speciesCount> vertexStart{}, vertexCount{};
    std::array<std::vector<Instance>, speciesCount> visible;
    std::vector<Flock> flocks;
    glm::ivec3 cameraCell{};
    bool haveCell = false, haveTime = false;
    double lastTime = 0, stepRemainder = 0;
    std::size_t visibleCount = 0;
    unsigned visibleSpecies = 0;
};
