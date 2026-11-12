#pragma once

#include <glm/mat4x4.hpp>
#include <array>
#include <cmath>
#include <vector>
#include <string>
#include "OrbitalMechanics.h"
#include "Atmosphere.h"
#include "StellarActivity.h"
#include "GalaxyBackground.h"
#include "Astronomy.h"
#include "UniverseCatalog.h"
#include "MilkyWaySky.h"

class BlackHole {
public:
    bool Initialize();
    void SetSpin(double value) { if(std::isfinite(value))spin=glm::clamp(value,-.98,.98); }
    double Spin() const { return spin; }
    void SetRayBudget(int value) { rayBudget=glm::clamp(value,64,384); }
    int RayBudget() const { return rayBudget; }
    void SetAtmospheresEnabled(bool enabled) { atmospheresEnabled=enabled; }
    unsigned int LensingSourceDepthTexture() const { return sourceDepth; }
    void RenderBackground(const glm::mat4& vp,const glm::dvec3& camera,bool photons=false,
        double seconds=0,const glm::dvec3& detector=glm::dvec3(0)) const;
    void RenderCore(const glm::mat4& vp,const glm::dvec3& localCamera,int coreId,double seconds) const;
    std::vector<int> NearbyStars(const glm::dvec3& camera) const { return catalogue.Nearby(camera); }
    const Universe::Catalog& Catalogue() const {return catalogue;}
    int NearestStar(const glm::dvec3& camera) const {return catalogue.Nearest(camera);}
    void SetStellarActivity(bool enabled) { stellarActivity.SetEnabled(enabled); }
    void SetStellarCorona(bool enabled) {stellarActivity.SetCoronaEnabled(enabled);}
    void SetMagnitudeLimit(float value){value=glm::clamp(value,-2.f,30.f);if(value!=magnitudeLimit){magnitudeLimit=value;skyCubeDirty=true;}}
    void SetWarmStellarColor(bool enabled) { stellarActivity.SetWarmColor(enabled); }
    void InvalidateSky() const { milkyWay.Invalidate(); }
    unsigned MilkyWayCacheBuilds() const { return milkyWay.CacheBuilds(); }
    unsigned MilkyWayTexture() const { return milkyWay.Texture(); }
    void TriggerStellarFlare(double seconds) { stellarActivity.Trigger(seconds); }
    static glm::dvec3 PlanetPosition(double seconds, int planet = 0);
    double PlanetSurfaceRadius(const glm::vec3& worldDirection, double seconds, int planet = 0) const;
    static int NearestPlanet(const glm::dvec3& position, double seconds);
    struct Target {
        glm::dvec3 position;
        float radius;
        std::string name;
    };
    Target ResolveTarget(int id, double seconds) const;
    glm::mat3 TargetRotation(int id, double seconds) const;
    int PickTarget(const glm::dvec3& origin, const glm::vec3& direction, double seconds) const;
    int PickCatalogueStar(const glm::dvec3& origin,const glm::vec3& direction,double angularTolerance,int exclude=-1) const;
    void RenderCatalogueSelection(const glm::mat4& vp,const glm::dvec3& camera,int selected,double seconds) const;
    static glm::mat4 PlanetRotation(double seconds,int planet=0);
    static double GravityParameter() { return Astronomy::gargantuaGravityAu; }
    static float HorizonRadius() { return static_cast<float>(Astronomy::gargantuaHorizonAu); }
    void Update(double seconds);
    void BoostSpacecraft();
    void ResetSpacecraft(double seconds);
    void RegenerateTerrain() { terrainSeed = (terrainSeed + 1) % 10000; }
    glm::dvec3 SpacecraftPosition() const { return spacecraft.position; }
    glm::dvec3 SpacecraftVelocity() const { return spacecraft.velocity; }
    double SpacecraftFuelSeconds() const { return fuelSeconds; }
    double SpacecraftSpeedAu() const { return glm::length(spacecraft.velocity); }
    double SpacecraftEnergy() const;
    double SpacecraftProperTime() const { return spacecraftProperTime; }
    bool RangersDetached() const { return rangersDetached; }
    double ObserverClockRate(const glm::dvec3& camera,double seconds,const glm::dvec3& velocity=glm::dvec3(0)) const;
    const char* SpacecraftStatus() const;
    double SpacecraftLightAge(const glm::dvec3& camera, double seconds) const;
    glm::dvec3 ObservedSpacecraftPosition(const glm::dvec3& camera,double seconds) const;
    struct VehiclePose {glm::dvec3 position,velocity;glm::dmat3 rotation;};
    VehiclePose CockpitPose(int vehicle,double seconds) const;
    void SetPhotonView(bool enabled, const glm::dvec3& detector) { photonView = enabled; photonDetector = detector; }
    void Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition, double seconds,
                const glm::vec3& observerVelocity = glm::vec3(0.0f), bool trajectories = true, int selected = -1,int interiorVehicle=0,
                const glm::vec3& physicalObserverBeta=glm::vec3(0),bool physicalObserver=false) const;
    void Release();

private:
    struct Mesh {
        unsigned int vao = 0;
        unsigned int buffer = 0;
        int vertexCount = 0;
    };

    unsigned int program = 0;
    double spin=.8;
    float magnitudeLimit=7;
    std::vector<float> starLuminosities;
    int rayBudget=160;
    bool atmospheresEnabled=true;
    unsigned int lensProgram = 0;
    unsigned int screenVao = 0;
    unsigned int stellarProperties = 0;
    unsigned int stellarResiduals = 0;
    unsigned int skyCube = 0;
    unsigned int sourceColor = 0;
    unsigned int sourceDepth = 0;
    mutable int sourceWidth = 0, sourceHeight = 0;
    mutable glm::dvec3 skyCubeOrigin=glm::dvec3(0);
    mutable bool skyCubeDirty=true,skyCubeAllocated=false;
    void BuildSkyCube(const glm::dvec3& origin=glm::dvec3(0)) const;
    void RenderLensing(const glm::mat4& viewProjection,const glm::dvec3& camera,double seconds,const glm::vec3& beta,
                       const glm::vec3& physicalBeta,bool physicalObserver,int coreId=-1) const;
    Mesh sphere;
    Mesh backgroundStars;
    Mesh craftRing;
    Mesh rangerMesh;
    Mesh craftTrail;
    Mesh photonPackets;
    Mesh guides;
    mutable Mesh waterPatch;
    mutable glm::dvec3 waterPatchDirection = glm::dvec3(0);
    mutable double waterPatchRadiusMetres=0;
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
    bool manualBoost = false;
    bool spacecraftCaptured = false;
    double spacecraftProperTime=0;
    bool rangersDetached=false;
    double rangerSeparationTime=0;
    std::array<OrbitalMechanics::ParticleState,2> rangers={};
    struct RangerSample {
        double time;
        std::array<OrbitalMechanics::ParticleState,2> state;
    };
    std::vector<RangerSample> rangerHistory;
    OrbitalMechanics::ParticleState RangerAt(double seconds,int index) const;
    OrbitalMechanics::ParticleState ObservedRanger(const glm::dvec3& camera,double seconds,int index) const;
    unsigned int terrainSeed = 618;
    std::vector<glm::dvec3> destinations;
    Universe::Catalog catalogue;
    Atmosphere atmosphere;
    StellarActivity stellarActivity;
    GalaxyBackground galaxies;
    MilkyWaySky milkyWay;
    struct FlightSample {
        double time;
        OrbitalMechanics::ParticleState state;
        bool firing;
        bool captured;
        double properTime=0;
    };
    std::vector<FlightSample> flightHistory;
    double missionStart = 0.0;
    FlightSample FlightAt(double seconds) const;
    FlightSample ObservedFlight(const glm::dvec3& camera, double seconds) const;
};
