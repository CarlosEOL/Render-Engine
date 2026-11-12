#pragma once
#include "Atmosphere.h"
#include "StellarActivity.h"
#include "SaturnRings.h"
#include "OrbitalMechanics.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <limits>
#include <cstdint>

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
        std::uint64_t systemSeed = 0;
    };
    struct Planet
    {
        enum class Palette { Climate, Lunar };
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
        Palette palette = Palette::Climate;
    };

    static Star DescribeStar(int id, const std::string &name);
    void Generate(int id, const std::string &name);
    void LoadSolarSystem();
    bool Initialize();
    void SetStellarActivity(bool enabled) { stellarActivity.SetEnabled(enabled); }
    void SetWarmStellarColor(bool enabled) { stellarActivity.SetWarmColor(enabled); }
    void TriggerStellarFlare(double seconds) { stellarActivity.Trigger(seconds); }
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
    void ToggleRotationFollow(double seconds);
    bool IsRotationFollowing() const { return followingRotation; }
    double DetailRadiusAu() const;
    bool InDetailRange() const { return glm::length(camera)<DetailRadiusAu(); }
    size_t PositionEvaluations() const { return positionEvaluations; }
    void SetPhotonDetector() { photonDetector=camera; }
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
        followingRotation = false;
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
    double ObserverClockRate(double seconds) const;
    glm::dvec3 camera = glm::dvec3(0, 0, 3);
    double speedAu = 0.1;

  private:
    glm::mat4 Rotation(int planet, double seconds) const;
    glm::dmat3 RotationDouble(int planet, double seconds) const;
    double SurfaceRadius(int planet, const glm::dvec3 &direction, double seconds) const;
    void ConstrainSurface(double seconds, glm::vec3 &front, glm::vec3 &up);
    void UpdatePatch(const glm::dvec3 &direction,double radiusMetres);
    Star star;
    std::vector<Planet> planets;
    std::vector<OrbitalMechanics::Orbit> asteroids;
    bool solarSystem = false;
    double GravityFor(int planet) const;
    glm::dvec3 RelativePosition(int planet, double seconds) const;
    void InvalidateCaches();
    mutable double cachedPositionTime = std::numeric_limits<double>::quiet_NaN();
    mutable size_t positionEvaluations=0;
    mutable std::vector<glm::dvec3> cachedPositions;
    std::vector<std::vector<glm::dvec3>> cachedOrbits;
    std::vector<glm::dvec3> cachedAsteroids;
    double cachedAsteroidTime = std::numeric_limits<double>::quiet_NaN();
    int selected = -1;
    int landedPlanet = -1;
    bool landed = false;
    bool following = false;
    bool followingRotation = false;
    glm::dmat3 lastFollowRotation = glm::dmat3(1);
    double lastTime = 0;
    glm::dvec3 lastFollowPosition = glm::dvec3(0);
    glm::dvec3 photonDetector = glm::dvec3(0);
    unsigned int program = 0;
    unsigned int sphereVao = 0, sphereBuffer = 0;
    unsigned int lineVao = 0, lineBuffer = 0;
    unsigned int patchVao = 0, patchBuffer = 0;
    int sphereCount = 0, patchCount = 0;
    glm::dvec3 patchDirection = glm::dvec3(0);
    double patchRadiusMetres = 0;
    Atmosphere atmosphere;
    StellarActivity stellarActivity;
    SaturnRings saturnRings;
};
