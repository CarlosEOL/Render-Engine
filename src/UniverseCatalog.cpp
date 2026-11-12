#include "UniverseCatalog.h"
#include "Astronomy.h"
#include "MilkyWayModel.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <queue>
#include <functional>

namespace {
std::uint64_t Mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
double Unit(std::mt19937_64 &rng) {
  return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}
double FastUnit(std::uint64_t& state){state=Mix(state);return static_cast<double>(state>>11)*0x1.0p-53;}
} // namespace

bool Universe::Galaxy::Contains(const glm::dvec3 &position) const {
  const auto q = (glm::transpose(rotation) * (position - centerAu)) / axesAu;
  return glm::dot(q, q) <= 1.0 + 1e-12;
}
Universe::Galaxy Universe::GalaxyAt(int index, std::uint64_t seed) {
  if (index < 0 || index > 3)
    throw std::invalid_argument("Unknown galaxy sector");
  const auto key = Mix(seed ^ Mix(static_cast<std::uint64_t>(index)));
  const double ly = Astronomy::auPerLightYear;
  if (index == 0)
    return {key,
            MilkyWayModel::CenterAu(),
            glm::dvec3(50*MilkyWayModel::auPerKpc),
            MilkyWayModel::Orientation(),
            0};
  std::mt19937_64 rng(key);
  const glm::dvec3 sectors[] = {{0, 0, -1}, {1, 0, -1}, {-1, 1, -2}};
  const auto center =
      (sectors[index - 1] +
       glm::dvec3(Unit(rng) - .5, (Unit(rng) - .5) * .3, Unit(rng) - .5) *
           .25) *
      (2.5e6 * ly);
  const double radius = (42000 + Unit(rng) * 18000) * ly;
  const float tilt = static_cast<float>((Unit(rng) - .5) * 1.8);
  const auto rotation = glm::dmat3(glm::rotate(
      glm::dmat4(1), static_cast<double>(tilt), glm::dvec3(1, 0, 0)));
  return {key,
          center,
          {radius, radius * (index == 3 ? .72 : .70),
           radius * (index == 3 ? .80 : 1)},
          rotation,
          tilt};
}
std::uint64_t Universe::SystemSeed(int id, std::uint64_t seed) {
  return Mix(seed ^ Mix(static_cast<std::uint64_t>(id)));
}
int Universe::GalaxyForStar(int index){
  if(index<0 || index>=StarCount)throw std::invalid_argument("Unknown star catalogue entry");
  if(index>=LegacyStarCount)return index<LegacyStarCount+262144?0:index%8<5?0:1+(index%8-5);
  return index<1600 || index>=7000?0:1+(index-1600)/1800;
}
std::string Universe::StarName(int index){
  if(index>=LegacyStarCount)return "Procedural star "+std::to_string(index-LegacyStarCount+1);
  if(index<1024)return "Local star "+std::to_string(index+1);
  if(GalaxyForStar(index)==0)return "Milky Way star "+std::to_string(index+1);
  return "Galaxy "+std::to_string(GalaxyForStar(index))+" star "+std::to_string((index-1600)%1800+1);
}
Universe::Population Universe::StarPopulation(int index) {
  if(index<0 || index>=StarCount)throw std::invalid_argument("Unknown star catalogue entry");
  if(index<1024)return Population::LocalDisk;
  const auto fraction=Mix(static_cast<std::uint64_t>(index)^0xbadc0ffeeULL)%100;
  const int galaxy=GalaxyForStar(index);
  if(galaxy==3)return fraction<80?Population::Bulge:Population::Halo;
  return fraction<12?Population::Bulge:fraction<17?Population::Halo:
      fraction<62?Population::SpiralArm:Population::ThinDisk;
}

glm::dvec3 Universe::StarPosition(int index, std::uint64_t seed) {
  if (index < 0 || index >= StarCount)
    throw std::invalid_argument("Unknown star catalogue entry");
  const int galaxy = GalaxyForStar(index);
  if(index>=LegacyStarCount){
    static const std::array<Galaxy,4> defaultGalaxies={GalaxyAt(0),GalaxyAt(1),GalaxyAt(2),GalaxyAt(3)};
    const auto group=seed==WorldSeed?defaultGalaxies[galaxy]:GalaxyAt(galaxy,seed);
    auto state=SystemSeed(StarId(index),seed)^group.key;
    const auto u=[&](){return FastUnit(state);};
    const double angle=u()*glm::two_pi<double>();
    if(index<LegacyStarCount+262144){
      // Fixed solar-neighbourhood population; never attached to the camera.
      const double radius=std::cbrt(u())*2500*Astronomy::auPerLightYear,y=u()*2-1;
      return {radius*std::sqrt(1-y*y)*std::cos(angle),radius*y,radius*std::sqrt(1-y*y)*std::sin(angle)};
    }
    const auto population=StarPopulation(index);
    if(galaxy==0){
      double radius;do{radius=-MilkyWayModel::diskScaleKpc*std::log(std::max(1e-12,u()*u()));}while(radius>16);
      double azimuth=angle,height=(u()*2-1)*(population==Population::SpiralArm?.12:.6);
      if(population==Population::SpiralArm)azimuth=(index%4)*glm::half_pi<double>()+std::log(std::max(.1,radius))/.22+(u()-.5)*.25;
      if(population==Population::Bulge){radius=std::pow(u(),.65)*4;height=(u()*2-1)*1.1;}
      if(population==Population::Halo){radius=std::cbrt(u())*43;const double y=u()*2-1;return MilkyWayModel::ToWorldAu({radius*std::sqrt(1-y*y)*std::cos(angle),radius*y,radius*std::sqrt(1-y*y)*std::sin(angle)});}
      glm::dvec3 p(radius*std::cos(azimuth),height,radius*std::sin(azimuth));p.y+=MilkyWayModel::WarpKpc(p);
      return MilkyWayModel::ToWorldAu(p);
    }
    double radius;do{radius=-.16*std::log(std::max(1e-12,u()*u()));}while(radius>.9);
    const double height=(u()*2-1)*(galaxy==3?.28:.025);
    const double azimuth=galaxy==3?angle:(index%(galaxy==2?2:4))*glm::two_pi<double>()/(galaxy==2?2:4)+std::log(std::max(.02,radius))/.3;
    const glm::dvec3 q(radius*std::cos(azimuth),height,radius*std::sin(azimuth));
    return group.centerAu+group.rotation*(q*group.axesAu);
  }
  const auto group = GalaxyAt(galaxy, seed);
  std::mt19937_64 rng(SystemSeed(index + 10, seed) ^ group.key);
  const double ly = Astronomy::auPerLightYear;
  if (index < 1024) {
    // A local survey volume inside the Milky Way, not a shell around the
    // camera.
    const double r = std::cbrt(Unit(rng)) * 75 * ly,
                 a = Unit(rng) * glm::two_pi<double>();
    const double vertical=Unit(rng)*2-1,horizontal=std::sqrt(1-vertical*vertical);
    const auto p = glm::dvec3(r*horizontal*std::cos(a),r*vertical,r*horizontal*std::sin(a));
    if (!group.Contains(p))
      throw std::runtime_error("Local star outside its galaxy");
    return p;
  }
  glm::dvec3 q;
  const auto population=StarPopulation(index);
  if(galaxy==0){
    // Density-weighted Galactic populations in kpc. Local survey stars above
    // retain their own solar-neighbourhood sample; the rest trace structure.
    glm::dvec3 p;
    do{
      const double angle=Unit(rng)*glm::two_pi<double>();
      if(population==Population::Bulge){
        p={std::pow(Unit(rng),.65)*5*(Unit(rng)<.5?-1:1),
            (Unit(rng)*2-1)*1.2,(Unit(rng)*2-1)*1.6};
        const double bar=glm::radians(MilkyWayModel::barAngleDegrees);
        p={p.x*std::cos(bar)-p.z*std::sin(bar),p.y,p.x*std::sin(bar)+p.z*std::cos(bar)};
      }else if(population==Population::Halo){
        const double radius=2+std::pow(Unit(rng),.55)*43,y=Unit(rng)*2-1;
        p={radius*std::sqrt(1-y*y)*std::cos(angle),radius*y,radius*std::sqrt(1-y*y)*std::sin(angle)};
      }else{
        const double radius=-MilkyWayModel::diskScaleKpc*std::log(std::max(1e-15,Unit(rng)*Unit(rng)));
        const double height=-std::log(std::max(1e-15,1-Unit(rng)))*(population==Population::SpiralArm?.085:.30)*(Unit(rng)<.5?-1:1);
        p={radius*std::cos(angle),height,radius*std::sin(angle)};
        if(radius>16)continue;
        if(population==Population::SpiralArm && Unit(rng)>MilkyWayModel::ArmStrength(p))continue;
        p.y+=MilkyWayModel::WarpKpc(p);
      }
      const auto world=MilkyWayModel::ToWorldAu(p);
      if(group.Contains(world))return world;
    }while(true);
  }
  do {
    if(population==Population::Bulge || population==Population::Halo){
      // Centrally concentrated, bounded spherical/spheroidal populations.
      const double radius=population==Population::Bulge?std::pow(Unit(rng),.65)*.30:std::cbrt(Unit(rng))*.93;
      const double y=Unit(rng)*2-1,azimuth=Unit(rng)*glm::two_pi<double>();
      q={radius*std::sqrt(1-y*y)*std::cos(azimuth),radius*y,radius*std::sqrt(1-y*y)*std::sin(azimuth)};
      if(population==Population::Bulge)q.y*=.65;
    }else{
      // Product of two uniform samples gives a gamma radial distribution:
      // surface density falls exponentially instead of filling a flat disk.
      const double r=-.19*std::log(std::max(1e-15,Unit(rng)*Unit(rng)));
      double azimuth=Unit(rng)*glm::two_pi<double>();
      if(population==Population::SpiralArm){
        const int arms=galaxy==2?2:4;
        azimuth=(index%arms)*glm::two_pi<double>()/arms+std::log(std::max(.025,r))/.30+(Unit(rng)-.5)*.28;
      }
      const double height=-std::log(std::max(1e-15,1-Unit(rng)))*(galaxy==0?.006:.009)*(Unit(rng)<.5?-1:1);
      q={r*std::cos(azimuth),height,r*std::sin(azimuth)};
      if(galaxy==2 && population==Population::ThinDisk && r<.3)q.z*=.22;
    }
  } while (glm::dot(q, q) > .95 * .95);
  return group.centerAu + group.rotation * (q * group.axesAu);
}

void Universe::Catalog::Generate(std::uint64_t seed) {
  positions.clear();
  positions.reserve(StarCount);
  for (int index = 0; index < StarCount; ++index) {
    const auto p = StarPosition(index, seed);
    positions.push_back(p);
  }
  std::vector<int> indices(StarCount);std::iota(indices.begin(),indices.end(),0);
  tree.clear();tree.reserve(StarCount);BuildTree(indices,0,StarCount,0);
  drawOrder=std::move(indices);
}
int Universe::Catalog::BuildTree(std::vector<int>& indices,int begin,int end,int depth){
  if(begin>=end)return -1;
  const int mid=(begin+end)/2,axis=depth%3;
  std::nth_element(indices.begin()+begin,indices.begin()+mid,indices.begin()+end,[&](int a,int b){return positions[a][axis]<positions[b][axis];});
  const int node=static_cast<int>(tree.size());tree.push_back({indices[mid],-1,-1,axis});
  const int left=BuildTree(indices,begin,mid,depth+1),right=BuildTree(indices,mid+1,end,depth+1);
  tree[node].left=left;tree[node].right=right;return node;
}
void Universe::Catalog::SearchTree(int node,const glm::dvec3& camera,int& nearest,double& squared) const{
  if(node<0)return;
  const auto& entry=tree[node];const auto delta=camera-positions[entry.star];
  const double distance=glm::dot(delta,delta);
  if(distance<squared){squared=distance;nearest=StarId(entry.star);}
  const double axis=delta[entry.axis];
  SearchTree(axis<0?entry.left:entry.right,camera,nearest,squared);
  if(axis*axis<squared)SearchTree(axis<0?entry.right:entry.left,camera,nearest,squared);
}
int Universe::Catalog::Nearest(const glm::dvec3& camera) const{
  // The Sun is a separate authored entry at the world origin.
  int nearest=-2;double squared=glm::dot(camera,camera);
  if(!tree.empty())SearchTree(0,camera,nearest,squared);return nearest;
}
std::vector<int> Universe::Catalog::Nearby(const glm::dvec3 &camera,
                                           double radius) const {
  return radius>0 && radius<=8?Closest(camera,radius,64):std::vector<int>{};
}
std::vector<int> Universe::Catalog::Closest(const glm::dvec3& camera,double radius,size_t limit) const{
  if(radius<=0 || limit==0)return {};
  std::priority_queue<std::pair<double,int>> heap;const double radius2=radius*radius;
  const auto search=[&](const auto& self,int node)->void{
    if(node<0)return;const auto& entry=tree[node];const auto delta=camera-positions[entry.star];
    const double distance=glm::dot(delta,delta),axis=delta[entry.axis];
    if(distance<=radius2){heap.emplace(distance,entry.star);if(heap.size()>limit)heap.pop();}
    self(self,axis<0?entry.left:entry.right);
    const double bound=heap.size()==limit?std::min(radius2,heap.top().first):radius2;
    if(axis*axis<=bound)self(self,axis<0?entry.right:entry.left);
  };
  if(!tree.empty())search(search,0);
  std::vector<int> result(heap.size());for(size_t i=result.size();i>0;--i){result[i-1]=StarId(heap.top().second);heap.pop();}return result;
}

Universe::ProceduralProperties Universe::Properties(int index){
  auto state=SystemSeed(StarId(index));const auto u=[&](){return FastUnit(state);};
  const double choice=u();char spectral;double mass,temperature;
  if(choice<.55){spectral='M';mass=.1+.45*u();temperature=2700+1200*u();}
  else if(choice<.78){spectral='K';mass=.55+.3*u();temperature=3900+1300*u();}
  else if(choice<.90){spectral='G';mass=.85+.25*u();temperature=5200+800*u();}
  else if(choice<.96){spectral='F';mass=1.1+.4*u();temperature=6000+1500*u();}
  else if(choice<.985){spectral='A';mass=1.5+u();temperature=7500+2500*u();}
  else if(choice<.998){spectral='B';mass=2.5+9.5*u();temperature=10000+20000*u();}
  else {spectral='O';mass=15+20*u();temperature=30000+15000*u();}
  const double radius=std::pow(mass,.8),ratio=temperature/5772,luminosity=radius*radius*ratio*ratio*ratio*ratio;
  const double lifetime=std::min(13.7,10*mass/luminosity);
  return {mass,radius,temperature,luminosity,(.02+.88*u())*lifetime,spectral};
}
