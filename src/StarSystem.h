#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "OrbitalMechanics.h"
#include "Atmosphere.h"

struct GLFWwindow;

class StarSystem {
public:
    struct Star {
        int id = 0;
        std::string name;
        std::string classification;
        double massSolar = 1;
        double radiusSolar = 1;
        double temperature = 5772;
        double luminositySolar = 1;
        double ageGyr = 4.6;
    };
    struct Planet {
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
    };

    static Star DescribeStar(int id, const std::string& name);
    void Generate(int id, const std::string& name);
    bool Initialize();
    void Release();
    void Update(double seconds, glm::vec3& front, glm::vec3& up);
    void Move(GLFWwindow* window, float realSeconds, glm::vec3& front, glm::vec3& up);
    void Render(float fieldOfView, float aspect, const glm::vec3& front, const glm::vec3& up,
                double seconds, bool trajectories, bool photonView);
    void GoToSelected(double seconds, glm::vec3& front, glm::vec3& up);
    void ToggleLanding(double seconds, glm::vec3& front, glm::vec3& up);
    void Overview(glm::vec3& front, glm::vec3& up);
    void Pick(const glm::vec3& direction, double seconds);
    void ToggleFollow(double seconds);
    glm::dvec3 Position(int planet, double seconds) const;
    const Star& Host() const { return star; }
    const std::vector<Planet>& Planets() const { return planets; }
    int Selected() const { return selected; }
    void SelectPlanet(int planet) { selected = planet; }
    bool IsLanded() const { return landed; }
    bool IsFollowing() const { return following; }
    std::string Information(double seconds) const;
    glm::dvec3 camera = glm::dvec3(0,0,3);
    double speedAu = 0.1;
private:
    glm::mat4 Rotation(int planet, double seconds) const;
    double SurfaceRadius(int planet, const glm::dvec3& direction, double seconds) const;
    void ConstrainSurface(double seconds, glm::vec3& front, glm::vec3& up);
    void UpdatePatch(const glm::vec3& direction);
    Star star;
    std::vector<Planet> planets;
    int selected = -1;
    int landedPlanet = -1;
    bool landed = false;
    bool following = false;
    double lastTime = 0;
    glm::dvec3 lastFollowPosition = glm::dvec3(0);
    unsigned int program = 0;
    unsigned int sphereVao = 0, sphereBuffer = 0;
    unsigned int lineVao = 0, lineBuffer = 0;
    unsigned int patchVao = 0, patchBuffer = 0;
    int sphereCount = 0, patchCount = 0;
    glm::vec3 patchDirection = glm::vec3(0);
    Atmosphere atmosphere;
};
