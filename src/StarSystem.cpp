#include "StarSystem.h"
#include "CameraMotion.h"
#include "Astronomy.h"
#include "PlanetTerrain.h"
#include "AssetManager.h"
#include "UniverseCatalog.h"
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include "GalacticCore.h"
#include "WormholePhysics.h"
#include "InterstellarWorlds.h"
#include "LensCalibration.h"

unsigned int compileShaderProgram(const char *, const char *);

namespace
{
double Random(std::mt19937_64 &engine, double lower, double upper)
{
    if(!std::isfinite(lower) || !std::isfinite(upper) || lower>upper)
        throw std::runtime_error("Star generation requested an invalid random range: "+
            std::to_string(lower)+" to "+std::to_string(upper));
    return std::uniform_real_distribution<double>(lower, upper)(engine);
}

} // namespace

StarSystem::Star StarSystem::DescribeStar(int id, const std::string &name)
{
    if(id==InterstellarWorlds::neutronStarId){
        Star result;result.id=id;result.name="Panurge";result.classification="Neutron star / compact companion";result.systemSeed=Universe::SystemSeed(id);
        result.massSolar=InterstellarWorlds::neutronStarMassSolar;
        result.radiusSolar=InterstellarWorlds::neutronStarRadiusMetres/(Astronomy::solarRadiusAu*Astronomy::metresPerAu);
        result.temperature=InterstellarWorlds::neutronStarTemperature;result.ageGyr=1;
        result.luminositySolar=result.radiusSolar*result.radiusSolar*std::pow(result.temperature/5772.,4);return result;
    }
    if(Universe::StarIndex(id)>=Universe::LegacyStarCount){
        const auto p=Universe::Properties(Universe::StarIndex(id));Star result;
        result.id=id;result.name=name;result.systemSeed=Universe::SystemSeed(id);
        result.massSolar=p.massSolar;result.radiusSolar=p.radiusSolar;result.temperature=p.temperature;
        result.luminositySolar=p.luminositySolar;result.ageGyr=p.ageGyr;
        result.classification=std::string(1,p.spectral)+" V procedural main-sequence star";return result;
    }
    if(id>=4 && id<=8) {
        const double temperatures[]={5200,10000,3500,6200,8500};
        const double radii[]={1,2,.65,1.2,1.8};
        const double masses[]={.9,2.4,.4,1.1,1.9};
        const char* types[]={"K0 V main-sequence star","A0 V main-sequence star","M2 V main-sequence star","F8 V main-sequence star","A5 V main-sequence star"};
        Star result;result.id=id;result.name=name;result.classification=types[id-4];
        result.systemSeed=Universe::SystemSeed(id);
        result.temperature=temperatures[id-4];result.radiusSolar=radii[id-4];result.massSolar=masses[id-4];
        result.luminositySolar=result.radiusSolar*result.radiusSolar*std::pow(result.temperature/5772.0,4);
        result.ageGyr=.45*std::min(13.7,10*result.massSolar/result.luminositySolar);
        return result;
    }
    std::mt19937_64 random(Universe::SystemSeed(id));
    Star result;
    result.id = id;
    result.name = name;
    result.systemSeed=Universe::SystemSeed(id);
    const auto population=Universe::IsStar(id)?Universe::StarPopulation(Universe::StarIndex(id)):Universe::Population::LocalDisk;
    const bool oldPopulation=population==Universe::Population::Bulge || population==Universe::Population::Halo;
    double choice = Random(random, 0, 1);
    if(oldPopulation)choice*=.90;
    else if(population==Universe::Population::SpiralArm && choice>.92)choice=Random(random,.985,1);
    char spectral;
    if (choice < 0.55)
    {
        spectral = 'M';
        result.massSolar = Random(random, 0.10, 0.55);
        result.temperature = Random(random, 2700, 3900);
    }
    else if (choice < 0.78)
    {
        spectral = 'K';
        result.massSolar = Random(random, 0.55, 0.85);
        result.temperature = Random(random, 3900, 5200);
    }
    else if (choice < 0.90)
    {
        spectral = 'G';
        result.massSolar = Random(random, 0.85, 1.10);
        result.temperature = Random(random, 5200, 6000);
    }
    else if (choice < 0.96)
    {
        spectral = 'F';
        result.massSolar = Random(random, 1.10, 1.50);
        result.temperature = Random(random, 6000, 7500);
    }
    else if (choice < 0.985)
    {
        spectral = 'A';
        result.massSolar = Random(random, 1.50, 2.50);
        result.temperature = Random(random, 7500, 10000);
    }
    else if (choice < 0.998)
    {
        spectral = 'B';
        result.massSolar = Random(random, 2.50, 12.0);
        result.temperature = Random(random, 10000, 30000);
    }
    else
    {
        spectral = 'O';
        result.massSolar = Random(random, 15, 35);
        result.temperature = Random(random, 30000, 45000);
    }
    result.radiusSolar = std::pow(result.massSolar, 0.8);
    result.luminositySolar = result.radiusSolar * result.radiusSolar * std::pow(result.temperature / 5772.0, 4);
    const double lifetime=10.0*result.massSolar/result.luminositySolar;
    result.ageGyr = oldPopulation?std::min(Random(random,8,13.5),lifetime*.90)
        :population==Universe::Population::SpiralArm?std::min(Random(random,.005,.15),lifetime*.8)
        :Random(random,.02,.90)*std::min(13.7,lifetime);
    const double lower=spectral=='M'?2700:spectral=='K'?3900:spectral=='G'?5200:spectral=='F'?6000:spectral=='A'?7500:spectral=='B'?10000:30000;
    const double upper=spectral=='M'?3900:spectral=='K'?5200:spectral=='G'?6000:spectral=='F'?7500:spectral=='A'?10000:spectral=='B'?30000:45000;
    const int subclass=std::clamp(static_cast<int>(10*(upper-result.temperature)/(upper-lower)),0,9);
    result.classification = std::string(1, spectral) + std::to_string(subclass) + " V main-sequence star";
    return result;
}

void StarSystem::Generate(int id, const std::string &name)
{
    coreId=-1;
    InvalidateCaches();
    solarSystem = false;
    asteroids.clear();
    star = DescribeStar(id, name);
    std::mt19937_64 random(star.systemSeed ^ 0x4f6cdd1dULL);
    // System architecture is chosen once, then composition depends on the
    // frost line and migration. There is no mandatory rock/giant ordering.
    const int architecture=static_cast<int>(random()%5);
    const int count=architecture==0?1+random()%7:architecture==1?5+random()%5:
        architecture==2?2+random()%5:architecture==3?2+random()%6:1+random()%10;
    planets.clear();
    const double frostLine=2.7*std::sqrt(star.luminositySolar);
    const double plane=Random(random,-.5,.5),node=Random(random,0,glm::two_pi<double>());
    double axis = std::max(star.radiusSolar * Astronomy::solarRadiusAu * 12,
        std::sqrt(star.luminositySolar)*Random(random,architecture==2?.2:.035,architecture==2?1.8:.45));
    for (int index = 0; index < count; ++index)
    {
        Planet planet;
        planet.name = name + " " + static_cast<char>('b' + index);
        const double composition=Random(random,0,1);
        const double giantChance=architecture==0?.025:architecture==2?.68:
            axis>frostLine?.48:.08;
        const double subNeptuneChance=architecture==0?.08:axis>frostLine?.35:.22;
        const int category=composition<giantChance?2:composition<giantChance+subNeptuneChance?1:0;
        planet.gas = category > 0;
        if (category == 0)
        {
            planet.radiusEarth = Random(random, 0.18, 1.95);
            planet.massEarth = std::pow(planet.radiusEarth,3.4)*Random(random,.65,1.35);
            planet.classification = planet.radiusEarth<.45?"Dwarf terrestrial planet":planet.radiusEarth > 1.3 ? "Super-Earth" : "Terrestrial planet";
        }
        else if (category == 1)
        {
            planet.radiusEarth = Random(random, 1.9, 4.8);
            planet.massEarth = std::pow(planet.radiusEarth,1.7)*Random(random,1.5,2.7);
            planet.classification = axis>frostLine?"Ice giant":"Sub-Neptune";
        }
        else
        {
            planet.massEarth=std::exp(Random(random,std::log(55.0),std::log(1500.0)));
            planet.radiusEarth=std::clamp(8+2*std::log10(planet.massEarth/55.0)+Random(random,-1,1),6.0,14.0);
            planet.classification=axis<frostLine*.2?"Hot gas giant":"Gas giant";
        }
        const double eccentricity=Random(random,.002,architecture==0?.045:architecture==3?.24:.12);
        if(index>0){
            const auto& previous=planets.back();
            const double hill=std::min(.80,4*std::cbrt((previous.massEarth+planet.massEarth)/(3*332946.0487*star.massSolar)));
            axis=std::max({axis,previous.orbit.semiMajorAxis*(1+previous.orbit.eccentricity)/(1-eccentricity)*1.15,
                previous.orbit.semiMajorAxis*(1+hill)/(1-hill)});
        }
        planet.orbit = {axis,eccentricity,plane+Random(random,-.035,.035),node,
                        Random(random,0,glm::two_pi<double>())};
        const double albedo = Random(random, 0.1, 0.65);
        planet.temperature = 278.5 * std::pow(star.luminositySolar * (1.0 - albedo), 0.25) / std::sqrt(axis);
        planet.daySeconds = Random(random, 7, 70) * 3600;
        if(axis<std::sqrt(star.luminositySolar)*.15)
            planet.daySeconds=glm::two_pi<double>()/std::sqrt(Astronomy::solarGravityAu*star.massSolar/(axis*axis*axis));
        planet.tilt = static_cast<float>(Random(random, -0.7, 0.7));
        planet.seed = static_cast<unsigned int>(random() & 0x00ffffffu);
        planet.seaLevel = planet.temperature > 160 && planet.temperature < 420
                              ? static_cast<float>(Random(random, -0.18, 0.22))
                              : -1.0f;
        const double escapeKm=11.186*std::sqrt(planet.massEarth/planet.radiusEarth);
        planet.atmosphere=planet.gas || (escapeKm>4 && planet.temperature<850 && Random(random,0,1)>.18);
        planet.color = glm::vec3(Random(random, .12, .65), Random(random, .15, .65), Random(random, .12, .65));
        planet.magneticField=planet.gas || (planet.radiusEarth>.5 && star.ageGyr<9 && std::abs(planet.daySeconds)<20*86400);
        if(!planet.gas && planet.atmosphere && planet.temperature>275 && planet.temperature<310 && planet.seaLevel>-.18f){
            const float biosphere=PlanetVegetation::Unit(planet.seed+991);
            if(biosphere<.58f)planet.life=Planet::Life::Multicellular;
            else if(biosphere<.82f)planet.life=Planet::Life::Microbial;
        }
        planets.push_back(planet);
        axis *= Random(random,architecture==0?1.3:1.5,architecture==3?3.0:2.4);
    }
    // Moons are generated after primary planets so parents precede children
    // in the existing position cache. Their orbits stay inside 22% of the
    // parent's pericentre Hill radius and outside a conservative Roche bound.
    for(int parent=0;parent<count;++parent){
        const auto host=planets[parent];
        const int moons=host.gas?static_cast<int>(random()%4):(random()%7==0?1:0);
        const double outer=host.orbit.semiMajorAxis*(1-host.orbit.eccentricity)*
            std::cbrt(host.massEarth/(3*332946.0487*star.massSolar))*.22;
        double moonAxis=host.radiusEarth*Astronomy::earthRadiusAu*Random(random,3.2,4.5);
        for(int index=0;index<moons && moonAxis<outer;++index){
            Planet moon{};moon.name=host.name+" - moon "+std::to_string(index+1);moon.parent=parent;
            const double maximumMoonRadius=std::min(.72,host.radiusEarth*.27);
            moon.radiusEarth=Random(random,std::max(.01,maximumMoonRadius*.16),maximumMoonRadius);
            moon.massEarth=std::pow(moon.radiusEarth,3)*Random(random,.35,.8);
            moon.temperature=host.temperature;moon.classification=moon.temperature<180?"Icy moon":"Rocky moon";
            moon.orbit={moonAxis,Random(random,.001,.025),host.orbit.inclination+Random(random,-.08,.08),node,Random(random,0,glm::two_pi<double>())};
            moon.daySeconds=glm::two_pi<double>()/std::sqrt(Astronomy::solarGravityAu*host.massEarth/332946.0487/(moonAxis*moonAxis*moonAxis));
            moon.seed=static_cast<unsigned>(random()&0x00ffffff);moon.seaLevel=-1;moon.gas=false;
            moon.atmosphere=false;moon.tilt=static_cast<float>(Random(random,-.15,.15));
            moon.color=moon.temperature<180?glm::vec3(.55,.59,.63):glm::vec3(.34,.31,.28);
            planets.push_back(moon);moonAxis*=Random(random,1.8,2.6);
        }
    }
    selected = -1;
    landed = following = followingRotation = false;
    lastTime = 0;
    camera = glm::dvec3(0, 0, std::max(0.1, star.radiusSolar * Astronomy::solarRadiusAu * 8));
    speedAu = 0.1;
}

void StarSystem::LoadCore(int id){
    const auto core=GalacticCores::Get(id);InvalidateCaches();coreId=id;solarSystem=false;
    planets.clear();asteroids.clear();selected=landedPlanet=-1;landed=following=followingRotation=false;
    star={id,core.name,core.classification,core.massSolar,core.RadiusAu()/Astronomy::solarRadiusAu,0,0,0};
    camera={0,core.RadiusAu()*3,core.RadiusAu()*14};speedAu=core.RadiusAu()*.2;
}

glm::dvec3 StarSystem::Position(int planet, double seconds) const
{
    if (planet < 0)
        return glm::dvec3(0);
    if(cachedPositionTime!=seconds || cachedPositions.size()!=planets.size()) {
        cachedPositions.resize(planets.size());
        for(int index=0;index<static_cast<int>(planets.size());++index) {
            ++positionEvaluations;
            cachedPositions[index]=RelativePosition(index,seconds);
            if(planets[index].parent>=0)cachedPositions[index]+=cachedPositions[planets[index].parent];
        }
        cachedPositionTime=seconds;
    }
    return cachedPositions[planet];
}

void StarSystem::InvalidateCaches() {
    birds.Reset();
    fish.Reset();
    wildlifePlanet=-1;
    cachedPositionTime=cachedAsteroidTime=std::numeric_limits<double>::quiet_NaN();
    cachedPositions.clear();cachedOrbits.clear();cachedAsteroids.clear();
}

double StarSystem::DetailRadiusAu() const {
    if(IsCore())return GalacticCores::Get(coreId).RadiusAu()*10000;
    double radius=20;
    for(const auto& planet:planets)radius=std::max(radius,planet.orbit.semiMajorAxis*(1+planet.orbit.eccentricity)*3);
    return radius;
}

double StarSystem::GravityFor(int planet) const
{
    return Astronomy::solarGravityAu * (planets[planet].parent < 0
                                            ? star.massSolar
                                            : planets[planets[planet].parent].massEarth * 5.97217e24 / 1.98847e30);
}

glm::dvec3 StarSystem::RelativePosition(int planet, double seconds) const
{
    const auto p = OrbitalMechanics::PositionAtTimeDouble(planets[planet].orbit, GravityFor(planet), seconds);
    return {p.x, p.z, -p.y}; // Ecliptic north is the camera's +Y axis.
}

glm::mat4 StarSystem::Rotation(int planet, double seconds) const
{
    return glm::mat4(glm::mat3(RotationDouble(planet,seconds)));
}

glm::dmat3 StarSystem::RotationDouble(int planet,double seconds) const {
    if(planet>=0 && planets[planet].wormhole)return glm::dmat3(1); // Stationary axisymmetric throat, no material surface spin.
    const double period=planet<0?25.38*Astronomy::secondsPerDay:planets[planet].daySeconds;
    const double tilt=planet<0?0:planets[planet].tilt;
    auto rotation=glm::rotate(glm::dmat4(1),tilt,glm::dvec3(1,0,0));
    return glm::dmat3(glm::rotate(rotation,std::remainder(seconds/period,1.0)*glm::two_pi<double>(),glm::dvec3(0,1,0)));
}

PlanetWeather::State StarSystem::WeatherState(int planet,double seconds) const {
    const auto& body=planets[planet];
    const PlanetWeather::Profile profile{body.seed,static_cast<float>(body.radiusEarth*6371000),body.seaLevel,
        static_cast<float>(body.temperature),solarSystem && planet==2?8.f:7.f+5*PlanetWeather::Unit(body.seed+17),false,
        body.atmosphere && !body.gas && !body.wormhole && body.temperature>240 && body.temperature<335,
        body.metalCore,body.magneticField && aurorasEnabled,body.atmosphere && !body.wormhole,false,body.earthPoles,!solarSystem};
    return PlanetWeather::Make(profile,seconds,weatherEnabled?weatherWindScale:0,weatherEnabled?weatherStormScale:0);
}

double StarSystem::SurfaceRadius(int planet, const glm::dvec3 &direction, double seconds) const
{
    const auto &body = planets[planet];
    const auto local=glm::vec3(glm::normalize(glm::transpose(RotationDouble(planet,seconds))*direction));
    const double radius = body.radiusEarth * Astronomy::earthRadiusAu;
    if (body.gas || body.wormhole)
        return radius;
    const auto weather=WeatherState(planet,seconds);
    return radius * (1.0+PlanetTerrain::SurfaceElevation(
                        local, body.seed, static_cast<float>(std::remainder(seconds, glm::two_pi<double>())),
                        body.seaLevel, 0.003f, static_cast<float>(2.0 / (body.radiusEarth * 6371000.0)),
                        false,&weather,body.earthPoles,body.earthPoles?PlanetTerrain::ArcticWinter(seconds):0,!solarSystem));
}

double StarSystem::WaterDepth(int planet,const glm::vec3& localDirection,double seconds) const
{
    const auto& body=planets[planet];
    if(!body.HasSurfaceLiquid())return 0;
    return PlanetTerrain::WaterDepthMetres(localDirection,body.seed,body.seaLevel,
        body.radiusEarth*Astronomy::earthRadiusAu*Astronomy::metresPerAu,
        body.earthPoles,body.earthPoles?PlanetTerrain::ArcticWinter(seconds):0,!solarSystem);
}

void StarSystem::UpdateLandingRadius(double seconds,double descentMetres)
{
    const auto& body=planets[landedPlanet];
    const glm::vec3 direction(landing.direction);
    const float elevation=PlanetTerrain::SurfaceElevation(direction,body.seed,0,body.seaLevel,.003f,0,
        false,nullptr,body.earthPoles,0,!solarSystem);
    landingWaterDepth=WaterDepth(landedPlanet,direction,seconds);
    landing.SetSurfaceRadius(body.radiusEarth*Astronomy::earthRadiusAu*(1.0+elevation),landingWaterDepth,descentMetres);
}

void StarSystem::Update(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    if (landed)
    {
        landing.CaptureLook(RotationDouble(landedPlanet,lastTime),front);
        if(planets[landedPlanet].earthPoles && DepthBelowWater()>0 && seconds!=lastTime)
            UpdateLandingRadius(seconds);
        landing.Place(RotationDouble(landedPlanet,seconds),Position(landedPlanet,seconds),camera,front,up);
    }
    else if (following)
    {
        const auto position = Position(selected, seconds);
        if (followingRotation) {
            const auto rotation=RotationDouble(selected,seconds);
            CameraMotion::Carry(camera,front,up,lastFollowPosition,position,rotation*glm::transpose(lastFollowRotation));
            lastFollowRotation=rotation;
        } else camera += position - lastFollowPosition;
        lastFollowPosition = position;
    }
    lastTime = seconds;
}

void StarSystem::Move(GLFWwindow *window, float realSeconds, glm::vec3 &front, glm::vec3 &up)
{
    double distance = speedAu * realSeconds;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        distance *= 5;
    const auto right = glm::normalize(glm::cross(front, up));
    glm::dvec3 movement(0);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        movement += glm::dvec3(front);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        movement -= glm::dvec3(front);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        movement += glm::dvec3(right);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        movement -= glm::dvec3(right);
    if(landed)
    {
        const bool modifiers=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS
            || glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
        const double descent=!modifiers?2.5*realSeconds*
            ((glfwGetKey(window,GLFW_KEY_F)==GLFW_PRESS)-(glfwGetKey(window,GLFW_KEY_R)==GLFW_PRESS)):0;
        if(DepthBelowWater()>0)distance=std::min(distance,4.0*realSeconds/Astronomy::metresPerAu);
        MoveOnSurface(distance*movement,descent,front,up);
        return;
    }
    const bool shift=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS
        || glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS || glfwGetKey(window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !shift)
        movement += glm::dvec3(up);
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !shift)
        movement -= glm::dvec3(up);
    camera += distance * movement;
    const double hostRadius=star.radiusSolar*Astronomy::solarRadiusAu;
    if (!IsCore() && glm::length(camera)<hostRadius+Astronomy::observerClearanceAu)
        camera=(glm::length(camera)>0?glm::normalize(camera):glm::dvec3(0,0,1))
            *(hostRadius+Astronomy::observerClearanceAu);
    if(InDetailRange())
    {
        for (int planet = 0; planet < static_cast<int>(planets.size()); ++planet)
        {
            if(planets[planet].wormhole)continue; // Portal crossing is handled by camera transport.
            const auto delta = camera - Position(planet, lastTime);
            const double radius = planets[planet].radiusEarth * Astronomy::earthRadiusAu;
            const double distanceToCentre = glm::length(delta);
            if (distanceToCentre < radius * 1.01)
            {
                const auto radial = distanceToCentre > 0 ? delta / distanceToCentre : glm::dvec3(0, 0, 1);
                const double ground = SurfaceRadius(planet, radial, lastTime) + Astronomy::observerClearanceAu;
                if (distanceToCentre < ground)
                    camera = Position(planet, lastTime) + radial * ground;
            }
        }
    }
}

void StarSystem::MoveOnSurface(const glm::dvec3& displacementAu,double descentMetres,glm::vec3& front,glm::vec3& up)
{
    if(!landed || (displacementAu==glm::dvec3(0) && (descentMetres==0 || !CanDive())))return;
    const auto rotation=RotationDouble(landedPlanet,lastTime);
    landing.CaptureLook(rotation,front);
    if(displacementAu!=glm::dvec3(0))landing.Walk(rotation,displacementAu);
    UpdateLandingRadius(lastTime,descentMetres);
    landing.Place(rotation,Position(landedPlanet,lastTime),camera,front,up);
}

void StarSystem::GoToSelected(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    if(IsCore()){Overview(front,up);return;}
    landed = following = followingRotation = false;
    const auto target = Position(selected, seconds);
    const double radius = selected < 0 ? star.radiusSolar * Astronomy::solarRadiusAu
                                       : planets[selected].radiusEarth * Astronomy::earthRadiusAu;
    auto offset = camera - target;
    if (glm::length(offset) < radius)
        offset = glm::dvec3(0, 0, 1);
    const double framing=selected>=0 && planets[selected].wormhole?12.0:5.0;
    camera = target + glm::normalize(offset) * radius * framing;
    front = glm::normalize(glm::vec3(target - camera));
    up = glm::vec3(0, 1, 0);
    speedAu = radius * 0.5;
    lastTime = seconds;
}

void StarSystem::Overview(glm::vec3 &front, glm::vec3 &up)
{
    if(IsCore()){
        const auto core=GalacticCores::Get(coreId);const auto radius=core.RadiusAu();
        camera=core.hotFlow?GalacticCores::HotFlowFrame()*glm::dvec3(0,52*radius,24*radius)
            :core.quasar?InterstellarWorlds::Orientation()*glm::dvec3(0,26*radius,20*radius):glm::dvec3(0,radius*3,radius*14);
        front=glm::normalize(glm::vec3(-camera));up=core.hotFlow?glm::vec3(GalacticCores::HotFlowFrame()[2]):core.quasar?glm::vec3(InterstellarWorlds::Orientation()[2]):glm::vec3(0,1,0);
        if(core.hotFlow)CameraMotion::Roll(front,up,glm::radians(-24.f));
        if(coreId==GalacticCores::tonId)CameraMotion::Roll(front,up,glm::radians(16.f));return;
    }
    landed = following = followingRotation = false;
    double outer = 0;
    for (const auto &body : planets)
        if (body.parent < 0)
            outer = std::max(outer, body.orbit.semiMajorAxis * (1 + body.orbit.eccentricity));
    camera = glm::dvec3(0, outer * 1.4, outer * 2.0);
    front = glm::normalize(glm::vec3(-camera));
    up = glm::vec3(0, 1, 0);
    speedAu = outer * 0.3;
}

void StarSystem::ToggleLanding(double seconds, glm::vec3 &front, glm::vec3 &up)
{
    if (landed)
    {
        camera+=glm::dvec3(up)*(1000.0+DepthBelowWater())/Astronomy::metresPerAu;
        landed = false;
        return;
    }
    if (selected < 0 || planets[selected].gas || planets[selected].wormhole)
        return;
    landedPlanet = selected;
    landed = true;
    following = false;
    const auto centre=Position(landedPlanet,seconds);
    const auto rotation=RotationDouble(landedPlanet,seconds);
    landing.Set(rotation,camera-centre,front);
    UpdateLandingRadius(seconds);
    up=glm::vec3(rotation*landing.direction);
    auto horizon = glm::vec3(-centre);
    horizon -= up * glm::dot(horizon, up);
    if (glm::length(horizon) < .000001f)
        horizon = glm::cross(up, std::abs(up.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    front = glm::normalize(horizon);
    landing.CaptureLook(rotation,front);
    landing.Place(rotation,centre,camera,front,up);
    speedAu = 2.0 / Astronomy::metresPerAu;
    lastTime = seconds;
}

void StarSystem::ToggleFollow(double seconds)
{
    following = !following;
    followingRotation=false;
    landed = false;
    lastFollowPosition = Position(selected, seconds);
}

void StarSystem::ToggleRotationFollow(double seconds) {
    followingRotation=!(following && followingRotation);
    following=followingRotation;
    landed=false;
    lastFollowPosition=Position(selected,seconds);
    lastFollowRotation=RotationDouble(selected,seconds);
    lastTime=seconds;
}

bool StarSystem::Pick(const glm::vec3 &direction, double seconds, double tolerance)
{
    double best = 1;
    int picked=-2;
    for (int body = -1; body < static_cast<int>(planets.size()); ++body)
    {
        const auto offset = Position(body, seconds) - camera;
        const double radius = body < 0 ? star.radiusSolar * Astronomy::solarRadiusAu
                                       : planets[body].radiusEarth * Astronomy::earthRadiusAu;
        const double distance = glm::length(offset);
        const double along = glm::dot(offset, glm::dvec3(direction));
        if (along <= 0 || distance == 0)
            continue;
        const double score =
            glm::length(offset - along * glm::dvec3(direction)) / distance / std::max(tolerance, radius / distance);
        if (score < best)
        {
            best = score;
            picked=body;
        }
    }
    if(picked==-2)return false;
    if(selected!=picked){following=false;followingRotation=false;}
    selected=picked;
    return true;
}

double StarSystem::ObserverClockRate(double seconds) const {
    if(IsCore())return std::sqrt(std::max(0.0,1-GalacticCores::Get(coreId).RadiusAu()/std::max(glm::length(camera),1e-15)));
    if(!InDetailRange())return 1;
    const double c2=Astronomy::lightSpeedAu*Astronomy::lightSpeedAu;
    double potential=star.massSolar*Astronomy::solarGravityAu/std::max(glm::length(camera),star.radiusSolar*Astronomy::solarRadiusAu);
    for(int i=0;i<static_cast<int>(planets.size());++i){
        const auto& body=planets[i];
        const double distance=std::max(glm::length(camera-Position(i,seconds)),body.radiusEarth*Astronomy::earthRadiusAu);
        potential+=Astronomy::solarGravityAu*(body.massEarth/332946.0487)/distance;
    }
    glm::dvec3 velocity(0);
    const int carried=landed?landedPlanet:following?selected:-1;
    if(carried>=0){
        for(int body=carried;body>=0;body=planets[body].parent){
            const auto v=glm::dvec3(OrbitalMechanics::VelocityAtTime(planets[body].orbit,GravityFor(body),seconds));
            velocity+=glm::dvec3(v.x,v.z,-v.y);
        }
        if((landed || followingRotation) && !planets[carried].wormhole){
            const auto axis=RotationDouble(carried,seconds)*glm::dvec3(0,1,0);
            velocity+=glm::cross(axis,camera-Position(carried,seconds))*(glm::two_pi<double>()/planets[carried].daySeconds);
        }
    }
    return std::sqrt(std::max(0.0,1-2*potential/c2))*std::sqrt(std::max(0.0,1-glm::dot(velocity,velocity)/c2));
}

std::string StarSystem::Information(double seconds) const
{
    if(IsCore())return GalacticCores::Information(coreId);
    std::ostringstream text;
    text<<std::setprecision(12)<<"Camera clock / distant clock: "<<ObserverClockRate(seconds)<<"\n";
    text << star.name << "\n"
         << star.classification << "\n\n"
         << std::fixed << std::setprecision(3) << "Age  " << star.ageGyr << " billion years\n"
         << "Mass  " << star.massSolar << " solar\nRadius  " << star.radiusSolar << " solar\n"
         << std::setprecision(0) << "Temperature  " << star.temperature << " K\n"
         << "Objects  " << planets.size() << "\n";
    if (selected >= 0)
    {
        const auto &body = planets[selected];
        if(body.wormhole){
            text<<"\n"<<body.name<<"\n"<<body.classification
                <<"\nParent  Saturn\nThroat radius  "<<WormholePhysics::ThroatRadiusKm<<" km"
                <<"\nSpin a/M  "<<WormholePhysics::Spin<<"\nDeformation  "<<WormholePhysics::Deformation
                <<"\nSaturn orbit  "<<WormholePhysics::OrbitRadiusKm<<" km"
                <<"\nPeriod  "<<glm::two_pi<double>()/OrbitalMechanics::MeanMotion(GravityFor(selected),body.orbit.semiMajorAxis)/86400<<" days"
                <<"\n\nFictional orbit; theoretical geometry\nConnected to Gargantua Wormhole\nFly through the throat to cross\nNo solid surface or event horizon\n";
            text<<(following?"\nFollowing orbit":"\nFree camera");
            return text.str();
        }
        text << "\n"
             << body.name << "\n"
             << body.classification << "\n"
             << std::setprecision(2) << "Radius  " << body.radiusEarth << " Earth\n"
             << "Mass  " << body.massEarth << " Earth\n"
             << std::setprecision(3) << "Orbit  " << body.orbit.semiMajorAxis << " AU\n"
             << std::setprecision(1) << "Period  "
             << glm::two_pi<double>() / OrbitalMechanics::MeanMotion(GravityFor(selected), body.orbit.semiMajorAxis) /
                    86400
             << " days\n"
             << "Temperature  " << body.temperature << " K\n";
        if(body.HasMulticellularLife())text<<"Multicellular life\nLocal vegetation: forests and grassland\n";
        else if(body.life==Planet::Life::Microbial)text<<"Microbial life\n";
    }
    text         << (landed      ? "\nStanding on surface"
             : following ? (followingRotation?"\nFollowing translation and rotation":"\nFollowing position")
                         : "\nFree camera");
    if(CanDive()){
        if(DepthBelowWater()>0)text<<"\nUnderwater  "<<DepthBelowWater()<<" m";
        text<<"\nF dive   R rise";
    }
    if(landed)text<<"\nHigh-detail surface: 100 m around camera\nCoarser terrain beyond 100 m\n";
    return text.str();
}

bool StarSystem::Initialize()
{
    const std::string vertex = std::string("#version 330 core\n") + PlanetTerrain::ShaderFunctions() + R"(
layout(location=0) in vec3 position;
uniform mat4 model;
uniform mat4 viewProjection;
uniform int bodyKind;
uniform float phase;
uniform float bodyRadiusKm;
uniform bool seabedRendering;
uniform bool guideOverlay;
uniform int guideDistortionModel;
uniform vec3 guideDistortion;
uniform vec2 guideRadiusScale;
out vec3 localPosition;
out vec3 relativePosition;
out vec3 patchDeltaMetres;
vec4 guideProjection(vec4 clip){
    if(guideOverlay && clip.w>0.0){
        float radius=length(clip.xy/clip.w*guideRadiusScale),factor=1.0;
        if(guideDistortionModel==1)factor=1.0-guideDistortion.x+guideDistortion.x*radius*radius;
        else if(guideDistortionModel==2)factor=((guideDistortion.x*radius+guideDistortion.y)*radius+guideDistortion.z)*radius+1.0-guideDistortion.x-guideDistortion.y-guideDistortion.z;
        clip.xy*=factor;
    }
    return clip;
}
void main() {
    patchDeltaMetres=vec3(0);
    if(bodyKind==1 && patchDrawing){
        localPosition=surfacePatchDirection(position.xy);
        float height=terrainHeight(localPosition);
        float elevation=seabedRendering?seabedElevation(localPosition,height,bodyRadiusKm*1000.0)
            :surfaceElevation(localPosition,height,phase,false);
        patchDeltaMetres=surfacePatchDelta(position.xy,elevation);
        relativePosition=surfacePatchRelative(patchDeltaMetres);
        gl_Position=viewProjection*vec4(relativePosition,1);return;
    }
    localPosition = position;
    if (bodyKind == 4) {
        relativePosition = vec3(0);
        gl_Position = guideProjection(vec4(position.xy,0,1));
        return;
    }
    vec3 displaced = position;
    if (bodyKind == 1){
        vec3 radial=normalize(position);
        float height=terrainHeight(radial);
        displaced=seabedRendering?radial*(1.0+seabedElevation(radial,height,bodyRadiusKm*1000.0))
            :planetSurface(radial,height,phase);
    }
    relativePosition = (model * vec4(displaced,1)).xyz;
    gl_Position = guideProjection(viewProjection * vec4(relativePosition,1));
}
)";
    const std::string fragment =
        std::string("#version 330 core\nconst float horizonRadius=0.0; const vec3 observerBeta=vec3(0);\n") + PlanetVegetation::ShaderFunctions() +
        PlanetTerrain::ShaderFunctions() + PlanetTerrain::SurfaceDetailShaderFunctions() + OrbitalMechanics::RelativityShaderFunctions() + StellarActivity::ShaderFunctions() + SaturnRings::ShaderFunctions() + R"(
in vec3 localPosition;
in vec3 relativePosition;
in vec3 patchDeltaMetres;
out vec4 color;
uniform int bodyKind;
uniform mat4 model;
uniform vec3 starPosition;
uniform vec3 albedo;
uniform float temperature;
uniform float phase;
uniform bool patchEnabled;
uniform bool saturnShadow;
uniform int surfacePalette;
uniform float bodyRadiusKm;
uniform bool lifeBearing;
uniform bool waterRefraction;
uniform bool earthShoreDetail;
uniform bool seabedRendering;
uniform sampler2D earthColorMap;

vec3 refractedSeabed(vec3 waterColor,vec3 radial,vec3 normal,float height){
    float radiusMetres=bodyRadiusKm*1000.0;
    float depth=liquidDepthMetres(radial,height,radiusMetres);
    if(depth>=45.0)return waterColor;
    vec3 localView=normalize(transpose(mat3(model))*(-relativePosition));
    vec3 downward=refract(-localView,normal,1.0/1.333);
    float travel=depth/max(.22,-dot(downward,radial));
    vec3 surface=radial*radiusMetres;
    if(patchDrawing){
        surface=patchTextureOrigin+patchRight*patchDeltaMetres.x
            +patchUp*patchDeltaMetres.y+patchCenter*patchDeltaMetres.z;
    }
    vec3 bottom=surface+downward*travel;
    float footprint=max(length(dFdx(bottom)),length(dFdy(bottom)));
    float detail=1.0-smoothstep(.7,5.0,footprint);
    float sediment=mix(.5,groundNoise(bottom*.65),detail);
    vec3 seabed=mix(vec3(.18,.24,.19),vec3(.51,.46,.29),sediment);
    vec3 transmission=exp(-travel*vec3(.16,.080,.058));
    return mix(waterColor,mix(waterColor,seabed,transmission),1.0-smoothstep(26.0,45.0,depth));
}

void main() {
    if (bodyKind == 4) {
        color=vec4(albedo,0); gl_FragDepth=0; return;
    }
    gl_FragDepth=log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12);
    if (bodyKind == 3) { color=vec4(albedo,0); return; }
    vec3 radial=normalize(localPosition);
    if (bodyKind == 0) {
        float mu=dot(normalize(mat3(model)*radial),normalize(-relativePosition));
        color=vec4(stellarSurface(radial,blackbody(temperature),mu,temperature),1);
        return;
    }
    if (patchEnabled) {
        bool inside=dot(radial,patchCenter)>cos(0.020);
        if (inside != patchDrawing) discard;
    }
    vec3 normal=radial;
    vec3 material=albedo;
    float water=0, shoreFoam=0;
    if (bodyKind == 1) {
        float height=terrainHeight(radial);
        water=liquidCoverage(radial,height);
        vec3 surface=seabedRendering?radial*(1.0+seabedElevation(radial,height,bodyRadiusKm*1000.0))
            :planetSurface(radial,height,phase);
        // Use the continuous height field to shade larger land features.
        // The nearby mesh derivatives preserve metre-scale water and ground normals.
        bool localDetail=(water>.01 && !seabedRendering) || length(relativePosition)*149597870700.0<110.0;
        vec3 slopePoint=surface;
        if(patchDrawing && localDetail)slopePoint=patchDeltaMetres;
        normal=normalize(cross(dFdx(slopePoint),dFdy(slopePoint)));
        if (dot(normal,radial)<0) normal=-normal;
        if(seabedRendering && water>.5){
            vec3 metres=patchDrawing?patchTextureOrigin+patchRight*patchDeltaMetres.x+
                patchUp*patchDeltaMetres.y+patchCenter*patchDeltaMetres.z:radial*bodyRadiusKm*1000.0;
            float detail=1.0-smoothstep(.45,3.0,max(length(dFdx(metres)),length(dFdy(metres))));
            float sediment=mix(.5,groundNoise(metres*.39),detail);
            vec3 floorColor=mix(vec3(.12,.20,.16),vec3(.51,.42,.29),sediment);
            groundDetail(floorColor,normal,patchDeltaMetres,0.0,length(relativePosition)*149597870700.0,phase);
            vec3 worldNormal=normalize(mat3(model)*normal);
            float daylight=max(0.0,dot(worldNormal,normalize(starPosition-relativePosition)));
            color=vec4(floorColor*(.07+.62*daylight),0);return;
        }
        if(surfacePalette==1){
            // Lunar albedo is independent of elevation: seeded dark maria and
            // bright highlands share a neutral regolith palette at every LOD.
            float maria=smoothstep(-.12,.18,terrainFbm(radial*3.1+vec3(17,53,29)));
            material=albedo*mix(.46,1.12,maria);
            material*=.90+.15*(.5+.5*terrainNoise(radial*120.0));
            water=0.0;
        }else{
        float oceanCoverage=surfaceWaterCoverage(radial,height);
        if(!earthMapped && !earthPoles){
            float shoreWidth=max(.000015,fwidth(height));
            oceanCoverage=1.0-smoothstep(seaLevel-shoreWidth,seaLevel+shoreWidth,height);
            water=oceanCoverage;
        }
        if(earthMapped){
            material=pow(texture(earthColorMap,earthMapUv(radial)).rgb,vec3(2.2));
            vec4 geography=earthGeography(radial);
            float coast=geography.b;
            vec3 dx=dFdx(relativePosition)*149597870700.0,dy=dFdy(relativePosition)*149597870700.0;
            float footprint=max(length(dx),length(dy));
            float detail=(1.0-smoothstep(12.0,85.0,footprint))
                *(1.0-smoothstep(.70,.88,abs(radial.y)))*(1.0-smoothstep(.08,.36,geography.a))
                *(1.0-smoothstep(8500.0,16000.0,length(relativePosition)*149597870700.0));
            vec3 ocean=mix(material,vec3(.004,.028,.076),.65);
            if(earthShoreDetail && abs(coast-.5)<.11 && detail>0.001){
                vec3 east=normalize(cross(vec3(0,1,0),radial));
                vec3 north=cross(radial,east);
                float offset=36.0/(bodyRadiusKm*1000.0);
                vec2 change=vec2(
                    earthGeography(normalize(radial+east*offset)).b-earthGeography(normalize(radial-east*offset)).b,
                    earthGeography(normalize(radial+north*offset)).b-earthGeography(normalize(radial-north*offset)).b);
                // Estimate metres from the mapped shoreline without screen-space quantization.
                float coastDistance=(.5-coast)*72.0/max(length(change),.0001);
                float elevation=earthMappedElevation(radial,geography);
                float slope=1.0-clamp(dot(radial,normal),0.0,1.0);
                float sandiness=(1.0-smoothstep(.002,.023,slope))*(1.0-smoothstep(9.0,39.0,abs(elevation)));
                vec3 shorePoint=patchTextureOrigin+patchDeltaMetres;
                float grain=groundNoise(shorePoint*.51), variation=groundNoise(shorePoint*.036);
                float width=mix(13.0,41.0,variation)*mix(.6,1.0,sandiness);
                float edgeAA=max(.65,footprint*.48);
                float wet=1.0-smoothstep(1.0,9.0,max(0.0,coastDistance));
                vec3 drySand=mix(vec3(.52,.36,.185),vec3(.76,.61,.37),.34+.55*grain);
                vec3 sand=mix(drySand,drySand*vec3(.50,.59,.65),wet);
                vec3 rocky=mix(vec3(.15,.13,.11),vec3(.32,.29,.22),grain);
                float shore=1.0-smoothstep(width*.52,width,max(0.0,coastDistance));
                material=mix(material,mix(rocky,sand,sandiness),shore*detail*(1.0-smoothstep(25.0,50.0,-coastDistance)));
                float shallow=1.0-smoothstep(3.0,45.0,-coastDistance);
                ocean=mix(ocean,mix(vec3(.020,.19,.22),sand*.62,.44),shallow*detail*(.46+.43*sandiness));
                float newCoverage=1.0-smoothstep(-edgeAA,edgeAA,coastDistance);
                float coastBlend=detail*(1.0-smoothstep(75.0,130.0,abs(coastDistance)));
                oceanCoverage=mix(oceanCoverage,newCoverage,coastBlend);
                water=mix(water,newCoverage,coastBlend);
                float wave=6.2831853*coastDistance/mix(4.8,10.5,variation)+phase+variation*3.5;
                float whitewater=smoothstep(.70,.98,sin(wave)+.14*(grain-.5));
                shoreFoam=newCoverage*coastBlend*(1.0-smoothstep(1.8,17.0,-coastDistance))*whitewater
                    *mix(.34,.74,smoothstep(0.0,13.0,weatherParameters.x));
            }
            material=mix(material,ocean,oceanCoverage);
        }else{
        float moisture=.5+.5*terrainNoise(radial*5.0+vec3(31));
        material=mix(albedo*vec3(1.3,.9,.5),albedo*vec3(.45,1.1,.55),moisture);
        material=mix(vec3(.55,.46,.31),material,smoothstep(seaLevel,seaLevel+.04,height));
        material=mix(material,vec3(.34,.32,.30),smoothstep(.15,.35,height));
        material=mix(material,vec3(.9),smoothstep(.25,.45,height+.15*pow(abs(radial.y),4.0)));
        if(lifeBearing){
            float wet=vegetationMoisture(radial,terrainSeed);
            float altitude=max(0.0,(height-seaLevel)*.003*bodyRadiusKm*1000.0);
            float climate=temperature+8.0-28.0*radial.y*radial.y-altitude*.005;
            bool habitat=height>seaLevel+.006 && height<max(.18,seaLevel+.11)
                && altitude<2200.0 && climate>265.0 && climate<312.0 && wet>.30;
            if(habitat)material=mix(vec3(.32,.29,.10),vec3(.10,.25,.045),smoothstep(.30,.48,wet));
        }
        material*=.80+.35*(.5+.5*terrainNoise(radial*400.0));
        vec3 ocean=mix(vec3(.02,.30,.36),vec3(.003,.015,.05),
            1.0-exp(-15.0*max(seaLevel-height,0.0)));
        material=mix(material,ocean,oceanCoverage);
        }
        if(earthPoles){
            float frozenSea=oceanCoverage-water;
            if(frozenSea>.001){
                float plates=terrainNoise(radial*91.0)+.25*terrainNoise(radial*260.0);
                float seams=1.0-smoothstep(.028,.10,abs(plates));
                vec3 ice=mix(vec3(.49,.66,.76),vec3(.83,.91,.94),smoothstep(-.67,.58,plates));
                material=mix(material,ice*mix(1.0,.62,seams),frozenSea);
            }
            float snow=earthMapped?0.0:1.0-oceanCoverage;
            snow*=max(smoothstep(.844,.891,-radial.y),smoothstep(.89,.982,radial.y)*.92);
            if(snow>.001){
                float glacier=.5+.5*terrainNoise(radial*77.0);
                vec3 sheet=mix(vec3(.69,.81,.89),vec3(.94,.97,.99),glacier);
                material=mix(material,sheet,snow);
            }
        }
        }
        if(water>.5 && weatherParameters.w>0.0){
            vec3 wavePosition=patchDrawing?patchTextureOrigin+patchDeltaMetres:radial*weatherParameters.w;
            normal=normalize(radial-weatherWaveGradient(wavePosition,radial,true));
        }else groundDetail(material,normal,patchDeltaMetres,water,length(relativePosition)*149597870700.0,phase);
        if(waterRefraction && water>.5)
            material=mix(material,refractedSeabed(material,radial,normal,height),smoothstep(.5,.97,water));
    } else {
        float bands=.5+.5*sin(70.0*radial.y+7.0*terrainFbm(radial*5.0));
        material=mix(albedo*.4,min(vec3(1),albedo*1.5+vec3(.15)),bands);
    }
    normal=normalize(mat3(transpose(inverse(model)))*normal);
    vec3 light=normalize(starPosition-relativePosition);
    vec3 view=normalize(-relativePosition);
    float diffuse=max(dot(normal,light),0.0);
    if(saturnShadow){
        vec3 localLight=normalize(transpose(mat3(model))*light);
        float distanceToPlane=abs(localLight.y)>.00001?-radial.y/localLight.y:-1.0;
        if(distanceToPlane>0.0){
            vec3 ringPoint=(radial+localLight*distanceToPlane)*bodyRadiusKm;
            diffuse*=exp(-saturnRingDepth(length(ringPoint.xz))/max(.02,abs(localLight.y)));
        }
    }
    float highlight=max(dot(normal,normalize(light+view)),0.0);
    float specular=water*(.82*pow(highlight,100.0)+.18*pow(highlight,27.0));
    float foam=max(shoreFoam,water*weatherOceanFoam(patchDrawing?patchTextureOrigin+patchDeltaMetres:radial*weatherParameters.w,radial));
    material=mix(material,vec3(.72,.80,.82),foam);specular*=1.0-.85*foam;
    color=vec4(material*(.012+diffuse)+specular,water>.95?.1+.3*specular:0.0);
}
)";
    program = compileShaderProgram(vertex.c_str(), fragment.c_str());
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    const auto upload = [](unsigned int &vao, unsigned int &buffer, const std::vector<glm::vec3> &vertices) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &buffer);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glEnableVertexAttribArray(0);
    };
    const auto sphere = PlanetTerrain::SphereVertices(64, 128);
    sphereCount = static_cast<int>(sphere.size());
    upload(sphereVao, sphereBuffer, sphere);
    upload(lineVao, lineBuffer, {{0, 0, 0}});
    upload(patchVao, patchBuffer, {{0, 0, 0}});
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!atmosphere.Initialize() || !stellarActivity.Initialize() || !saturnRings.Initialize() || !wormhole.Initialize()
        || !vegetation.Initialize() || !birds.Initialize() || !fish.Initialize()) {
        Release();
        return false;
    }
    return true;
}

void StarSystem::UpdatePatch(const glm::dvec3 &direction,double radiusMetres)
{
    if(!patchCount || radiusMetres!=patchRadiusMetres){
        const auto vertices=PlanetTerrain::SurfaceLodVertices(radiusMetres);
        patchCount=static_cast<int>(vertices.size());patchRadiusMetres=radiusMetres;
        glBindBuffer(GL_ARRAY_BUFFER,patchBuffer);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_STATIC_DRAW);
        patchDirection=direction;
    }else if(glm::length(direction-patchDirection)*radiusMetres>2.0)patchDirection=direction;
}

void StarSystem::BindEarthMaps() const {
    glUniform1i(glGetUniformLocation(program,"earthReliefMap"),4);
    glUniform1i(glGetUniformLocation(program,"earthColorMap"),5);
    glUniform1i(glGetUniformLocation(program,"earthDetailMap"),6);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,earthReliefTexture);
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,earthColorTexture);
    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,earthDetailTexture);
    glActiveTexture(GL_TEXTURE0);
}

void StarSystem::Render(float fieldOfView, float aspect, const glm::vec3 &front, const glm::vec3 &up, double seconds,
                        bool trajectories, bool photonView, bool showSelection, unsigned int wormholeSky)
{
    vegetation.ClearCounts();
    birds.ClearCounts();
    fish.ClearCount();
    if (!program || planets.empty())
        return;
    if(!InDetailRange())return;
    const float waterDepth=static_cast<float>(DepthBelowWater());
    const int divePlanet=waterDepth>0?landedPlanet:-1;
    glDisable(GL_BLEND); // Opaque reflected light writes RGB and zero flare eligibility.
    double clearance = glm::length(camera) - star.radiusSolar * Astronomy::solarRadiusAu;
    for (int planet = 0; planet < static_cast<int>(planets.size()); ++planet)
    {
        const auto delta = camera - Position(planet, seconds);
        const double distance = glm::length(delta);
        if (distance > 0)
            clearance = std::min(clearance, distance - SurfaceRadius(planet, delta / distance, seconds));
    }
    if (landed)
        clearance = 1.0 / Astronomy::metresPerAu;
    const auto projection = glm::perspective(glm::radians(fieldOfView), aspect,
                                             static_cast<float>(std::clamp(clearance * .01, 1e-13, 1e-3)), 20000.0f);
    const auto view = glm::lookAt(glm::vec3(0), front, up);
    const auto vp = projection * view;
    int viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    const double pixelsPerRadian=viewport[3]/(2.0*std::tan(glm::radians(static_cast<double>(fieldOfView))*.5));
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    BindEarthMaps();
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(vp));
    glUniform3fv(glGetUniformLocation(program, "starPosition"), 1, glm::value_ptr(glm::vec3(-camera)));
    glUniform1f(glGetUniformLocation(program, "phase"),
                static_cast<float>(std::remainder(seconds, glm::two_pi<double>())));
    glUniform1i(glGetUniformLocation(program,"waterRefraction"),waterRefractionEnabled);
    glUniform1i(glGetUniformLocation(program,"earthShoreDetail"),earthShoreDetail);
    glUniform1i(glGetUniformLocation(program,"layeredTerrain"),!solarSystem);
    const auto setModel = [&](const glm::mat4 &model) {
        glUniformMatrix4fv(glGetUniformLocation(program, "model"), 1, GL_FALSE, glm::value_ptr(model));
    };
    auto model = glm::translate(glm::mat4(1), glm::vec3(-camera));
    model*=Rotation(-1,seconds);
    model = glm::scale(model, glm::vec3(static_cast<float>(star.radiusSolar * Astronomy::solarRadiusAu)));
    setModel(model);
    glUniform1i(glGetUniformLocation(program, "bodyKind"), 0);
    glUniform1i(glGetUniformLocation(program,"seabedRendering"),false);
    glUniform1i(glGetUniformLocation(program, "patchEnabled"), false);
    glUniform1i(glGetUniformLocation(program, "patchDrawing"), false);
    glUniform1f(glGetUniformLocation(program, "terrainSeed"), static_cast<float>(star.id % 10000));
    glUniform1f(glGetUniformLocation(program, "temperature"), static_cast<float>(star.temperature));
    stellarActivity.BindSurface(program,star.id,seconds);
    glBindVertexArray(sphereVao);
    glDrawArrays(GL_TRIANGLES, 0, sphereCount);
    stellarActivity.Render(vp,model,static_cast<float>(star.temperature),star.id,seconds);
    for (int planet = 0; planet < static_cast<int>(planets.size()); ++planet)
    {
        const auto &body = planets[planet];
        if(body.wormhole)continue; // The throat is rendered by null-ray transport, never terrain.
        const auto weather=WeatherState(planet,seconds);PlanetWeather::Upload(program,weather);
        const auto center = Position(planet, seconds);
        const double radius = body.radiusEarth * Astronomy::earthRadiusAu;
        if(radius/glm::max(radius,glm::length(center-camera))*pixelsPerRadian<.75)continue;
        model = glm::translate(glm::mat4(1), glm::vec3(center - camera)) * Rotation(planet, seconds);
        model = glm::scale(model, glm::vec3(static_cast<float>(radius)));
        setModel(model);
        glUniform1i(glGetUniformLocation(program,"saturnShadow"),solarSystem && body.name=="Saturn");
        const bool seabed=planet==divePlanet;
        glUniform1i(glGetUniformLocation(program,"seabedRendering"),seabed);
        glUniform1f(glGetUniformLocation(program,"bodyRadiusKm"),static_cast<float>(radius*Astronomy::metresPerAu/1000));
        glUniform1i(glGetUniformLocation(program, "bodyKind"), body.gas ? 2 : 1);
        glUniform1i(glGetUniformLocation(program, "surfacePalette"),static_cast<int>(body.palette));
        glUniform1i(glGetUniformLocation(program,"lifeBearing"),body.HasMulticellularLife());
        glUniform1f(glGetUniformLocation(program,"temperature"),static_cast<float>(body.temperature));
        glUniform1f(glGetUniformLocation(program, "terrainSeed"), static_cast<float>(body.seed));
        glUniform1f(glGetUniformLocation(program, "seaLevel"), body.seaLevel);
        const float arcticWinter=body.earthPoles?PlanetTerrain::ArcticWinter(seconds):0;
        glUniform1i(glGetUniformLocation(program,"earthPoles"),body.earthPoles);
        if(body.earthPoles && !earthMapsLoaded){
            earthMapsLoaded=true;
            earthReliefTexture=PlanetTerrain::LoadEarthReliefTexture();
            const auto path=PlanetTerrain::EarthAssetPath("earth-color.jpg");
            earthColorTexture=AssetManager::LoadTexture(path.string().c_str());
            if(earthColorTexture){
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
            }
            BindEarthMaps();
        }
        glUniform1i(glGetUniformLocation(program,"earthMapped"),body.earthPoles && earthReliefTexture!=0);
        glUniform1f(glGetUniformLocation(program,"arcticWinter"),arcticWinter);
        glUniform1f(glGetUniformLocation(program, "terrainRelief"), .003f);
        glUniform1f(glGetUniformLocation(program, "waterWaveHeight"),
                    static_cast<float>(2.0 / (body.radiusEarth * 6371000.0)));
        glUniform3fv(glGetUniformLocation(program, "albedo"), 1, glm::value_ptr(body.color));
        const auto cameraRadial=glm::normalize(camera-center);
        const auto localDirection=glm::transpose(RotationDouble(planet,seconds))*cameraRadial;
        double groundClearance=(glm::length(camera-center)-SurfaceRadius(planet,cameraRadial,seconds))*Astronomy::metresPerAu;
        const bool patch = !body.gas && groundClearance<20000.0;
        if(seabed)groundClearance=WaterDepth(planet,glm::vec3(localDirection),seconds)-waterDepth;
        glUniform1f(glGetUniformLocation(program,"groundClearanceMetres"),static_cast<float>(std::max(0.0,groundClearance)));
        glUniform1i(glGetUniformLocation(program,"earthDetailed"),false);
        if(patch && body.earthPoles && earthReliefTexture)
            PlanetTerrain::BindEarthReliefDetail(program,glm::vec3(localDirection),earthDetailTexture,earthDetailOrigin);
        glUniform1i(glGetUniformLocation(program, "patchEnabled"), patch);
        glUniform1i(glGetUniformLocation(program, "patchDrawing"), false);
        if (patch)
        {
            const double metres=radius*Astronomy::metresPerAu;
            UpdatePatch(localDirection,metres);
            float elevation=PlanetTerrain::SurfaceElevation(glm::vec3(patchDirection),body.seed,
                static_cast<float>(std::remainder(seconds,glm::two_pi<double>())),body.seaLevel,.003f,static_cast<float>(2/metres),
                false,seabed?nullptr:&weather,body.earthPoles,arcticWinter,!solarSystem);
            if(seabed)elevation-=static_cast<float>(WaterDepth(planet,glm::vec3(patchDirection),seconds)/metres);
            PlanetTerrain::ConfigureSurfacePatch(program,patchDirection,RotationDouble(planet,seconds),center-camera,metres,elevation);
        }
        glBindVertexArray(sphereVao);
        glDrawArrays(GL_TRIANGLES, 0, sphereCount);
        if (patch)
        {
            glUniform1i(glGetUniformLocation(program, "patchDrawing"), true);
            glBindVertexArray(patchVao);
            glDrawArrays(GL_TRIANGLES, 0, patchCount);
            glUniform1i(glGetUniformLocation(program,"patchDrawing"),false);
        }
    }
    glUniform1i(glGetUniformLocation(program, "patchEnabled"), false);
    glUniform1i(glGetUniformLocation(program, "bodyKind"), 3);
    setModel(glm::mat4(1));
    const auto drawLines = [&](const std::vector<glm::vec3> &vertices, GLenum mode, const glm::vec3 &color,
                               float size) {
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineBuffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_DYNAMIC_DRAW);
        glUniform3fv(glGetUniformLocation(program, "albedo"), 1, glm::value_ptr(color));
        glPointSize(size);
        glDrawArrays(mode, 0, static_cast<int>(vertices.size()));
    };
    if (trajectories && !asteroids.empty())
    {
        if(cachedAsteroids.size()!=asteroids.size() || !std::isfinite(cachedAsteroidTime)
            || std::abs(seconds-cachedAsteroidTime)>=300) {
            cachedAsteroids.clear();cachedAsteroids.reserve(asteroids.size());
            for(const auto& orbit:asteroids) {
                const auto p=OrbitalMechanics::PositionAtTimeDouble(orbit,Astronomy::solarGravityAu,seconds);
                cachedAsteroids.push_back({p.x,p.z,-p.y});
            }
            cachedAsteroidTime=seconds;
        }
    }
    if (photonView)
    {
        std::vector<glm::vec3> packets;
        for (int body = -1; body < static_cast<int>(planets.size()); ++body)
        {
            const auto source = Position(body, seconds);
            const double duration = std::max(.001, glm::length(source - photonDetector) / Astronomy::lightSpeedAu);
            for (int point = 0; point < 8; ++point)
            {
                const double fraction = std::fmod(seconds / duration + point / 8.0, 1.0);
                packets.push_back(glm::vec3(glm::mix(source, photonDetector, fraction) - camera));
            }
        }
        for (const auto& source:cachedAsteroids) {
            const double duration=std::max(.001,glm::length(source-photonDetector)/Astronomy::lightSpeedAu);
            for (int point=0;point<4;++point) {
                const double fraction=std::fmod(seconds/duration+point/4.0,1.0);
                packets.push_back(glm::vec3(glm::mix(source,photonDetector,fraction)-camera));
            }
        }
        drawLines(packets, GL_POINTS, glm::vec3(.3f, .7f, 1), 3);
    }
    glBindVertexArray(0);
    if(solarSystem && planets.size()>5){
        saturnRings.Render(vp,Position(5,seconds)-camera,RotationDouble(5,seconds),-Position(5,seconds),
            planets[5].radiusEarth*Astronomy::earthRadiusAu,
            Astronomy::solarGravityAu*planets[5].massEarth/332946.0487,seconds);
    }
    int livingPlanet=divePlanet;double livingClearance=std::numeric_limits<double>::max();
    if((vegetationEnabled || birdsEnabled || fishEnabled) && divePlanet<0)for(int planet=0;planet<static_cast<int>(planets.size());++planet){
        const auto& body=planets[planet];if(!body.HasMulticellularLife() || body.gas || body.wormhole)continue;
        const double clearance=std::abs(glm::length(Position(planet,seconds)-camera)-body.radiusEarth*Astronomy::earthRadiusAu);
        if(clearance<livingClearance){livingClearance=clearance;livingPlanet=planet;}
    }
    if(livingPlanet>=0 && planets[livingPlanet].HasMulticellularLife()){
        if(livingPlanet!=wildlifePlanet){birds.Reset();fish.Reset();wildlifePlanet=livingPlanet;}
        const auto& body=planets[livingPlanet];
        const auto centre=Position(livingPlanet,seconds);
        const auto light=glm::normalize(glm::vec3(-centre));
        const auto rotation=RotationDouble(livingPlanet,seconds);
        const auto weather=WeatherState(livingPlanet,seconds);
        const auto radiusMetres=body.radiusEarth*Astronomy::earthRadiusAu*Astronomy::metresPerAu;
        if(vegetationEnabled && divePlanet<0){
            vegetation.SetSunlight(light);
            vegetation.Render({body.seed,radiusMetres,body.temperature,body.seaLevel,body.HasMulticellularLife(),body.earthPoles},
                rotation,centre-camera,vp,front,seconds,weather);
        }
        if(birdsEnabled && body.atmosphere && divePlanet<0)
            birds.Render({body.seed,radiusMetres,body.temperature,body.seaLevel,body.earthPoles,!solarSystem},
                rotation,camera-centre,vp,light,seconds,weather);
        if(fishEnabled && body.HasSurfaceLiquid() && (divePlanet>=0 || livingClearance*Astronomy::metresPerAu<7300)){
            const auto delta=camera-centre;
            const auto radial=glm::normalize(delta);
            const auto local=glm::vec3(glm::transpose(rotation)*radial);
            double waterRadius=body.earthPoles?radiusMetres:radiusMetres*(1+body.seaLevel*.003);
            if(divePlanet>=0)waterRadius=glm::length(delta)*Astronomy::metresPerAu+waterDepth;
            else if(WaterDepth(livingPlanet,local,seconds)>0)
                waterRadius=SurfaceRadius(livingPlanet,radial,seconds)*Astronomy::metresPerAu;
            fish.Render(vp,rotation,delta,waterRadius,light,seconds,body.seed,divePlanet>=0,
                [&](const glm::vec3& direction){return WaterDepth(livingPlanet,direction,seconds);});
        }
    }
    for (int planet = 0; planet < static_cast<int>(planets.size()); ++planet)
    {
        if(planet==divePlanet)continue;
        const auto& body=planets[planet];
        const bool reflectOcean=oceanReflectionsEnabled && body.HasSurfaceLiquid();
        if(!body.atmosphere && !reflectOcean)continue;
        const auto center = glm::vec3(Position(planet, seconds) - camera);
        const auto weather=WeatherState(planet,seconds);
        atmosphere.Render(vp, glm::vec3(0), center,
                          static_cast<float>(body.radiusEarth * Astronomy::earthRadiusAu), true,
                          glm::vec3(-camera),weatherEnabled && earthCloudsEnabled && weather.parameters.w>0,seconds,glm::mat3(Rotation(planet,seconds)),
                          reflectOcean,1,&weather,glm::vec2(0),false,body.atmosphere);
    }
    for(int body=0;body<static_cast<int>(planets.size());++body)
        if(planets[body].wormhole)wormhole.Render(vp,Position(body,seconds)-camera,
            planets[body].radiusEarth*Astronomy::earthRadiusAu,wormholeSky);
    if(divePlanet>=0){
        const auto& body=planets[divePlanet];
        const auto center=Position(divePlanet,seconds);
        const auto rotation=RotationDouble(divePlanet,seconds);
        const auto weather=WeatherState(divePlanet,seconds);
        atmosphere.Render(vp,glm::vec3(0),glm::vec3(center-camera),
            static_cast<float>(body.radiusEarth*Astronomy::earthRadiusAu),true,
            glm::vec3(-camera),false,seconds,glm::mat3(rotation),false,1,&weather,glm::vec2(0),false,true,waterDepth);
    }
    glBindVertexArray(0);
}

void StarSystem::RenderOverlays(float fieldOfView,float aspect,const glm::vec3& front,const glm::vec3& up,
                                double seconds,bool trajectories,bool showSelection,int lensProfile){
    if(!program || planets.empty() || !InDetailRange())return;
    // UI guides must not erase a subpixel stellar emitter, enter exposure
    // metering, or appear as opaque geometry in screen-space reflections.
    const bool depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND);
    GLboolean depthMask;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    GLint src,dst,srcA,dstA;glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);
    glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ONE);glDepthMask(GL_FALSE);
    const auto vp=glm::perspective(glm::radians(fieldOfView),aspect,.001f,20000.f)*glm::lookAt(glm::vec3(0),front,up);
    const glm::mat4 identity(1);
    glUseProgram(program);
    BindEarthMaps();
    lensProfile=glm::clamp(lensProfile,0,6);const auto& lens=LensCalibration::profiles[lensProfile];
    const float zoom=std::tan(glm::radians(fieldOfView*.5f))/std::tan(glm::radians(LensCalibration::FieldOfView(lensProfile,aspect)*.5f));
    glUniform1i(glGetUniformLocation(program,"guideOverlay"),true);
    glUniform1i(glGetUniformLocation(program,"guideDistortionModel"),lens.distortionModel);
    glUniform3fv(glGetUniformLocation(program,"guideDistortion"),1,glm::value_ptr(lens.distortion));
    glUniform2f(glGetUniformLocation(program,"guideRadiusScale"),1.5f*zoom,1.5f/aspect*zoom);
    glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    glUniformMatrix4fv(glGetUniformLocation(program,"model"),1,GL_FALSE,glm::value_ptr(identity));
    glUniform1i(glGetUniformLocation(program,"bodyKind"),3);
    glUniform1i(glGetUniformLocation(program,"patchDrawing"),false);
    const auto drawLines=[&](const std::vector<glm::vec3>& vertices,GLenum mode,const glm::vec3& color,float size){
        glBindVertexArray(lineVao);glBindBuffer(GL_ARRAY_BUFFER,lineBuffer);
        glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec3),vertices.data(),GL_DYNAMIC_DRAW);
        glUniform3fv(glGetUniformLocation(program,"albedo"),1,glm::value_ptr(color));glPointSize(size);
        glDrawArrays(mode,0,static_cast<int>(vertices.size()));
    };
    std::vector<glm::vec3> markers;
    for(int planet=0;planet<static_cast<int>(planets.size());++planet)
        if(!planets[planet].wormhole)markers.push_back(glm::vec3(Position(planet,seconds)-camera));
    drawLines(markers,GL_POINTS,glm::vec3(.15f,.45f,.65f),3);
    if(trajectories && !cachedAsteroids.empty()){
        std::vector<glm::vec3> points;points.reserve(cachedAsteroids.size());
        for(const auto& p:cachedAsteroids)points.push_back(glm::vec3(p-camera));
        drawLines(points,GL_POINTS,glm::vec3(.13f,.10f,.07f),1);
    }
    if(trajectories){
        if(cachedOrbits.size()!=planets.size()){
            cachedOrbits.resize(planets.size());
            for(int planet=0;planet<static_cast<int>(planets.size());++planet){
                const double period=glm::two_pi<double>()/OrbitalMechanics::MeanMotion(GravityFor(planet),planets[planet].orbit.semiMajorAxis);
                for(int point=0;point<=256;++point)cachedOrbits[planet].push_back(RelativePosition(planet,period*point/256.0));
            }
        }
        for(int planet=0;planet<static_cast<int>(planets.size());++planet){
            std::vector<glm::vec3> path;const auto offset=Position(planets[planet].parent,seconds)-camera;
            for(const auto& point:cachedOrbits[planet])path.push_back(glm::vec3(point+offset));
            drawLines(path,GL_LINE_STRIP,glm::vec3(.035f,.12f,.18f),1);
        }
    }
    const auto clip = vp * glm::vec4(glm::vec3(Position(selected, seconds) - camera), 1);
    if (showSelection && clip.w > 0)
    {
        const auto position = glm::vec2(clip) / clip.w;
        if (std::abs(position.x) < 1 && std::abs(position.y) < 1)
        {
            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "bodyKind"), 4);
            std::vector<glm::vec3> crosshair;
            for (int sign : {-1, 1})
            {
                crosshair.push_back({position.x + sign * .006f, position.y, 0});
                crosshair.push_back({position.x + sign * .020f, position.y, 0});
                crosshair.push_back({position.x, position.y + sign * .006f * aspect, 0});
                crosshair.push_back({position.x, position.y + sign * .020f * aspect, 0});
            }
            drawLines(crosshair, GL_LINES, glm::vec3(.2f, 1, .75f), 1);
        }
    }
    glBindVertexArray(0);
    glUniform1i(glGetUniformLocation(program,"guideOverlay"),false);
    glDepthMask(depthMask);if(depth)glEnable(GL_DEPTH_TEST);
    glBlendFuncSeparate(src,dst,srcA,dstA);if(!blend)glDisable(GL_BLEND);
}

void StarSystem::Release()
{
    for(auto texture:{earthReliefTexture,earthColorTexture,earthDetailTexture})if(texture)glDeleteTextures(1,&texture);
    earthReliefTexture=earthColorTexture=earthDetailTexture=0;
    earthMapsLoaded=false;
    wormholeSkyCache.Release();
    vegetation.Release();
    birds.Release();
    fish.Release();
    wormhole.Release();
    saturnRings.Release();
    stellarActivity.Release();
    atmosphere.Release();
    if (program)
        glDeleteProgram(program);
    for (auto vao : {sphereVao, lineVao, patchVao})
        if (vao)
            glDeleteVertexArrays(1, &vao);
    for (auto buffer : {sphereBuffer, lineBuffer, patchBuffer})
        if (buffer)
            glDeleteBuffers(1, &buffer);
    program = sphereVao = sphereBuffer = lineVao = lineBuffer = patchVao = patchBuffer = 0;
}

glm::dvec3 StarSystem::SaturnWormholePosition(double seconds) {
    static const StarSystem solar=[](){StarSystem model;model.LoadSolarSystem();return model;}();
    for(int i=0;i<static_cast<int>(solar.planets.size());++i)
        if(solar.planets[i].wormhole)return solar.Position(i,seconds);
    return glm::dvec3(0);
}

unsigned StarSystem::WormholeSky(double seconds,const WormholeBackground& background) {
    if(!program || !solarSystem)return 0;
    const auto previousCamera=camera;
    const auto mouth=SaturnWormholePosition(seconds);
    camera=mouth;
    const unsigned result=wormholeSkyCache.Capture(seconds,[&](const glm::vec3& direction,const glm::vec3& up){
        const auto vp=glm::perspective(glm::half_pi<float>(),1.f,1e-6f,20000.f)*glm::lookAt(glm::vec3(0),direction,up);
        if(background)background(vp,mouth);
        Render(90.f,1.f,direction,up,seconds,false,false,false,0);
    });
    camera=previousCamera;
    return result;
}
