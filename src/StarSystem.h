#pragma once
#include "Atmosphere.h"
#include "OrbitalMechanics.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GLFWwindow;

class StarSystem
{
  public:
    struct Star
    {
        int id = 0;
        std::string name;
        std::string classification;
        double massSolar = 1;
        double radiusSolar = 1;
        double temperature = 5772;
        double luminositySolar = 1;
        double ageGyr = 4.6;
    };
    struct Planet
    {
        std::string name;
        std::string classification;
        OrbitalMechanics::Orbit orbit;
        double radiusEarth;
        double massEarth;
        double temperature;
        double daySeconds;
        float tilt;
        unsigned int seed;
        float seaLevel;
        bool gas;
        bool atmosphere;
        glm::vec3 color;
        int parent = -1;
    };

    static Star DescribeStar(int id, const std::string &name);
    void Generate(int id, const std::string &name);
    void LoadSolarSystem();
    bool Initialize();
    void Release();
    void Update(double seconds, glm::vec3 &front, glm::vec3 &up);
    void Move(GLFWwindow *window, float realSeconds, glm::vec3 &front, glm::vec3 &up);
    void Render(float fieldOfView, float aspect, const glm::vec3 &front, const glm::vec3 &up, double seconds,
                bool trajectories, bool photonView);
    void GoToSelected(double seconds, glm::vec3 &front, glm::vec3 &up);
    void ToggleLanding(double seconds, glm::vec3 &front, glm::vec3 &up);
    void Overview(glm::vec3 &front, glm::vec3 &up);
    void Pick(const glm::vec3 &direction, double seconds);
    void ToggleFollow(double seconds);
    glm::dvec3 Position(int planet, double seconds) const;
    const Star &Host() const
    {
        return star;
    }
    const std::vector<Planet> &Planets() const
    {
        return planets;
    }
    size_t AsteroidCount() const
    {
        return asteroids.size();
    }
    bool IsSolarSystem() const
    {
        return solarSystem;
    }
    int Selected() const
    {
        return selected;
    }
    void SelectPlanet(int planet)
    {
        selected = planet >= 0 && planet < static_cast<int>(planets.size()) ? planet : -1;
        following = false;
    }
    bool IsLanded() const
    {
        return landed;
    }
    bool IsFollowing() const
    {
        return following;
    }
    std::string Information(double seconds) const;
    glm::dvec3 camera = glm::dvec3(0, 0, 3);
    double speedAu = 0.1;

  private:
    glm::mat4 Rotation(int planet, double seconds) const;
    double SurfaceRadius(int planet, const glm::dvec3 &direction, double seconds) const;
    void ConstrainSurface(double seconds, glm::vec3 &front, glm::vec3 &up);
    void StoreLandedDirection(double seconds);
    void BuildOrbitPaths();
    Star star;
    std::vector<Planet> planets;
    std::vector<OrbitalMechanics::Orbit> asteroids;
    bool solarSystem = false;
    double GravityFor(int planet) const;
    glm::dvec3 RelativePosition(int planet, double seconds) const;
    int selected = -1;
    int landedPlanet = -1;
    bool landed = false;
    glm::dvec3 landedDirection = glm::dvec3(0, 1, 0); // unit vector from the planet centre in the planet's own frame
    bool following = false;
    double lastTime = 0;
    glm::dvec3 lastFollowPosition = glm::dvec3(0);
    unsigned int program = 0;
    unsigned int sphereVao = 0, sphereBuffer = 0;
    unsigned int lineVao = 0, lineBuffer = 0;
    unsigned int patchVao = 0, patchBuffer = 0;
    int sphereCount = 0, patchCount = 0;
    struct Uniforms
    {
        int viewProjection = -1, starPosition = -1, phase = -1, model = -1, normalMatrix = -1, bodyKind = -1,
            terrainSeed = -1, seaLevel = -1, terrainRelief = -1, waterWaveHeight = -1, albedo = -1, temperature = -1,
            patchEnabled = -1, patchDrawing = -1, patchCenter = -1, patchFrame = -1;
    } uniform;
    // Orbit polylines relative to each body's parent. They do not depend on time, so they are built on first use.
    static constexpr int orbitSamples = 257;
    std::vector<glm::dvec3> orbitPaths;
    // Per-frame scratch, kept to avoid reallocating.
    std::vector<glm::dvec3> positions;
    std::vector<glm::vec3> lineVertices;
    size_t lineCapacity = 0;
    Atmosphere atmosphere;
};
