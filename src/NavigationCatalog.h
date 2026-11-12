#pragma once
#include "UniverseCatalog.h"
#include "StarSystem.h"
#include <unordered_map>
#include <string>
#include <vector>
struct NavigationEntry {
    enum class Kind {Universe,Galaxy,Star,Planet,Core,Craft,Asteroid,Barycentre,Group,Nebula};
    Kind kind=Kind::Star;
    int system=-2,body=-1,planetCount=-1,satelliteCount=0;
    std::string key,parentKey,name,parent,type;
    // Subtract anchors before adding local offsets: never flatten a landed
    // camera into a cosmological floating-point world coordinate.
    glm::dvec3 anchorAu=glm::dvec3(0),localAu=glm::dvec3(0);
    glm::vec3 color=glm::vec3(.7f);
    double diameterKm=0,temperature=0,luminosity=0,massEarth=0,distanceAu=0;
    double Distance(const glm::dvec3& anchor,const glm::dvec3& local) const {
        return glm::length((anchorAu-anchor)+(localAu-local));
    }
};
class NavigationCatalog {
public:
    void Initialize(const Universe::Catalog& catalog);
    std::vector<NavigationEntry> Find(const std::string& query,double seconds);
    std::vector<NavigationEntry> Nearby(const glm::dvec3& anchor,const glm::dvec3& local,double radiusLy,double seconds);
    std::vector<NavigationEntry> Children(const NavigationEntry& parent,double seconds);
    NavigationEntry System(int id,double seconds=0) const;
    NavigationEntry Parent(const NavigationEntry& entry,double seconds=0);
    NavigationEntry Object(int system,int body,double seconds);
    NavigationEntry UniverseRoot() const;
private:
    const Universe::Catalog* catalog=nullptr;
    std::vector<NavigationEntry> systems;
    std::vector<NavigationEntry> Bodies(int system,double seconds);
};
