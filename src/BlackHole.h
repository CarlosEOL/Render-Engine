#pragma once

#include <glm/mat4x4.hpp>
#include <vector>
#include <string>
#include "OrbitalMechanics.h"
#include "Atmosphere.h"
#include "StellarActivity.h"
#include "GalaxyBackground.h"
#include "Astronomy.h"

class BlackHole {
public:
    bool Initialize();
    void SetStellarActivity(bool enabled) { stellarActivity.SetEnabled(enabled); }
    void TriggerStellarFlare(double seconds) { stellarActivity.Trigger(seconds); }
    static glm::dvec3 PlanetPosition(double seconds, int planet = 0);
    float PlanetSurfaceRadius(const glm::vec3& worldDirection, double seconds, int planet = 0) const;
    static int NearestPlanet(const glm::dvec3& position, double seconds);
    struct Target {
        glm::dvec3 position;
        float radius;
        std::string name;
    };
    Target ResolveTarget(int id, double seconds) const;
    glm::mat3 TargetRotation(int id, double seconds) const;
    int PickTarget(const glm::dvec3& origin, const glm::vec3& direction, double seconds) const;
    static glm::mat4 PlanetRotation(double seconds);
    static double GravityParameter() { return Astronomy::gargantuaGravityAu; }
    static float HorizonRadius() { return static_cast<float>(Astronomy::gargantuaHorizonAu); }
    void Update(double seconds);
    void BoostSpacecraft();
    void ResetSpacecraft(double seconds);
    void RegenerateTerrain() { terrainSeed = (terrainSeed + 1) % 10000; }
    glm::dvec3 SpacecraftPosition() const { return spacecraft.position; }
    const char* SpacecraftStatus() const;
    double SpacecraftLightAge(const glm::dvec3& camera, double seconds) const;
    void SetPhotonView(bool enabled, const glm::dvec3& detector) { photonView = enabled; photonDetector = detector; }
    void Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition, double seconds,
                const glm::vec3& observerVelocity = glm::vec3(0.0f), bool trajectories = true, int selected = -1) const;
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
    unsigned int stellarProperties = 0;
    unsigned int skyCube = 0;
    unsigned int sourceColor = 0;
    unsigned int sourceDepth = 0;
    mutable int sourceWidth = 0, sourceHeight = 0;
    void BuildSkyCube();
    void RenderLensing(const glm::mat4& viewProjection,const glm::dvec3& camera,double seconds,const glm::vec3& beta) const;
    Mesh sphere;
    Mesh backgroundStars;
    Mesh craftRing;
    Mesh craftTrail;
    Mesh photonPackets;
    Mesh guides;
    mutable Mesh waterPatch;
    mutable glm::vec3 waterPatchDirection = glm::vec3(0);
    mutable std::vector<std::vector<glm::dvec3>> orbitGuides;
    bool photonView = false;
    glm::dvec3 photonDetector = glm::dvec3(0);
    struct PhotonPacket {
        glm::vec3 position;
        glm::vec3 color;
    };
    mutable std::vector<PhotonPacket> packetCache;
    OrbitalMechanics::ParticleState spacecraft = {};
    std::vector<glm::dvec3> trail;
    double spacecraftTime = 0.0;
    double fuelSeconds = 1.2;
    double trailSampleTime = 0.0;
    bool boosterFiring = false;
    bool spacecraftCaptured = false;
    unsigned int terrainSeed = 618;
    std::vector<glm::vec3> destinations;
    Atmosphere atmosphere;
    StellarActivity stellarActivity;
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
    FlightSample ObservedFlight(const glm::dvec3& camera, double seconds) const;
};
