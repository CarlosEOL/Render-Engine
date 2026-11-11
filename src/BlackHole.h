#pragma once

#include <glm/mat4x4.hpp>
#include <vector>
#include <string>
#include "OrbitalMechanics.h"
#include "Atmosphere.h"
#include "GalaxyBackground.h"

class BlackHole {
public:
    bool Initialize();
    static glm::vec3 PlanetPosition(double seconds, int planet = 0);
    float PlanetSurfaceRadius(const glm::vec3& worldDirection, double seconds, int planet = 0) const;
    static int NearestPlanet(const glm::vec3& position, double seconds);
    struct Target {
        glm::vec3 position;
        float radius;
        std::string name;
    };
    Target ResolveTarget(int id, double seconds) const;
    int PickTarget(const glm::vec3& origin, const glm::vec3& direction, double seconds) const;
    static glm::mat4 PlanetRotation(double seconds);
    static double GravityParameter() { return 1.8; }
    static float HorizonRadius() { return 0.24f; }
    void Update(double seconds);
    void BoostSpacecraft();
    void ResetSpacecraft(double seconds);
    void RegenerateTerrain() { terrainSeed = (terrainSeed + 1) % 10000; }
    glm::vec3 SpacecraftPosition() const { return glm::vec3(spacecraft.position); }
    const char* SpacecraftStatus() const;
    double SpacecraftLightAge(const glm::vec3& camera, double seconds) const;
    void SetPhotonView(bool enabled, const glm::vec3& detector) { photonView = enabled; photonDetector = detector; }
    void Render(const glm::mat4& viewProjection, const glm::vec3& cameraPosition, double seconds,
                const glm::vec3& observerVelocity = glm::vec3(0.0f)) const;
    void Release();

private:
    struct Mesh {
        unsigned int vao = 0;
        unsigned int buffer = 0;
        int vertexCount = 0;
    };

    unsigned int program = 0;
    unsigned int lensProgram = 0;
    unsigned int screenVao = 0;
    Mesh sphere;
    Mesh backgroundStars;
    Mesh craftRing;
    Mesh craftTrail;
    Mesh photonPackets;
    bool photonView = false;
    glm::vec3 photonDetector = glm::vec3(0);
    struct PhotonPacket {
        glm::vec3 position;
        glm::vec3 color;
    };
    mutable std::vector<PhotonPacket> packetCache;
    OrbitalMechanics::ParticleState spacecraft = {};
    std::vector<glm::vec3> trail;
    double spacecraftTime = 0.0;
    double fuelSeconds = 1.2;
    double trailSampleTime = 0.0;
    bool boosterFiring = false;
    bool spacecraftCaptured = false;
    unsigned int terrainSeed = 618;
    std::vector<glm::vec3> destinations;
    Atmosphere atmosphere;
    GalaxyBackground galaxies;
    struct FlightSample {
        double time;
        OrbitalMechanics::ParticleState state;
        bool firing;
        bool captured;
    };
    std::vector<FlightSample> flightHistory;
    double missionStart = 0.0;
    FlightSample FlightAt(double seconds) const;
    FlightSample ObservedFlight(const glm::vec3& camera, double seconds) const;
};
