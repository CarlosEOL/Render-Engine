#include "Astronomy.h"
#include "CameraMotion.h"
#include "QuantumStates.h"
#include "StarSystem.h"
#include "UniverseCatalog.h"
#include "PlanetTerrain.h"
#include "InterstellarWorlds.h"
#include "KerrSpacetime.h"
#include "MilkyWayModel.h"
#include "NavigationCatalog.h"
#include "GalacticCore.h"
#include "FlightUnits.h"
#include "StellarPhotometry.h"
#include "CelestialPhenomena.h"
#include "WormholePhysics.h"
#include "CameraZoom.h"
#include "WormholeTransit.h"
#include "BlackHoleJourney.h"
#include "TesseractSpace.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <set>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

// The catalogue is exercised without creating an OpenGL context.
unsigned int compileShaderProgram(const char *, const char *)
{
    return 0;
}
void Require(bool result, const char *message)
{
    if (!result)
        throw std::runtime_error(message);
}
int main()
{
#ifdef _MSC_VER
    // Automated checks must fail in their log instead of blocking on a CRT
    // dialog that the calling build cannot dismiss or report.
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
    try {
    Require(Astronomy::gargantuaHorizonAu>1.97 && Astronomy::gargantuaHorizonAu<1.98,"Black hole mass-to-horizon scale incorrect");
    Require(Astronomy::millerOrbitAu>Astronomy::gargantuaHorizonAu*3,"Miller model orbit is inside Schwarzschild ISCO");
    Require(std::abs(Astronomy::millerRadiusAu*Astronomy::metresPerAu-6371000)<.001,"Miller radius is not Earth scale");
    Require(std::abs(Astronomy::observerClearanceAu*Astronomy::metresPerAu-6.6)<1e-10,"Telescope observer clearance incorrect");
    for(double eccentricity:{0.0,.1,.8,.96714,.999999})for(double phase:{0.0,1e-9,.1,1.5,3.141592653589793}) {
        OrbitalMechanics::Orbit orbit{1,eccentricity,.1,.3,phase};
        const auto p=OrbitalMechanics::PositionAtTimeDouble(orbit,1,0);
        Require(std::isfinite(glm::length(p)),"Fast Kepler solver produced nonfinite orbit");
        Require(glm::length(p)>=1-eccentricity-1e-12 && glm::length(p)<=1+eccentricity+1e-12,"Fast Kepler solver left orbital bounds");
    }
    using namespace QuantumStates;
    Require(std::abs(KerrSpacetime::Horizon(2,0)-2)<1e-12 && std::abs(KerrSpacetime::ProgradeIsco(2,0)-6)<1e-12,"Kerr zero-spin limit is wrong");
    for(double spin:{-.8,0.0,.8,.98}){
        auto ray=KerrSpacetime::PastRay({20,2,3},glm::normalize(glm::dvec3(-1,.24,-.04)),2,spin);
        Require(std::abs(KerrSpacetime::Hamiltonian(ray,2,spin))<1e-12,"Kerr observer tetrad is not null");
        const double angular=glm::cross(ray.position,ray.momentum).z;
        double error=0;
        for(int step=0;step<20000;++step){
            if(KerrSpacetime::Radius(ray.position,2,spin)<KerrSpacetime::Horizon(2,spin)*1.1)break;
            KerrSpacetime::AdvanceRay(ray,2,spin,.002);
            error=std::max(error,std::abs(KerrSpacetime::Hamiltonian(ray,2,spin)));
        }
        Require(error<1e-7 && std::abs(glm::cross(ray.position,ray.momentum).z-angular)<1e-7,"Kerr null-ray invariants drifted");
        std::cout<<"Kerr spin="<<spin<<" max |H|="<<error<<"\n";
    }
    for (const auto &example : Examples())
    {
        if (example.family == Family::Box || example.family == Family::Oscillator || example.family == Family::Packet)
        {
            for (double time : {0.0, .7, 4.0})
            {
                double norm = 0;
                constexpr double dx = .004;
                for (int i = 0; i < 40000; ++i)
                {
                    const double x = -80 + (i + .5) * dx;
                    norm += std::norm(Evaluate(example, x, 0, time)) * dx;
                }
                if (std::abs(norm - 1) > 1e-5)
                    std::cerr << example.name << " norm=" << norm << "\n";
                Require(std::abs(norm - 1) < 1e-5, "Analytic wavefunction normalization failed");
            }
        }
        if (example.family == Family::Spin || example.family == Family::Bell)
        {
            for (double time : {0.0, .7, 4.0})
            {
                double norm = 0;
                for (double p : Probabilities(example, time))
                {
                    Require(p >= 0 && p <= 1, "Invalid probability");
                    norm += p;
                }
                Require(std::abs(norm - 1) < 1e-12, "Qubit probabilities not normalized");
            }
        }
    }
    for (int potential = 0; potential < 3; ++potential)
    {
        Solver solver;
        solver.Reset(potential);
        Require(std::abs(solver.Norm() - 1) < 1e-12, "Initial numerical norm failed");
        solver.Advance(5);
        Require(std::abs(solver.Norm() - 1) < 1e-10, "Crank-Nicolson lost probability");
        if (potential == 0)
        {
            double left = 0, right = 0;
            for (int i = 0; i < Solver::size; ++i)
            {
                const double x = -Solver::extent + i * Solver::dx;
                if (x < -.5)
                    left += std::norm(solver.Wave()[i]) * Solver::dx;
                if (x > .5)
                    right += std::norm(solver.Wave()[i]) * Solver::dx;
            }
            Require(left > .1 && right > .01, "Barrier did not produce reflected and transmitted waves");
            std::cout << "Barrier R=" << left << " T=" << right << " norm=" << solver.Norm() << "\n";
        }
    }
    StarSystem solar;
    solar.LoadSolarSystem();
    {
        const auto earth=solar.Position(2,0);
        const auto outward=glm::normalize(earth);
        solar.camera=earth+outward*Astronomy::earthRadiusAu*1.000001;
        const double surfaceClock=solar.ObserverClockRate(0);
        solar.camera=earth+outward*Astronomy::earthRadiusAu*1.2;
        const double orbitClock=solar.ObserverClockRate(0);
        Require(surfaceClock<orbitClock && orbitClock-surfaceClock<1e-8,"Nearby planetary gravity is missing from camera time");
        std::cout.precision(12);std::cout<<"Earth clock surface="<<surfaceClock<<"; elevated="<<orbitClock<<"\n";
        solar.camera={0,0,3};
    }
    for (int orbital = 0; orbital < 7; ++orbital)
    {
        double norm = 0, angular = 0;
        for (int i = 0; i < 20000; ++i)
            norm += std::pow(HydrogenRadial(orbital, (i + .5) * .01), 2) * .01;
        Require(std::abs(norm - 1) < 1e-7, "Hydrogen radial probability not normalized");
        const double radialAtOne = HydrogenRadial(orbital, 1);
        for (int i = 0; i < 4096; ++i)
        {
            const double z = 1 - 2 * (i + .5) / 4096, angle = i * 2.399963229728653;
            const double x = std::sqrt(1 - z * z) * std::cos(angle), y = std::sqrt(1 - z * z) * std::sin(angle);
            angular +=
                std::norm(Hydrogen3D(orbital, x, y, z, 0)) / (radialAtOne * radialAtOne) * 12.566370614359172 / 4096;
        }
        Require(std::abs(angular - 1) < 1e-4, "Hydrogen angular probability not normalized");
    }
    Require(solar.Planets().size() == 21 && solar.AsteroidCount() == 4096, "Solar system catalogue incomplete");
    const int mouth=static_cast<int>(solar.Planets().size())-1;
    const auto& wormhole=solar.Planets()[mouth];
    Require(wormhole.wormhole && wormhole.parent==5 && wormhole.massEarth==0 && !wormhole.atmosphere,"Wormhole is not a Saturn test object");
    const double wormholeMu=Astronomy::solarGravityAu*solar.Planets()[5].massEarth*5.97217e24/1.98847e30;
    const double wormholePeriod=glm::two_pi<double>()/OrbitalMechanics::MeanMotion(wormholeMu,wormhole.orbit.semiMajorAxis);
    const auto relativeMouth=[&](double time){return solar.Position(mouth,time)-solar.Position(5,time);};
    Require(glm::length(relativeMouth(0)-relativeMouth(wormholePeriod))<1e-12,"Wormhole Saturn-relative orbit did not close");
    Require(glm::length(relativeMouth(0)-relativeMouth(wormholePeriod*.25))>.01,"Wormhole did not move relative to Saturn");
    const double massLength=WormholePhysics::MassLength(),aLength=WormholePhysics::SpinLength();
    Require(massLength*(1+WormholePhysics::Deformation*WormholePhysics::Deformation)+std::sqrt(std::pow(massLength*(1+WormholePhysics::Deformation*WormholePhysics::Deformation),2)-aLength*aLength)>1-1e-12,"Incorrect throat normalization");
    Require(massLength+std::sqrt(massLength*massLength-aLength*aLength)<1,"Wormhole throat is behind the Kerr horizon");
    Require(1-2*massLength+aLength*aLength>0,"Throat metric has a horizon singularity");
    solar.SelectPlanet(mouth);glm::vec3 mouthFront(0,0,-1),mouthUp(0,1,0);
    solar.ToggleLanding(0,mouthFront,mouthUp);Require(!solar.IsLanded(),"Landed on a wormhole as if it were terrain");
    const auto &earth = solar.Planets()[2];
    const double year =
        6.283185307179586 / OrbitalMechanics::MeanMotion(Astronomy::solarGravityAu, earth.orbit.semiMajorAxis);
    Require(year / 86400 > 365 && year / 86400 < 366, "Earth year not physical");
    Require(std::abs(earth.radiusEarth * Astronomy::earthRadiusAu * Astronomy::metresPerAu - 6371008.4) < .01,
            "Earth radius scale incorrect");
    Require(glm::length(solar.Position(2, 0) - solar.Position(2, year)) < 1e-9, "Earth orbit not closed");
    const double moonDistance = glm::length(solar.Position(13, 0) - solar.Position(2, 0));
    Require(moonDistance * Astronomy::metresPerAu > 350e6 && moonDistance * Astronomy::metresPerAu < 410e6,
            "Moon did not orbit Earth at physical distance");
    glm::vec3 front(0, 0, -1), up(0, 1, 0);
    for (int i = 0; i < 10000; ++i)
    {
        CameraMotion::Roll(front, up, .013f);
        CameraMotion::Look(front, up, .007f, -.005f, false);
    }
    Require(std::abs(glm::dot(front, up)) < 1e-5f && std::abs(glm::length(up) - 1) < 1e-5f, "Camera basis drifted");
    solar.SelectPlanet(2);
    const glm::dvec3 offset(earth.radiusEarth * Astronomy::earthRadiusAu * 5, 0, 0);
    solar.camera = solar.Position(2, 0) + offset;
    solar.ToggleRotationFollow(0);
    const double quarterDay = earth.daySeconds * .25;
    solar.Update(quarterDay, front, up);
    const auto tilt = glm::rotate(glm::dmat4(1), static_cast<double>(earth.tilt), glm::dvec3(1, 0, 0));
    const glm::dmat3 delta(tilt * glm::rotate(glm::dmat4(1), 1.5707963267948966, glm::dvec3(0, 1, 0)) *
                           glm::transpose(tilt));
    Require(glm::length(solar.camera - solar.Position(2, quarterDay) - delta * offset) < 1e-12,
            "Camera did not follow object rotation");
    const auto pausedCamera = solar.camera;
    solar.Update(quarterDay, front, up);
    Require(glm::length(solar.camera - pausedCamera) < 1e-12, "Paused rotation-follow moved camera");
    StarSystem a, b, c;
    Require(StarSystem::DescribeStar(5,"Orbiting star 2").temperature==10000,"Orbiting star metadata diverged from rendered temperature");
    a.Generate(64, "A");
    b.Generate(64, "A");
    c.Generate(65, "B");
    Require(a.Planets().size() == b.Planets().size(), "Generation not deterministic");
    Require(a.Planets()[0].seed == b.Planets()[0].seed && a.Planets()[0].seed != c.Planets()[0].seed,
            "Planet seeds not deterministic and distinct");
    for (int i = 0; i < static_cast<int>(a.Planets().size()); ++i)
        Require(glm::length(a.Position(i, 12345) - b.Position(i, 12345)) < 1e-12,
                "Generated positions not deterministic");
    std::set<std::string> layouts;
    int leadingGiants=0,rockOnly=0,moons=0;
    for(int id=10;id<1034;++id){
        StarSystem generated,revisited;generated.Generate(id,"Survey");revisited.Generate(id,"Survey");
        const auto& worlds=generated.Planets();
        Require(!worlds.empty() && worlds.size()==revisited.Planets().size(),"System missing or not repeatable");
        std::string layout;bool onlyRock=true;
        for(size_t index=0;index<worlds.size();++index){
            const auto& planet=worlds[index];const auto& same=revisited.Planets()[index];
            Require(planet.seed==same.seed && planet.orbit.semiMajorAxis==same.orbit.semiMajorAxis
                && planet.radiusEarth==same.radiusEarth && planet.color==same.color,"Revisit changed planet identity");
            Require(planet.massEarth>0 && planet.radiusEarth>0 && std::isfinite(planet.temperature),"Invalid generated body");
            if(planet.parent>=0){
                ++moons;Require(planet.parent<static_cast<int>(index),"Moon parent must precede child");
                const auto& parent=worlds[planet.parent];
                const double hill=parent.orbit.semiMajorAxis*(1-parent.orbit.eccentricity)*
                    std::cbrt(parent.massEarth/(3*332946.0487*generated.Host().massSolar));
                Require(planet.orbit.semiMajorAxis*(1+planet.orbit.eccentricity)<hill*.23,"Moon outside stable Hill region");
                Require(planet.orbit.semiMajorAxis*(1-planet.orbit.eccentricity)>2.5*parent.radiusEarth*Astronomy::earthRadiusAu,"Moon inside Roche margin");
                continue;
            }
            layout+=planet.gas?'G':'R';onlyRock=onlyRock&&!planet.gas;
            if(index==0 && planet.gas)++leadingGiants;
            if(index>0){const auto& previous=worlds[index-1];
                Require(planet.orbit.semiMajorAxis*(1-planet.orbit.eccentricity)>
                    previous.orbit.semiMajorAxis*(1+previous.orbit.eccentricity),"Generated primary orbits cross");}
        }
        layouts.insert(layout);if(onlyRock)++rockOnly;
        Require(generated.Host().ageGyr>0 && generated.Host().ageGyr<13.8,"Invalid stellar age");
    }
    Require(layouts.size()>70 && leadingGiants>50 && rockOnly>20 && moons>100,"System architecture diversity collapsed");
    std::cout<<"Generation survey: "<<layouts.size()<<" primary layouts; "<<leadingGiants<<" leading giants; "<<moons<<" moons\n";
    Universe::Catalog catalogue;catalogue.Generate();
    Require(std::abs(glm::determinant(MilkyWayModel::Orientation())-1)<1e-12,"Galactic frame is not orthonormal/right-handed");
    Require(glm::length(MilkyWayModel::ToModelKpc(glm::dvec3(0))-glm::dvec3(8.277,0,0))<1e-12,"Sun is not at the measured Galactic radius");
    const auto sample=glm::dvec3(2,.3,-4);
    Require(glm::length(MilkyWayModel::ToModelKpc(MilkyWayModel::ToWorldAu(sample))-sample)<1e-12,"Galactic coordinate round trip failed");
    for(int i=0;i<32;++i){
        const auto point=catalogue.Positions()[i*461]+glm::dvec3(31,-11,13)*Astronomy::auPerLightYear;
        int closest=-2;double best=glm::dot(point,point);
        for(int j=0;j<Universe::StarCount;++j){const auto delta=catalogue.Positions()[j]-point;double distance=glm::dot(delta,delta);if(distance<best){best=distance;closest=Universe::StarId(j);}}
        Require(catalogue.Nearest(point)==closest,"Nearest-star tree disagrees with full catalogue");
    }
    Require(FlightUnits::Label(1/Astronomy::metresPerAu).find("m/s")!=std::string::npos
        && FlightUnits::Label(Astronomy::lightSpeedAu).find("c")!=std::string::npos
        && FlightUnits::Label(1).find("AU/s")!=std::string::npos
        && FlightUnits::Label(Astronomy::auPerLightYear).find("ly/s")!=std::string::npos,"Flight units do not span physical scales");
    Require(std::abs(FlightUnits::Step(FlightUnits::Step(1,1),-1)-1)<1e-12,"Wheel steps are not reversible");
    const double pc=206264.80624709636;
    Require(std::abs(StellarPhotometry::ApparentMagnitude(1,10*pc)-4.74)<1e-12
        && std::abs(StellarPhotometry::ApparentMagnitude(1,100*pc)-9.74)<1e-12
        && std::abs(StellarPhotometry::ApparentMagnitude(100,10*pc)+.26)<1e-12,"Magnitude does not follow luminosity/distance scaling");
    NavigationCatalog navigator;navigator.Initialize(catalogue);
    const auto earthSearch=navigator.Find("Earth",0),moonSearch=navigator.Find("Moon",0),tonSearch=navigator.Find("TON 618",0);
    Require(!earthSearch.empty() && earthSearch[0].name=="Earth","Object finder lost Earth");
    Require(!moonSearch.empty() && moonSearch[0].parent=="Earth","Moon hierarchy lost Earth parent");
    const auto jovian=navigator.Find("Jupiter",0);
    const auto satellites=navigator.Children(jovian[0],0);
    const auto wormholes=navigator.Find("Saturn Kerr Wormhole",0);
    Require(wormholes.size()==1 && wormholes[0].wormhole && wormholes[0].parent=="Saturn","Wormhole missing from Object Finder");
    Require(navigator.Parent(wormholes[0]).name=="Saturn","Wormhole hierarchy does not lead back to Saturn");
    const auto saturnChildren=navigator.Children(navigator.Object(-2,5,0),0);
    Require(std::any_of(saturnChildren.begin(),saturnChildren.end(),[](const auto& e){return e.wormhole;}),"Saturn overview omits its wormhole");
    Require(std::count_if(satellites.begin(),satellites.end(),[](const auto& e){return e.parent=="Jupiter";})==4,"Jupiter satellite overview is incomplete");
    Require(navigator.Parent(moonSearch[0]).name=="Earth" && navigator.Parent(jovian[0]).name=="Sun","Hierarchy Back did not reach the physical parent");
    Require(!tonSearch.empty() && tonSearch[0].system==GalacticCores::tonId && GalacticCores::Get(GalacticCores::tonId).massSolar>4e10,"TON 618 is absent or incorrectly scaled");
    const auto generatedName=Universe::StarName(37);StarSystem named;named.Generate(47,generatedName);
    Require(!navigator.Find(named.Planets().back().name,0).empty(),"Unvisited generated planet is not findable by name");
    Require(!navigator.Find("Solar asteroid 4096",0).empty(),"Small-body name lookup failed");
    Require(!navigator.Find("Earth satellite barycentre",0).empty(),"Computed barycentre cannot be found by its name");
    for(const auto& nebula:CelestialPhenomena::Nebulae()){
        Require(!navigator.Find(nebula.name,0).empty(),"Nebula is not findable by name");
        Require(Universe::GalaxyAt(0).Contains(nebula.positionAu) && nebula.radiusLy>0,"Nebula has invalid Galactic coordinates or size");
    }
    const auto nearby=navigator.Nearby(glm::dvec3(0),glm::dvec3(1,0,0),20,0);
    Require(!nearby.empty() && std::is_sorted(nearby.begin(),nearby.end(),[](const auto& a,const auto& b){return a.distanceAu<b.distanceAu;}),"Nearby search is not distance sorted");
    const glm::dvec3 distant(1e15,1e15,1e15);NavigationEntry precision;precision.anchorAu=distant;precision.localAu={.0000000001,0,0};
    Require(precision.Distance(distant,glm::dvec3(0))==1e-10,"Hierarchical distance flattened local precision");
    bool rejected=false;try{navigator.Nearby(glm::dvec3(0),glm::dvec3(0),-1,0);}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected,"Nearby search accepted a negative radius");
    std::cout<<"Navigation checks: coordinate precision, nearest-star tree, global names, planetary satellites, TON 618 and flight units passed\n";
    Require(catalogue.Positions().size()==Universe::StarCount,"Stellar catalogue size changed");
    {
        const auto core=MilkyWayModel::ToWorldAu({4,0,0}),rim=MilkyWayModel::ToWorldAu({15.7,0,0});
        Require(Universe::StellarDensity(0,core)>Universe::StellarDensity(0,rim)*20,"Star density does not thin at the Galactic rim");
        Require(Universe::StellarDensity(0,core)>Universe::StellarDensity(0,MilkyWayModel::ToWorldAu({4,4,0}))*20,"Star density ignores height above disk");
        size_t mainResidents=0;
        for(int galaxy:{0,1,4,5,6,7,8,9,10,27,Universe::StreamGalaxyCount-1}){
            const auto group=Universe::GalaxyAt(galaxy);
            const auto position=galaxy==0?glm::dvec3(0):group.centerAu;
            catalogue.UpdateStream(position,7);
            Require(!catalogue.StreamStars().empty(),"Entered galaxy has no seeded stars");
            const auto saved=catalogue.StreamStars();const auto revision=catalogue.StreamRevision();
            Require(!catalogue.UpdateStream(position,7) && catalogue.StreamRevision()==revision,"Stationary camera regenerated its star cells");
            for(size_t i=0;i<saved.size();i+=std::max<size_t>(1,saved.size()/12)){
                const auto& star=saved[i];const int id=Universe::StarId(star.index);
                Require(Universe::StarIndex(id)==star.index && Universe::StreamExists(star.index),"Streamed star identity is invalid");
                Require(Universe::StarPosition(star.index)==star.position,"Seeded cell position depends on camera");
                Require(Universe::StarPosition(star.index,619)!=star.position,"Changing seed did not change a streamed star");
                Require(catalogue.Nearest(star.position)==id,"Nearest lookup omitted streamed star");
                const auto found=navigator.Find(Universe::StarName(star.index),0);
                Require(!found.empty() && found.front().system==id,"Streamed star name is not globally searchable");
                StarSystem first,again;first.Generate(id,Universe::StarName(star.index));again.Generate(id,Universe::StarName(star.index));
                Require(first.Planets().size()==again.Planets().size() && first.Planets()[0].seed==again.Planets()[0].seed,"Streamed planetary system changed after revisit");
            }
            catalogue.UpdateStream(glm::dvec3(1e18),7);Require(catalogue.StreamStars().empty() && catalogue.ResidentCells()==0,"Distant travel retained obsolete cells");
            catalogue.UpdateStream(position,7);
            Require(catalogue.StreamStars().size()==saved.size(),"Revisit changed seeded star count");
            for(size_t i=0;i<saved.size();++i)Require(catalogue.StreamStars()[i].index==saved[i].index && catalogue.StreamStars()[i].position==saved[i].position,"Revisit changed seeded cell contents");
            catalogue.UpdateStream(position,13);
            Require(catalogue.StreamStars().size()>=saved.size(),"Higher camera magnitude removed resident stars");
            mainResidents=std::max(mainResidents,catalogue.StreamStars().size());
        }
        const auto m32=navigator.Find("NGC221",0);Require(!m32.empty() && m32.front().name=="M32 (NGC 221)","M32 alias missing");
        Require(navigator.Parent(m32.front()).name=="Andromeda (M31)","M32 hierarchy lost Andromeda");
        std::cout<<"Seeded streaming: "<<Universe::StreamGalaxyCount<<" galaxy domains, "<<Universe::StreamCount<<" virtual candidate identities; maximum sampled residents="<<mainResidents<<"; repeatability, density, magnitude, eviction and navigation passed\n";
        catalogue.UpdateStream(glm::dvec3(1e18),7);
    }
    {
        const auto m31=Universe::GalaxyAt(4);
        const auto sunDirection=glm::normalize(m31.centerAu);
        const glm::dvec3 queries[]={glm::dvec3(0),m31.centerAu-sunDirection*m31.axesAu.x*3.5,
            m31.centerAu+m31.rotation*glm::dvec3(m31.axesAu.x*.55,0,0),glm::dvec3(8e13,-2e13,3e13)};
        for(const auto& point:queries){
            catalogue.UpdateStream(point,13);
            const auto verify=[&](){
                int expected=-2;double best=glm::dot(point,point);
                const auto consider=[&](int id,const glm::dvec3& position){const auto d=position-point;const double r=glm::dot(d,d);if(r<best){best=r;expected=id;}};
                for(int i=0;i<Universe::StarCount;++i)consider(Universe::StarId(i),catalogue.Positions()[i]);
                for(const auto& star:catalogue.StreamStars())consider(Universe::StarId(star.index),star.position);
                Require(catalogue.Nearest(point)==expected,"Bounded nearest search disagrees with full population scan");
                const auto visits=catalogue.LastNearestTreeVisits();
                Require(visits<20000,"Remote galaxy nearest lookup traversed too much of the catalogue");
                Require(catalogue.Nearest(point)==expected && catalogue.LastNearestTreeVisits()==0,"Repeated nearest lookup was not cached");
            };
            verify();
            std::vector<std::pair<double,int>> reference;
            const double radius=1e6*Astronomy::auPerLightYear,radius2=radius*radius;
            const auto consider=[&](int id,const glm::dvec3& p){const auto d=p-point;const double r=glm::dot(d,d);if(r<=radius2)reference.emplace_back(r,id);};
            for(int i=0;i<Universe::StarCount;++i)consider(Universe::StarId(i),catalogue.Positions()[i]);
            for(const auto& star:catalogue.StreamStars())consider(Universe::StarId(star.index),star.position);
            const size_t count=std::min<size_t>(20,reference.size());
            std::partial_sort(reference.begin(),reference.begin()+count,reference.end());
            const auto closest=catalogue.Closest(point,radius,20);Require(closest.size()==count,"Bounded radius query lost entries");
            for(size_t i=0;i<count;++i)Require(closest[i]==reference[i].second,"Bounded radius query changed nearest ordering");
            catalogue.UpdateStream(glm::dvec3(1e18),13);verify(); // Resident eviction must invalidate nearest results.
        }
        std::cout<<"Spatial bounds: nearest and radius queries match exhaustive scans; repeated queries cached; remote traversal budget passed\n";
    }
    for(int index=0;index<4;++index){
        const auto first=Universe::GalaxyAt(index),again=Universe::GalaxyAt(index);
        Require(first.key==again.key && first.centerAu==again.centerAu && first.axesAu==again.axesAu
            && first.rotation==again.rotation,"Galaxy generation changed for the same seed");
        Require(first.key!=Universe::GalaxyAt(index,619).key,"World seed did not change galaxy key");
        if(index>0)Require(first.centerAu!=Universe::GalaxyAt(index,619).centerAu,"World seed did not change galaxy position");
    }
    const std::array<Universe::Galaxy,4> boundaries={Universe::GalaxyAt(0),Universe::GalaxyAt(1),Universe::GalaxyAt(2),Universe::GalaxyAt(3)};
    size_t formerSolarBall=0;
    for(int index=0;index<Universe::StarCount;++index){
        const int galaxy=Universe::GalaxyForStar(index);
        const auto position=catalogue.Positions()[index];
        if(index>=Universe::LegacyStarCount && index<Universe::LegacyStarCount+262144 && glm::length(position)<2500*Astronomy::auPerLightYear)++formerSolarBall;
        Require(boundaries[galaxy].Contains(position),"Star generated outside galaxy boundary");
        if(index%1543==0){Require(position==Universe::StarPosition(index),"Camera-independent star key is not repeatable");
        Require(Universe::GalaxyAt(galaxy,619).Contains(Universe::StarPosition(index,619)),"Alternate-seed star outside galaxy boundary");}
        if(index%137==0){
            const auto nearby=catalogue.Nearby(position+glm::dvec3(.1,0,0));
            Require(std::find(nearby.begin(),nearby.end(),Universe::StarId(index))!=nearby.end(),"Proximity lookup lost a star");
        }
    }
    Require(formerSolarBall<5000,"A dense spherical procedural population remains around the Sun");
    std::cout<<"Former 262144-star Solar ball now contains "<<formerSolarBall<<" of those density-distributed entries within 2500 ly\n";
    Require(Universe::StarPosition(37)!=Universe::StarPosition(37,619),"World seed did not change stars");
    Require(std::abs(CameraZoom::Magnification(45,CameraZoom::MinimumFieldOfView(45))-20)<1e-5,"Zoom did not reach 20x");
    Require(std::abs(CameraZoom::MagnitudeLimit(7,20)-13.50515f)<.001 && CameraZoom::MagnitudeLimit(7,1)==7 && CameraZoom::MagnitudeLimit(29,20)==30,"Zoom magnitude boost/clamp failed");
    const auto lod=PlanetTerrain::SurfaceLodVertices(6371000);
    Require(lod.size()<200000 && lod.size()>30000,"Surface LOD memory budget incorrect");
    double nearMaxEdge=0,farMaxEdge=0;
    for(size_t i=0;i<lod.size();i+=3){
        const auto a=glm::dvec3(lod[i]),b=glm::dvec3(lod[i+1]),c=glm::dvec3(lod[i+2]);
        Require(glm::cross(b-a,c-a).z>0,"Surface LOD has reversed or degenerate triangle");
        const double r=std::max({glm::length(a),glm::length(b),glm::length(c)});
        const double edge=std::max({glm::length(a-b),glm::length(a-c),glm::length(b-c)});
        if(r<=100.01)nearMaxEdge=std::max(nearMaxEdge,edge);
        if(r>1000)farMaxEdge=std::max(farMaxEdge,edge);
    }
    Require(nearMaxEdge<4 && farMaxEdge>100,"Terrain mesh does not lower detail with distance");
    {
        const auto profile=PlanetWeather::Miller();
        const auto calm=PlanetWeather::Make(profile,12345,0,0),breeze=PlanetWeather::Make(profile,12345,1,0),gale=PlanetWeather::Make(profile,12345,3,0);
        const auto repeated=PlanetWeather::Make(profile,12345,1,0);
        Require(breeze.phases==repeated.phases && breeze.spectrum==repeated.spectrum,"Paused weather is not deterministic");
        double breezeEnergy=0,galeEnergy=0;
        for(int i=0;i<512;++i){
            const glm::dvec3 point(i*2.71,10,i*.31);const auto n=glm::normalize(glm::vec3(.3,.4,.7));
            const double b=PlanetWeather::WaveHeight(breeze,point,n),g=PlanetWeather::WaveHeight(gale,point,n);
            Require(PlanetWeather::WaveHeight(calm,point,n)==0,"Calm wind retained wind-driven waves");
            Require(std::abs(b-PlanetWeather::WaveHeight(breeze,point+glm::dvec3(4096,0,0),n))<.0001,"Ocean coordinates wrap with a visible seam");
            breezeEnergy+=b*b;galeEnergy+=g*g;
        }
        Require(galeEnergy>breezeEnergy*3,"Wind speed did not increase ocean energy");
        StarSystem earth;earth.LoadSolarSystem();const auto& body=earth.Planets()[2];
        PlanetWeather::Profile earthProfile{body.seed,6371000,body.seaLevel,288,8,false,true};
        const auto storms=PlanetWeather::Make(earthProfile,0);
        int active=0;
        for(int i=0;i<3;++i)if(storms.storms[i].w>0){
            ++active;const auto n=glm::vec3(storms.storms[i]);
            Require(std::abs(n.y)>.087 && std::abs(n.y)<.6 && PlanetWeather::Ocean(earthProfile,n),"Tropical cyclone formed at a pole/equator or over land");
            const auto east=glm::normalize(glm::cross(glm::vec3(0,1,0),n));const float a=storms.shapes[i].x*.23f;
            const auto wall=n*std::cos(a)+east*std::sin(a);
            Require(PlanetWeather::WindSpeed(storms,wall)>PlanetWeather::WindSpeed(storms,n)+10,"Cyclone eye is not calmer than the eyewall");
            Require(storms.shapes[i].y*n.y>0,"Cyclone rotation has the wrong hemisphere");
        }
        Require(active>0,"Seeded Earth weather has no ocean storms");
        auto hot=earthProfile;hot.temperature=700;
        for(const auto& storm:PlanetWeather::Make(hot,0).storms)Require(storm.w==0,"Water cyclone formed on an overheated planet");
        const auto later=PlanetWeather::Make(profile,12345+1800);
        Require(later.phases!=breeze.phases,"Ocean waves do not advance with simulation time");
        for(double t:{-1e9,1e12})Require(std::isfinite(PlanetWeather::WaveHeight(PlanetWeather::Make(profile,t),{12,34,56},{0,1,0})),"Accelerated weather produced invalid waves");
        Require(earth.WeatherState(2,0).auroraAxis.w>0 && earth.WeatherState(0,0).auroraAxis.w==0 && earth.WeatherState(1,0).auroraAxis.w==0,"Aurora eligibility ignored magnetic activity or atmosphere");
        auto auroraProfile=profile;auroraProfile.metalCore=false;
        Require(PlanetWeather::Make(auroraProfile,0).auroraAxis.w==0,"Aurora ignored metal core eligibility");
        const auto panurge=InterstellarWorlds::CompanionOrbit(InterstellarWorlds::neutronStarId-4),miller=InterstellarWorlds::Get(0).orbit;
        Require(miller.semiMajorAxis*(1+miller.eccentricity)<panurge.semiMajorAxis*(1-panurge.eccentricity),"Miller's orbit crossed outside Panurge's periapsis");
        const auto neutron=StarSystem::DescribeStar(InterstellarWorlds::neutronStarId,"Panurge");
        Require(neutron.classification.find("Neutron")!=std::string::npos && std::abs(neutron.radiusSolar*Astronomy::solarRadiusAu*Astronomy::metresPerAu-12000)<.01,"Panurge is not a compact neutron-star companion");
        std::cout<<"Weather: deterministic circulation/storms, ocean-only cyclones, calm eyes, wind-driven wave energy and periodic coordinates passed\n";
    }
    {
        BlackHoleJourney journey;
        const double horizon=KerrSpacetime::Horizon(Astronomy::gargantuaHorizonAu,.8);
        const double kmToAu=1000/Astronomy::metresPerAu;
        for(int frame=0;frame<480;++frame)journey.Advance(0,true,.25,horizon-999*kmToAu,horizon);
        Require(!journey.tesseract,"A stationary camera reached the interior through a timer");
        journey.Advance(0,true,.016,horizon-1001*kmToAu,horizon);
        Require(journey.gridBlend>0 && journey.gridBlend<1 && journey.interiorBlend==0,"Grid did not start just beyond 1,000 km");
        double lastBlend=0;
        for(int i=0;i<=500;++i){journey.Advance(0,true,.016,horizon*(1-i*.001),horizon);
            Require(journey.interiorBlend>=lastBlend && journey.interiorBlend-lastBlend<.010,"Interior reveal is discontinuous with depth");lastBlend=journey.interiorBlend;}
        Require(journey.tesseract && journey.interiorBlend==1,"Gargantua spatial interior did not appear");
        journey.Advance(0,true,.016,horizon-999*kmToAu,horizon);
        Require(!journey.tesseract && journey.interiorBlend==0,"Outward flight retained the grid inside the cutoff");
        journey.Advance(0,false,.25,horizon,horizon);Require(!journey.inside && !journey.tesseract && journey.insideSeconds==0,"Leaving the horizon retained the interior scene");
        for(int id=GalacticCores::firstId;id<GalacticCores::firstId+GalacticCores::count;++id){
            for(int frame=0;frame<80;++frame)journey.Advance(id,true,.25,.1,horizon);
            Require(journey.inside && !journey.tesseract,"A non-Gargantua black hole acquired a tesseract");
        }
        const double r=WormholePhysics::ThroatRadiusAu();const auto rotation=WormholePhysics::SaturnToGargantuaRotation();
        const glm::dvec3 a(12,3,-2),b(300,40,-8),direction(0,0,1);
        const auto crossing=WormholeTransit::Sweep(a+direction*r*3.,a-direction*r*3.,a,a,b,rotation,r);
        Require(crossing.entered && glm::length(crossing.position-b)>r*1.5,"Fast transit failed to exit the paired throat");
        Require(glm::length(glm::transpose(rotation)*(rotation*direction)-direction)<1e-12,"Wormhole orientations are not reciprocal");
        Require(!WormholeTransit::Sweep(a+direction*r*3.,a+direction*r*2.,a,a,b,rotation,r).entered,"A near approach triggered premature transit");
        Require(!WormholeTransit::Sweep(a,a-direction*r*3.,a,a,b,rotation,r).entered,"An arrival inside the mouth immediately bounced back");
        const auto miss=glm::dvec3(r*1.1,0,0);
        Require(!WormholeTransit::Sweep(a+miss+direction*r*3.,a+miss-direction*r*3.,a,a,b,rotation,r).entered,"A grazing miss triggered wormhole transit");
        const auto reverse=WormholeTransit::Sweep(b+direction*r*3.,b-direction*r*3.,b,b,a,glm::transpose(rotation),r);
        Require(reverse.entered && glm::length(reverse.position-a)<r*6,"Return wormhole transit failed");
        glm::dvec3 landing(0,-6,0);Require(TesseractSpace::Land({0,2,0},landing)
            && std::abs(landing.y-(TesseractSpace::deckTop+TesseractSpace::eyeHeight))<1e-12,"Interior landing tunneled through the deck");
        glm::dvec3 gap(TesseractSpace::roomPitch.x*.5,-6,0);Require(!TesseractSpace::Land({gap.x,2,0},gap),"Interior landing hit an empty corridor gap");
        glm::dvec3 upper(0,4,0);Require(!TesseractSpace::Land({0,1,0},upper),"Ascending interior camera snapped to a deck");
        std::cout<<"Journey checks: spatial continuous reveal, stationary camera, deck collisions; reciprocal wormhole crossings passed\n";
    }
    std::cout<<"Galaxy bounds/repeatability: "<<Universe::StarCount<<" stars; terrain vertices="<<lod.size()<<"\n";
    auto ship=InterstellarWorlds::DepartureState(0);
    {
        const double mu=Astronomy::gargantuaGravityAu,c=Astronomy::lightSpeedAu,rs=Astronomy::gargantuaHorizonAu;
        const double r=8*rs,omega=std::sqrt(mu/(r*r*r));
        OrbitalMechanics::ParticleState circular{{r,0,0},{0,r*omega,0}};
        const double rate=OrbitalMechanics::SchwarzschildClockRate(circular,rs,c);
        Require(std::abs(rate-std::sqrt(1-1.5*rs/r))<1e-12,"Schwarzschild circular-orbit clock is wrong");
        const double energy=OrbitalMechanics::SchwarzschildEnergy(circular,rs,c);
        const double period=glm::two_pi<double>()/omega;
        double proper=0;
        for(int step=0;step<4000;++step)proper+=OrbitalMechanics::AdvanceSchwarzschild(circular,mu,c,0,period/4000);
        Require(std::abs(glm::length(circular.position)/r-1)<1e-8,"Relativistic circular orbit drifted");
        Require(std::abs(OrbitalMechanics::SchwarzschildEnergy(circular,rs,c)-energy)<1e-10,"Relativistic geodesic lost energy");
        Require(std::abs(proper/period-rate)<1e-9,"Proper-time integration disagrees with circular solution");
        const auto delay=[&](double radius){return OrbitalMechanics::LightTravelDelay({radius,0,0},{100*rs,0,0},mu,rs);};
        Require(delay(1.000001*rs)>delay(1.001*rs),"Near-horizon received-light delay did not increase");
        const auto separated=InterstellarWorlds::SeparateRangers(ship);
        Require(glm::length((separated[0].velocity+separated[1].velocity)*.5-ship.velocity)<1e-16,"Symmetric Ranger release changed assembly momentum");
        std::cout<<"Schwarzschild circular clock="<<rate<<"; conserved energy="<<energy<<"\n";
    }
    const auto mann=InterstellarWorlds::Orientation()*OrbitalMechanics::PositionAtTimeDouble(InterstellarWorlds::Get(1).orbit,Astronomy::gargantuaGravityAu,0);
    Require(glm::length(ship.position-mann)*Astronomy::metresPerAu<7e6,"Craft did not depart near Mann");
    const double initialSpeed=glm::length(ship.velocity);
    double fuel=InterstellarWorlds::FuelSeconds,time=0,minRadius=1e9,maxSpeed=initialSpeed,properTotal=0;
    bool firing=false,burned=false;
    while(time<180*Astronomy::secondsPerDay){
        const double before=fuel;
        double proper=0;
        time+=InterstellarWorlds::AdvanceMission(ship,fuel,firing,false,180*Astronomy::secondsPerDay-time,&proper);
        properTotal+=proper;
        burned=burned || fuel<before;
        minRadius=std::min(minRadius,glm::length(ship.position));maxSpeed=std::max(maxSpeed,glm::length(ship.velocity));
    }
    Require(burned && fuel>=0,"Autonomous periapsis burn did not occur");
    Require(fuel==0 && properTotal<time && properTotal>time*.8,"Fuel or proper-time clock was not integrated correctly");
    Require(minRadius>3*Astronomy::gargantuaHorizonAu && maxSpeed>initialSpeed*4,"Gravity-assisted inbound acceleration failed");
    Require(InterstellarWorlds::SpecificEnergy(ship)>0 && glm::dot(ship.position,ship.velocity)>0,"Craft did not escape on an unbound outbound trajectory");
    std::cout<<"Mann flyby: periapsis="<<minRadius<<" AU; fuel remaining="<<fuel<<" s; escape energy="<<InterstellarWorlds::SpecificEnergy(ship)<<"\n";
    std::cout << Examples().size()
              << " quantum presets checked; solar scale, hierarchy and generation checks passed.\n";
    }catch(const std::exception& error){std::cerr<<"Simulation checks FAILED: "<<error.what()<<"\n";return 1;}
    return 0;
}
