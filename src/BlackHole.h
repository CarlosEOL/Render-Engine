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
#include "GalacticCore.h"
#include "KerrWormhole.h"
#include "WormholeSky.h"

class BlackHole {
public:
    bool Initialize();
    glm::vec2 DiskAppearance(int id=0) const {
        const int index=GalacticCores::IsCore(id)?id-GalacticCores::firstId+1:0;
        if(diskAppearance[index].x>0)return diskAppearance[index];
        return {1.f,id==0?12000.f:static_cast<float>(GalacticCores::Get(id).temperature)};
    }
    void SetDiskAppearance(int id,float thickness,float temperature){
        if((id!=0 && !GalacticCores::IsCore(id)) || !std::isfinite(thickness) || !std::isfinite(temperature)
            || thickness<.25f || thickness>3.f || temperature<1000.f || temperature>(id==GalacticCores::firstId?1e9f:40000.f))return;
        diskAppearance[GalacticCores::IsCore(id)?id-GalacticCores::firstId+1:0]={thickness,temperature};
    }
    void SetSpin(double value) { if(std::isfinite(value))spin=glm::clamp(value,-.98,.98); }
    double Spin() const { return spin; }
    void SetRayBudget(int value) { rayBudget=glm::clamp(value,64,384); }
    int RayBudget() const { return rayBudget; }
    void SetGargantuaDisk(float outerRadii,float emission){if(std::isfinite(outerRadii)&&std::isfinite(emission)){diskOuterRadii=glm::clamp(outerRadii,2.f,24.f);diskBrightness=glm::clamp(emission,0.f,32.f);}}
    float GargantuaDiskOuterRadius() const {return HorizonRadius()*diskOuterRadii;}
    float GargantuaDiskBrightness() const {return diskBrightness;}
    void SetLensingDiagnostic(bool value) { lensingDiagnostic=value; }
    void SetHotFlowDiagnostic(bool value) { hotFlowDiagnostic=value; }
    void SetAccretionCloudsEnabled(bool value) { accretionCloudsEnabled=value; }
    void SetAccretionPlaneEnabled(bool value) { accretionPlaneEnabled=value; }
    void SetAtmospheresEnabled(bool enabled) { atmospheresEnabled=enabled; }
    void SetWeatherCloudsEnabled(bool enabled) { weatherCloudsEnabled=enabled; }
    void SetOceanReflectionsEnabled(bool enabled) { oceanReflectionsEnabled=enabled; }
    void SetWeather(float windScale,float stormScale){weatherWindScale=windScale;weatherStormScale=stormScale;}
    void SetWeatherEnabled(bool enabled){weatherEnabled=enabled;}
    void SetGalaxyAutoExposure(bool enabled){if(galaxies.SetAutoExposure(enabled))skyCubeDirty=true;}
    unsigned int LensingSourceDepthTexture() const { return sourceDepth; }
    unsigned int WormholeSky(double seconds=0) const;
    static glm::dvec3 GargantuaWormholePosition(double seconds);
    void SetWormholeSky(unsigned int texture){solarWormholeSky=texture;}
    void SetWormholeEnabled(bool value){gargantuaWormhole.SetEnabled(value);}
    bool SetSkyRefreshDeferred(bool value){const bool flush=skyRefreshDeferred && !value && skyCubeDirty;skyRefreshDeferred=value;return flush;}
    size_t LastSubmittedStars() const {return lastSubmittedStars;}
    size_t ResidentStreamStars() const {return catalogue.StreamStars().size();}
    size_t ResidentStarCells() const {return catalogue.ResidentCells();}
    void SetGalaxyMagnitudeLimit(float value){value=glm::clamp(value,-2.f,30.f);if(galaxyMagnitudeLimit!=value){galaxyMagnitudeLimit=value;skyCubeDirty=true;}}
    void RenderBackground(const glm::mat4& vp,const glm::dvec3& camera,bool photons=false,
        double seconds=0,const glm::dvec3& detector=glm::dvec3(0),bool includeLocal=true) const;
    void RenderCore(const glm::mat4& vp,const glm::dvec3& localCamera,int coreId,double seconds,
        bool physicalObserver=false,const glm::vec3& physicalBeta=glm::vec3(0)) const;
    std::vector<int> NearbyStars(const glm::dvec3& camera) const { return catalogue.Nearby(camera); }
    const Universe::Catalog& Catalogue() const {return catalogue;}
    int NearestStar(const glm::dvec3& camera) const {return catalogue.Nearest(camera);}
    void SetStellarActivity(bool enabled) { stellarActivity.SetEnabled(enabled); }
    void SetStellarCorona(bool enabled) {stellarActivity.SetCoronaEnabled(enabled);}
    void SetMagnitudeLimit(float value){value=glm::clamp(value,-2.f,30.f);if(value!=magnitudeLimit){magnitudeLimit=value;skyCubeDirty=true;}}
    float MagnitudeLimit() const {return magnitudeLimit;}
    void SetWarmStellarColor(bool enabled) { stellarActivity.SetWarmColor(enabled); }
    void InvalidateSky() const { milkyWay.Invalidate(); }
    void ObserveSkyCamera(const glm::dvec3& camera,double dt,float fov,int height) const {milkyWay.ObserveCamera(camera,dt,fov,height);galaxies.ObserveCamera(camera,dt);}
    void SetAdaptiveSkyQuality(bool enabled) const {milkyWay.SetAdaptiveQuality(enabled);galaxies.SetAdaptiveQuality(enabled);}
    unsigned MilkyWayCacheBuilds() const { return milkyWay.CacheBuilds(); }
    bool SkyNeedsFrame(const glm::dvec3& camera) const {return milkyWay.NeedsFrame(camera) || galaxies.NeedsFrame();}
    double MilkyWayCacheTimestamp() const {return milkyWay.CacheTimestamp();}
    float MilkyWayCacheBlend() const {return milkyWay.BlendFraction();}
    float MilkyWayLastDrawBlend() const {return milkyWay.LastDrawBlend();}
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
    static glm::dmat3 PlanetRotationDouble(double seconds,int planet);
    static double PlanetLandingRadius(const glm::dvec3& direction,int planet);
    static constexpr double MillerOceanDepthMetres=700.0;
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
                const glm::vec3& physicalObserverBeta=glm::vec3(0),bool physicalObserver=false,
                bool captureDiskForeground=false,float depthBelowMiller=0) const;
    unsigned int DiskForegroundTexture() const { return atmosphere.DiskForegroundTexture(); }
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
    float galaxyMagnitudeLimit=14;
    std::vector<float> starLuminosities;
    int rayBudget=160;
    float diskOuterRadii=8.f,diskBrightness=6.4f;
    bool lensingDiagnostic=false;
    bool hotFlowDiagnostic=false;
    bool accretionCloudsEnabled=true;
    bool accretionPlaneEnabled=true;
    bool atmospheresEnabled=true;
    bool weatherCloudsEnabled=true;
    bool weatherEnabled=true;
    PlanetWeather::State MillerWeather(double seconds) const;
    std::array<glm::vec2,6> diskAppearance{};
    bool oceanReflectionsEnabled=true;
    float weatherWindScale=1,weatherStormScale=1;
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
    bool skyRefreshDeferred=false;
    mutable size_t lastSubmittedStars=0;
    struct StarBatch {glm::dvec3 center;double radius;float maxLuminosity;int first,count;};
    std::vector<StarBatch> starBatches;
    void BuildSkyCube(const glm::dvec3& origin=glm::dvec3(0)) const;
    void RenderLensing(const glm::mat4& viewProjection,const glm::dvec3& camera,double seconds,const glm::vec3& beta,
                       const glm::vec3& physicalBeta,bool physicalObserver,int coreId=-1) const;
    Mesh sphere;
    Mesh backgroundStars;
    mutable Mesh streamedStars;
    mutable unsigned uploadedStreamRevision=0;
    void UploadStream(const glm::dvec3& camera) const;
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
    Universe::Catalog catalogue;
    Atmosphere atmosphere;
    StellarActivity stellarActivity;
    GalaxyBackground galaxies;
    MilkyWaySky milkyWay;
    KerrWormhole gargantuaWormhole;
    unsigned int solarWormholeSky=0;
    mutable WormholeSkyCache wormholeSkyCache;
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
