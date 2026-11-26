#pragma once
#include "Atmosphere.h"
#include "StellarActivity.h"
#include "SaturnRings.h"
#include "KerrWormhole.h"
#include "PlanetVegetation.h"
#include "PlanetBirds.h"
#include "OceanFish.h"
#include "CameraMotion.h"
#include "WormholeSky.h"
#include "OrbitalMechanics.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <limits>
#include <cstdint>
#include <functional>

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
        enum class Life { None, Microbial, Multicellular };
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
        bool wormhole = false;
        bool metalCore = true,magneticField = false;
        Life life = Life::None;
        bool earthPoles = false;
        bool HasMulticellularLife() const {return life==Life::Multicellular;}
        bool HasSurfaceLiquid() const {
            return !gas && !wormhole && palette==Palette::Climate && (earthPoles || seaLevel>-1);
        }
    };

    static Star DescribeStar(int id, const std::string &name);
    void Generate(int id, const std::string &name);
    void LoadSolarSystem();
    void LoadCore(int id);
    bool IsCore() const {return coreId>=0;}
    bool Initialize();
    void SetStellarActivity(bool enabled) { stellarActivity.SetEnabled(enabled); }
    void SetStellarCorona(bool enabled) {stellarActivity.SetCoronaEnabled(enabled);}
    void SetEarthClouds(bool enabled){earthCloudsEnabled=enabled;}
    void SetOceanReflections(bool enabled){oceanReflectionsEnabled=enabled;}
    void SetWaterRefraction(bool enabled){waterRefractionEnabled=enabled;}
    void SetEarthShoreDetail(bool enabled){earthShoreDetail=enabled;}
    void SetWeather(float windScale,float stormScale){weatherWindScale=windScale;weatherStormScale=stormScale;}
    void SetWeatherEnabled(bool enabled){weatherEnabled=enabled;}
    void SetAurorasEnabled(bool enabled){aurorasEnabled=enabled;}
    void SetVegetationEnabled(bool enabled){vegetationEnabled=enabled;}
    size_t VisibleTrees() const {return vegetation.LastTrees();}
    size_t VisibleGrassSprites() const {return vegetation.LastGrass();}
    std::size_t VisibleBirds() const {return birds.VisibleCount();}
    unsigned VisibleBirdSpecies() const {return birds.VisibleSpecies();}
    void SetBirdsEnabled(bool value){birdsEnabled=value;}
    PlanetWeather::State WeatherState(int planet,double seconds) const;
    glm::dmat3 WeatherFrame(int planet,double seconds) const {return RotationDouble(planet,seconds);}
    void SetWormholeEnabled(bool enabled){wormhole.SetEnabled(enabled);}
    void SetWormholeDiagnostic(bool enabled){wormhole.SetDiagnostic(enabled);}
    static glm::dvec3 SaturnWormholePosition(double seconds);
    using WormholeBackground=std::function<void(const glm::mat4&,const glm::dvec3&)>;
    unsigned WormholeSky(double seconds=0,const WormholeBackground& background={});
    void SetWarmStellarColor(bool enabled) { stellarActivity.SetWarmColor(enabled); }
    void TriggerStellarFlare(double seconds) { stellarActivity.Trigger(seconds); }
    void Release();
    void Update(double seconds, glm::vec3 &front, glm::vec3 &up);
    void Move(GLFWwindow *window, float realSeconds, glm::vec3 &front, glm::vec3 &up);
    void MoveOnSurface(const glm::dvec3& displacementAu,double descentMetres,glm::vec3& front,glm::vec3& up);
    double DepthBelowWater() const {return landed?landing.DepthBelowWater():0;}
    bool CanDive() const {return landed && landingWaterDepth>.40;}
    size_t VisibleFish() const {return fish.LastDrawn();}
    size_t VisibleSharks() const {return fish.LastSharks();}
    size_t VisibleDolphins() const {return fish.LastDolphins();}
    unsigned FishCaught() const {return fish.FishCaught();}
    void SetFishEnabled(bool value){fishEnabled=value;}
    void Render(float fieldOfView, float aspect, const glm::vec3 &front, const glm::vec3 &up, double seconds,
                bool trajectories, bool photonView, bool showSelection = true, unsigned int wormholeSky = 0,
                unsigned reflectionSky = 0, bool captureReflectionSky = false);
#ifdef RENDER_ENGINE_DX12
    bool HasLocalReflectionReceivers(double seconds) const;
#endif
    void RenderOverlays(float fieldOfView,float aspect,const glm::vec3& front,const glm::vec3& up,
                        double seconds,bool trajectories,bool showSelection,int lensProfile=0);
    void GoToSelected(double seconds, glm::vec3 &front, glm::vec3 &up);
    void ToggleLanding(double seconds, glm::vec3 &front, glm::vec3 &up);
    void Overview(glm::vec3 &front, glm::vec3 &up);
    bool Pick(const glm::vec3 &direction, double seconds, double tolerance = .012);
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
    const std::vector<OrbitalMechanics::Orbit>& AsteroidOrbits() const {return asteroids;}
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
    double WaterDepth(int planet,const glm::vec3& localDirection,double seconds) const;
    void UpdateLandingRadius(double seconds,double descentMetres=0);
    void UpdatePatch(const glm::dvec3 &direction,double radiusMetres);
    void BindEarthMaps() const;
    Star star;
    std::vector<Planet> planets;
    std::vector<OrbitalMechanics::Orbit> asteroids;
    bool solarSystem = false;
    bool earthCloudsEnabled=true;
    bool earthMapsLoaded=false;
    unsigned earthReliefTexture=0,earthColorTexture=0;
    unsigned earthDetailTexture=0;
    glm::ivec2 earthDetailOrigin{0};
    bool weatherEnabled=true;
    bool oceanReflectionsEnabled=true;
    bool waterRefractionEnabled=true;
    bool earthShoreDetail=true;
    float weatherWindScale=1,weatherStormScale=1;
    bool aurorasEnabled=true;
    bool vegetationEnabled=true;
    int coreId=-1;
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
    CameraMotion::SurfaceAnchor landing;
    double landingWaterDepth=0;
    bool fishEnabled=true;
    bool birdsEnabled=true;
    int wildlifePlanet=-1;
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
    KerrWormhole wormhole;
    WormholeSkyCache wormholeSkyCache;
    PlanetVegetation vegetation;
    OceanFish fish;
    PlanetBirds birds;
};
