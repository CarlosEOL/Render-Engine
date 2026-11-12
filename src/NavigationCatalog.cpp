#include "NavigationCatalog.h"
#include "GalacticCore.h"
#include "CelestialPhenomena.h"
#include "StellarPhotometry.h"
#include "InterstellarWorlds.h"
#include "Astronomy.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace {
using Kind=NavigationEntry::Kind;
std::string Normalize(const std::string& text){std::string out;for(unsigned char c:text)if(std::isalnum(c))out+=static_cast<char>(std::tolower(c));return out;}
std::string Key(int id){return "system:"+std::to_string(id);}
NavigationEntry Galaxy(int index){NavigationEntry e;e.kind=Kind::Galaxy;e.system=index;e.key="galaxy:"+std::to_string(index);e.parentKey="universe";e.name=index==0?"Milky Way":"Galaxy "+std::to_string(index);e.parent="Universe";e.type=index==0?"Barred spiral galaxy":"Procedural galaxy";e.anchorAu=Universe::GalaxyAt(index).centerAu;e.color={.7,.56,.37};return e;}
void Load(StarSystem& model,int id,const std::string& name){if(id==-2)model.LoadSolarSystem();else if(GalacticCores::IsCore(id))model.LoadCore(id);else model.Generate(id,name);}
}
NavigationEntry NavigationCatalog::UniverseRoot() const{NavigationEntry e;e.kind=Kind::Universe;e.key="universe";e.name="Universe";e.type="Catalogue root";return e;}
void NavigationCatalog::Initialize(const Universe::Catalog& source){
    catalog=&source;systems.clear();systems.reserve(Universe::LegacyStarCount+12);
    const auto add=[&](int id,const std::string& name,const glm::dvec3& position){
        NavigationEntry e;e.system=id;e.key=Key(id);e.name=name;e.anchorAu=position;
        if(GalacticCores::IsCore(id)){
            const auto core=GalacticCores::Get(id);e.kind=Kind::Core;e.type=core.classification;e.diameterKm=core.RadiusAu()*2*Astronomy::metresPerAu/1000;e.color={1,.72,.36};
            e.parentKey=id==GalacticCores::tonId?"universe":"galaxy:"+std::to_string(id-GalacticCores::firstId);e.parent=id==GalacticCores::tonId?"TON 618 host galaxy":id==GalacticCores::firstId?"Milky Way":"Galaxy "+std::to_string(id-GalacticCores::firstId);
        }else if(id==0){e.kind=Kind::Core;e.type="Fictional supermassive black hole";e.parentKey="universe";e.parent="Authored Interstellar system";e.diameterKm=2*Astronomy::gargantuaHorizonAu*Astronomy::metresPerAu/1000;e.color={1,.65,.22};}
        else{
            StarSystem::Star star;if(id==-2){StarSystem solar;solar.LoadSolarSystem();star=solar.Host();}else star=StarSystem::DescribeStar(id,name);
            e.type=star.classification;e.temperature=star.temperature;e.luminosity=star.luminositySolar;e.diameterKm=2*star.radiusSolar*Astronomy::solarRadiusAu*Astronomy::metresPerAu/1000;e.color=star.temperature>7500?glm::vec3(.55,.72,1):star.temperature<4500?glm::vec3(1,.45,.18):glm::vec3(1,.85,.50);
            const int galaxy=Universe::IsStar(id)?Universe::GalaxyForStar(Universe::StarIndex(id)):0;e.parentKey=id>=4 && id<=8?Key(0):"galaxy:"+std::to_string(galaxy);e.parent=id>=4 && id<=8?"Gargantua":galaxy==0?"Milky Way":"Galaxy "+std::to_string(galaxy);
        }
        systems.push_back(std::move(e));
    };
    add(-2,"Sun",glm::dvec3(0));add(0,"Gargantua",glm::dvec3(0));
    for(int id=4;id<=8;++id)add(id,"Orbiting star "+std::to_string(id-3),glm::dvec3(0));
    for(int i=0;i<Universe::LegacyStarCount;++i)add(Universe::StarId(i),Universe::StarName(i),source.Positions()[i]);
    for(int i=0;i<GalacticCores::count;++i){const auto core=GalacticCores::Get(GalacticCores::firstId+i);add(core.id,core.name,core.position);}
    for(const auto& nebula:CelestialPhenomena::Nebulae()){NavigationEntry e;e.kind=Kind::Nebula;e.system=nebula.id;e.key="nebula:"+std::to_string(nebula.id);e.parentKey="galaxy:0";e.name=nebula.name;e.parent="Milky Way";e.type=nebula.classification;e.anchorAu=nebula.positionAu;e.diameterKm=nebula.radiusLy*2*Astronomy::auPerLightYear*Astronomy::metresPerAu/1000;e.color=nebula.color;systems.push_back(e);}
}
NavigationEntry NavigationCatalog::System(int id,double seconds) const{
    const int index=Universe::StarIndex(id);
    if(index>=Universe::LegacyStarCount){
        NavigationEntry e;e.system=id;e.key=Key(id);e.name=Universe::StarName(index);e.anchorAu=catalog->Positions()[index];
        const auto star=StarSystem::DescribeStar(id,e.name);e.type=star.classification;e.temperature=star.temperature;e.luminosity=star.luminositySolar;
        e.diameterKm=2*star.radiusSolar*Astronomy::solarRadiusAu*Astronomy::metresPerAu/1000;
        const int galaxy=Universe::GalaxyForStar(index);e.parentKey="galaxy:"+std::to_string(galaxy);e.parent=galaxy==0?"Milky Way":"Galaxy "+std::to_string(galaxy);
        e.color=star.temperature>7500?glm::vec3(.55,.72,1):star.temperature<4500?glm::vec3(1,.45,.18):glm::vec3(1,.85,.50);return e;
    }
    if(index>=0)return systems.at(7+index);
    for(auto entry:systems)if(entry.system==id){if(id>=4 && id<=8)entry.anchorAu=InterstellarWorlds::Orientation()*OrbitalMechanics::PositionAtTimeDouble(InterstellarWorlds::CompanionOrbit(id-4),Astronomy::gargantuaGravityAu,seconds);return entry;}
    throw std::invalid_argument("Unknown navigation system");
}
std::vector<NavigationEntry> NavigationCatalog::Bodies(int id,double seconds){
    const auto root=System(id,seconds);
    if(id==0){
        std::vector<NavigationEntry> out;
        for(int i=0;i<3;++i){const auto& body=InterstellarWorlds::Get(i);NavigationEntry e;e.kind=Kind::Planet;e.system=0;e.body=i+1;e.key=Key(0)+"/"+std::to_string(e.body);e.parentKey=Key(0);e.name=body.name;e.parent="Gargantua";e.type=body.classification;e.localAu=InterstellarWorlds::Orientation()*OrbitalMechanics::PositionAtTimeDouble(body.orbit,Astronomy::gargantuaGravityAu,seconds);e.diameterKm=body.radiusEarth*6371.0084*2;e.color=body.color;out.push_back(e);}
        NavigationEntry craft;craft.kind=Kind::Craft;craft.system=0;craft.body=9;craft.key="endurance";craft.parentKey=Key(0);craft.name="Endurance";craft.parent="Gargantua";craft.type="Spacecraft / finite-fuel gravity assist";craft.diameterKm=.05;craft.color={.65,.75,.8};out.push_back(craft);
        for(int i=0;i<2;++i){auto ranger=craft;ranger.body=10+i;ranger.key="ranger:"+std::to_string(i);ranger.parentKey=craft.key;ranger.parent="Endurance";ranger.name="Ranger "+std::to_string(i+1);ranger.type="Detachable spacecraft";ranger.diameterKm=.015;out.push_back(ranger);}
        return out;
    }
    if(GalacticCores::IsCore(id) || CelestialPhenomena::IsNebula(id))return {};
    // Only instantiate a queried system; inactive systems never integrate orbits.
    StarSystem model;Load(model,id,root.name);std::vector<NavigationEntry> out;out.reserve(model.Planets().size());
    for(int i=0;i<static_cast<int>(model.Planets().size());++i){
        const auto& body=model.Planets()[i];NavigationEntry e;e.kind=Kind::Planet;e.wormhole=body.wormhole;e.system=id;e.body=i;e.key=Key(id)+"/"+std::to_string(i);e.parentKey=body.parent<0?Key(id):Key(id)+"/"+std::to_string(body.parent);e.name=body.name;e.parent=body.parent<0?root.name:model.Planets()[body.parent].name;e.type=body.classification;e.anchorAu=root.anchorAu;e.localAu=model.Position(i,seconds);e.diameterKm=body.radiusEarth*6371.0084*2;e.temperature=body.temperature;e.color=body.color;e.massEarth=body.massEarth;out.push_back(e);
    }
    for(auto& body:out)body.satelliteCount=static_cast<int>(std::count_if(out.begin(),out.end(),[&](const auto& child){return child.parentKey==body.key;}));
    return out;
}
std::vector<NavigationEntry> NavigationCatalog::Find(const std::string& query,double seconds){
    const auto q=Normalize(query);std::vector<NavigationEntry> result;
    const auto matches=[&](const NavigationEntry& entry){auto name=Normalize(entry.name);return q.empty() || name.find(q)!=std::string::npos || (entry.system==-2 && entry.kind==Kind::Star && q=="solarsystem") || (entry.kind==Kind::Core && q=="blackhole") || (entry.name=="Sagittarius A*" && (q=="sgra" || q=="sgra*"));};
    const std::string prefix="proceduralstar";
    if(q.rfind(prefix,0)==0){
        const auto start=prefix.size();size_t end=start;while(end<q.size() && std::isdigit(static_cast<unsigned char>(q[end])))++end;
        if(end>start){
            try{const auto serial=std::stoll(q.substr(start,end-start));
                if(serial>0 && serial<=Universe::StarCount-Universe::LegacyStarCount){const int id=Universe::StarId(Universe::LegacyStarCount+static_cast<int>(serial)-1);auto e=System(id,seconds);if(matches(e))result.push_back(e);for(auto body:Bodies(id,seconds))if(matches(body))result.push_back(std::move(body));}
            }catch(const std::exception&){} // Invalid/overflowing names simply have no result.
        }else if(q==prefix){for(int i=0;i<1000;++i)result.push_back(System(Universe::StarId(Universe::LegacyStarCount+i),seconds));}
    }
    for(int i=0;i<4;++i){auto galaxy=Galaxy(i);if(matches(galaxy))result.push_back(galaxy);}
    for(auto system:systems){
        if(system.system>=4 && system.system<=8)system=System(system.system,seconds);
        if(matches(system))result.push_back(system);
        if(system.system==0 || system.system==-2 || (!q.empty() && q.find(Normalize(system.name))!=std::string::npos))
            for(auto entry:Bodies(system.system,seconds)){
                if(matches(entry))result.push_back(entry);
                if(q.find("barycentre")!=std::string::npos && entry.satelliteCount>0)
                    for(const auto& centre:Children(entry,seconds))if(centre.kind==Kind::Barycentre && matches(centre))result.push_back(centre);
            }
    }
    // Every representative small body has a stable, searchable identity.
    if(q.find("asteroid")!=std::string::npos || q.find("kuiper")!=std::string::npos){
        NavigationEntry group;group.system=-2;group.kind=Kind::Group;group.key="small-bodies";group.name="Solar small bodies";
        for(auto entry:Children(group,seconds))if(matches(entry))result.push_back(std::move(entry));
    }
    std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b){const bool exactA=Normalize(a.name)==q,exactB=Normalize(b.name)==q;return exactA!=exactB?exactA:a.name<b.name;});
    if(result.size()>1000)result.resize(1000);return result;
}
std::vector<NavigationEntry> NavigationCatalog::Nearby(const glm::dvec3& anchor,const glm::dvec3& local,double radiusLy,double seconds){
    if(!std::isfinite(radiusLy) || radiusLy<=0 || radiusLy>1e12)throw std::invalid_argument("Enter a radius greater than zero, up to 1e12 light-years.");
    std::vector<NavigationEntry> result;const double radius=radiusLy*Astronomy::auPerLightYear;
    for(auto entry:systems){if(entry.kind==Kind::Nebula)continue;if(entry.system>=4 && entry.system<=8)entry=System(entry.system,seconds);entry.distanceAu=entry.Distance(anchor,local);if(entry.distanceAu<=radius)result.push_back(std::move(entry));}
    for(int id:catalog->Closest(anchor+local,radius,1000))if(Universe::StarIndex(id)>=Universe::LegacyStarCount){auto e=System(id,seconds);e.distanceAu=e.Distance(anchor,local);result.push_back(std::move(e));}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.distanceAu<b.distanceAu;});if(result.size()>1000)result.resize(1000);
    for(auto& entry:result){if(entry.kind==Kind::Star){StarSystem model;Load(model,entry.system,entry.name);entry.planetCount=static_cast<int>(model.Planets().size());}}
    return result;
}
std::vector<NavigationEntry> NavigationCatalog::Children(const NavigationEntry& parent,double seconds){
    std::vector<NavigationEntry> result;
    if(parent.kind==Kind::Universe){for(int i=0;i<4;++i)result.push_back(Galaxy(i));result.push_back(System(0));result.push_back(System(GalacticCores::tonId));return result;}
    if(parent.kind==Kind::Galaxy){for(const auto& e:systems)if(e.parentKey==parent.key)result.push_back(e);if(result.size()>500)result.resize(500);return result;}
    if(parent.kind==Kind::Group){
        StarSystem solar;solar.LoadSolarSystem();int i=0;for(const auto& orbit:solar.AsteroidOrbits()){
            NavigationEntry e;e.kind=Kind::Asteroid;e.system=-2;e.body=i++;e.key="asteroid:"+std::to_string(e.body);e.parentKey="small-bodies";std::ostringstream name;name<<"Solar asteroid "<<std::setfill('0')<<std::setw(4)<<e.body+1;e.name=name.str();e.parent="Sun";e.type="Representative belt / Kuiper object";const auto p=OrbitalMechanics::PositionAtTimeDouble(orbit,Astronomy::solarGravityAu,seconds);e.localAu={p.x,p.z,-p.y};e.color={.4,.35,.3};result.push_back(e);
        }return result;
    }
    if(parent.kind==Kind::Star || parent.kind==Kind::Core || parent.kind==Kind::Planet || parent.kind==Kind::Craft || parent.kind==Kind::Barycentre){
        const auto root=System(parent.system,seconds);const auto bodies=Bodies(parent.system,seconds);
        if(parent.kind==Kind::Star || parent.kind==Kind::Core){result.push_back(root);for(const auto& e:bodies)if(e.parentKey==root.key)result.push_back(e);if(parent.system==0)for(int i=4;i<=8;++i)result.push_back(System(i,seconds));}
        else {
            const std::string scope=parent.kind==Kind::Barycentre?parent.parentKey:parent.key;
            NavigationEntry body=parent;if(parent.kind==Kind::Barycentre){for(const auto& e:bodies)if(e.key==scope)body=e;}
            result.push_back(body);double mass=body.massEarth;glm::dvec3 weighted=body.localAu*mass;
            for(const auto& e:bodies)if(e.parentKey==scope){result.push_back(e);mass+=e.massEarth;weighted+=e.localAu*e.massEarth;}
            if(result.size()>1 && mass>0){NavigationEntry centre=body;centre.kind=Kind::Barycentre;centre.key="bary:"+scope;centre.parentKey=scope;centre.name=body.name+" satellite barycentre";centre.type="Mass-weighted system centre";centre.localAu=weighted/mass;centre.diameterKm=0;centre.temperature=0;centre.satelliteCount=0;result.insert(result.begin(),centre);}
        }
        if(parent.system==-2 && parent.kind==Kind::Star){NavigationEntry group;group.system=-2;group.kind=Kind::Group;group.key="small-bodies";group.parentKey=Key(-2);group.name="Asteroid / Kuiper belt";group.parent="Sun";group.type="4096 representative small bodies";result.push_back(group);}
    }
    return result;
}
NavigationEntry NavigationCatalog::Object(int system,int body,double seconds){
    if(body<0 || (system==0 && body==0))return System(system,seconds);
    for(const auto& entry:Bodies(system,seconds))if(entry.body==body)return entry;
    return System(system,seconds);
}
NavigationEntry NavigationCatalog::Parent(const NavigationEntry& entry,double seconds){
    if(entry.parentKey=="universe" || entry.kind==Kind::Universe)return UniverseRoot();
    if(entry.parentKey.rfind("galaxy:",0)==0)return Galaxy(std::stoi(entry.parentKey.substr(7)));
    if(entry.parentKey=="small-bodies"){NavigationEntry e;e.kind=Kind::Group;e.system=-2;e.key="small-bodies";e.parentKey=Key(-2);e.name="Asteroid / Kuiper belt";return e;}
    for(const auto& e:systems)if(e.key==entry.parentKey)return System(e.system,seconds);
    for(const auto& e:Bodies(entry.system,seconds))if(e.key==entry.parentKey)return e;
    return System(entry.system);
}
