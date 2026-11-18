#include "UniverseCatalog.h"
#include "Astronomy.h"
#include "MilkyWayModel.h"
#include "NearbyGalaxies.h"
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
auto MakeDistantGalaxies(std::uint64_t seed){
  std::array<Universe::Galaxy,Universe::DistantGalaxies> result;
  std::mt19937_64 rng(seed^0x67616c617879ULL);
  for(int i=0;i<Universe::DistantGalaxies;++i){
    const double z=Unit(rng)*2-1,angle=Unit(rng)*glm::two_pi<double>();
    const double distance=1.8e6*std::pow(80.0/1.8,Unit(rng))*Astronomy::auPerLightYear;
    const glm::dvec3 direction(std::sqrt(1-z*z)*std::cos(angle),z,std::sqrt(1-z*z)*std::sin(angle));
    const double radius=(18000+Unit(rng)*62000)*Astronomy::auPerLightYear;
    auto rotation=glm::rotate(glm::dmat4(1),Unit(rng)*glm::two_pi<double>(),glm::dvec3(0,1,0));
    rotation=glm::rotate(rotation,std::acos(Unit(rng)*2-1),glm::dvec3(1,0,0));
    result[i]={Mix(seed^Mix(i+10)),MilkyWayModel::CenterAu()+direction*distance,{radius,radius*(i%3==2?.65:.15),radius},glm::dmat3(rotation),0};
  }
  return result;
}
} // namespace

bool Universe::Galaxy::Contains(const glm::dvec3 &position) const {
  const auto q = (glm::transpose(rotation) * (position - centerAu)) / axesAu;
  return glm::dot(q, q) <= 1.0 + 1e-12;
}
Universe::Galaxy Universe::GalaxyAt(int index, std::uint64_t seed) {
  if (index < 0 || index >= StreamGalaxyCount)
    throw std::invalid_argument("Unknown galaxy sector");
  const auto key = Mix(seed ^ Mix(static_cast<std::uint64_t>(index)));
  const double ly = Astronomy::auPerLightYear;
  if(index>=MainStreamGalaxies){static const auto field=MakeDistantGalaxies(WorldSeed);return seed==WorldSeed?field[index-MainStreamGalaxies]:MakeDistantGalaxies(seed)[index-MainStreamGalaxies];}
  if(index>=4){
    const auto& source=NearbyGalaxies::Catalog.at(index-4);
    const double r=source.Radius();
    return {key,source.Center(),{r,r*(source.morphology==5?.65:.15),r},source.Frame(),0};
  }
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
namespace {
const Universe::Galaxy& CachedGalaxy(int index){
  static const auto groups=[](){std::array<Universe::Galaxy,Universe::StreamGalaxyCount> result;for(int i=0;i<Universe::StreamGalaxyCount;++i)result[i]=Universe::GalaxyAt(i);return result;}();
  return groups.at(index);
}
}
int Universe::GalaxyForStar(int index){
  if(index<0 || index>=StarCount+StreamCount)throw std::invalid_argument("Unknown star catalogue entry");
  if(index>=StarCount){const int offset=index-StarCount;return offset<MainStreamCount?offset/(StreamCells*StreamSlots):MainStreamGalaxies+(offset-MainStreamCount)/(FieldCells*FieldSlots);}
  if(index>=LegacyStarCount)return index%8<5?0:1+(index%8-5);
  return index<1600 || index>=7000?0:1+(index-1600)/1800;
}
std::string Universe::StarName(int index){
  if(index>=StarCount)return "Seed star "+std::to_string(StarId(index));
  if(index>=LegacyStarCount)return "Procedural star "+std::to_string(index-LegacyStarCount+1);
  if(index<1024)return "Local star "+std::to_string(index+1);
  if(GalaxyForStar(index)==0)return "Milky Way star "+std::to_string(index+1);
  return "Galaxy "+std::to_string(GalaxyForStar(index))+" star "+std::to_string((index-1600)%1800+1);
}
Universe::Population Universe::StarPopulation(int index) {
  if(index<0 || index>=StarCount+StreamCount)throw std::invalid_argument("Unknown star catalogue entry");
  if(index<1024)return Population::LocalDisk;
  const auto fraction=Mix(static_cast<std::uint64_t>(index)^0xbadc0ffeeULL)%100;
  const int galaxy=GalaxyForStar(index);
  if(galaxy==3)return fraction<80?Population::Bulge:Population::Halo;
  return fraction<12?Population::Bulge:fraction<17?Population::Halo:
      fraction<62?Population::SpiralArm:Population::ThinDisk;
}

glm::dvec3 Universe::StarPosition(int index, std::uint64_t seed) {
  if (index < 0 || index >= StarCount+StreamCount)
    throw std::invalid_argument("Unknown star catalogue entry");
  const int galaxy = GalaxyForStar(index);
  if(index>=StarCount){
    const auto group=seed==WorldSeed?CachedGalaxy(galaxy):GalaxyAt(galaxy,seed);
    const int grid=CellGrid(galaxy),slots=CellSlots(galaxy),offset=index-StarCount;
    const int cell=(galaxy<MainStreamGalaxies?offset/slots:(offset-MainStreamCount)/slots)%(grid*grid*grid);
    const glm::ivec3 coordinate(cell%grid,(cell/grid)%grid,cell/(grid*grid));
    auto state=SystemSeed(StarId(index),seed);
    const double x=FastUnit(state),y=FastUnit(state),z=FastUnit(state);
    const glm::dvec3 jitter(x,y,z);
    const auto local=((glm::dvec3(coordinate)+jitter)*(2.0/grid)-1.0)*StreamHalfSize(galaxy);
    return group.centerAu+group.rotation*local;
  }
  if(index>=LegacyStarCount){
    static const std::array<Galaxy,4> defaultGalaxies={GalaxyAt(0),GalaxyAt(1),GalaxyAt(2),GalaxyAt(3)};
    const auto group=seed==WorldSeed?defaultGalaxies[galaxy]:GalaxyAt(galaxy,seed);
    auto state=SystemSeed(StarId(index),seed)^group.key;
    const auto u=[&](){return FastUnit(state);};
    const double angle=u()*glm::two_pi<double>();
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
  streamCells.clear();streamStars.clear();streamMagnitude=-100;++streamRevision;
  streamQueryValid=nearestQueryValid=false;
  positions.clear();
  positions.reserve(StarCount);
  for (int index = 0; index < StarCount; ++index) {
    const auto p = StarPosition(index, seed);
    positions.push_back(p);
  }
  std::vector<int> indices(StarCount);std::iota(indices.begin(),indices.end(),0);
  tree.clear();tree.reserve(StarCount);treeBounds.clear();treeBounds.reserve(StarCount/32);
  Bounds bounds;BuildTree(indices,0,StarCount,0,bounds);
  drawOrder=std::move(indices);
}
int Universe::Catalog::BuildTree(std::vector<int>& indices,int begin,int end,int depth,Bounds& bounds){
  if(begin>=end)return -1;
  const int mid=(begin+end)/2,axis=depth%3;
  std::nth_element(indices.begin()+begin,indices.begin()+mid,indices.begin()+end,[&](int a,int b){return positions[a][axis]<positions[b][axis];});
  const int node=static_cast<int>(tree.size());tree.push_back({indices[mid],-1,-1,axis});
  bounds.low=bounds.high=positions[indices[mid]];
  Bounds child;
  const int left=BuildTree(indices,begin,mid,depth+1,child);
  if(left>=0){bounds.low=glm::min(bounds.low,child.low);bounds.high=glm::max(bounds.high,child.high);}
  const int right=BuildTree(indices,mid+1,end,depth+1,child);
  if(right>=0){bounds.low=glm::min(bounds.low,child.low);bounds.high=glm::max(bounds.high,child.high);}
  tree[node].left=left;tree[node].right=right;
  // Bound spatial regions, not every leaf: retain compact per-star nodes.
  if(end-begin>=64){tree[node].bounds=static_cast<int>(treeBounds.size());treeBounds.push_back(bounds);}
  return node;
}
void Universe::Catalog::SearchTree(int node,const glm::dvec3& camera,int& nearest,double& squared) const{
  if(node<0)return;
  ++nearestTreeVisits;
  const auto& entry=tree[node];
  if(entry.bounds>=0){const auto& box=treeBounds[entry.bounds];const auto d=camera-glm::clamp(camera,box.low,box.high);if(glm::dot(d,d)>squared)return;}
  const auto delta=camera-positions[entry.star];
  const double distance=glm::dot(delta,delta);
  if(distance<squared){squared=distance;nearest=StarId(entry.star);}
  const double axis=delta[entry.axis];
  SearchTree(axis<0?entry.left:entry.right,camera,nearest,squared);
  if(axis*axis<squared)SearchTree(axis<0?entry.right:entry.left,camera,nearest,squared);
}
int Universe::Catalog::Nearest(const glm::dvec3& camera) const{
  nearestTreeVisits=0;
  if(nearestQueryValid && nearestQueryRevision==streamRevision && nearestQueryCamera==camera)return nearestQueryResult;
  // The Sun is a separate authored entry at the world origin.
  int nearest=-2;double squared=glm::dot(camera,camera);
  // A local resident supplies a tight initial radius before searching the
  // far catalogue. Starting at the Sun made remote-galaxy queries enormous.
  for(const auto& star:streamStars){const auto d=star.position-camera;const double r=glm::dot(d,d);if(r<squared){squared=r;nearest=StarId(star.index);}}
  if(!tree.empty())SearchTree(0,camera,nearest,squared);
  nearestQueryValid=true;nearestQueryCamera=camera;nearestQueryRevision=streamRevision;nearestQueryResult=nearest;
  return nearest;
}
std::vector<int> Universe::Catalog::Nearby(const glm::dvec3 &camera,
                                           double radius) const {
  return radius>0 && radius<=8?Closest(camera,radius,64):std::vector<int>{};
}
std::vector<int> Universe::Catalog::Closest(const glm::dvec3& camera,double radius,size_t limit) const{
  if(radius<=0 || limit==0)return {};
  std::priority_queue<std::pair<double,int>> heap;const double radius2=radius*radius;
  const auto search=[&](const auto& self,int node)->void{
    if(node<0)return;const auto& entry=tree[node];
    const double currentBound=heap.size()==limit?std::min(radius2,heap.top().first):radius2;
    if(entry.bounds>=0){const auto& box=treeBounds[entry.bounds];const auto d=camera-glm::clamp(camera,box.low,box.high);if(glm::dot(d,d)>currentBound)return;}
    const auto delta=camera-positions[entry.star];
    const double distance=glm::dot(delta,delta),axis=delta[entry.axis];
    if(distance<=radius2){heap.emplace(distance,entry.star);if(heap.size()>limit)heap.pop();}
    self(self,axis<0?entry.left:entry.right);
    const double bound=heap.size()==limit?std::min(radius2,heap.top().first):radius2;
    if(axis*axis<=bound)self(self,axis<0?entry.right:entry.left);
  };
  for(const auto& star:streamStars){const auto d=star.position-camera;const double r=glm::dot(d,d);if(r<=radius2){heap.emplace(r,star.index);if(heap.size()>limit)heap.pop();}}
  if(!tree.empty())search(search,0);
  std::vector<int> result(heap.size());for(size_t i=result.size();i>0;--i){result[i-1]=StarId(heap.top().second);heap.pop();}return result;
}

glm::dvec3 Universe::StreamHalfSize(int galaxy){
  if(galaxy==0)return glm::dvec3(24,6,24)*MilkyWayModel::auPerKpc;
  const auto& group=CachedGalaxy(galaxy);
  return {group.axesAu.x,group.axesAu.y,group.axesAu.z};
}
double Universe::StellarDensity(int galaxy,const glm::dvec3& world){
  const auto& group=CachedGalaxy(galaxy);
  const auto local=glm::transpose(group.rotation)*(world-group.centerAu);
  const auto edge=glm::abs(local/StreamHalfSize(galaxy));
  const double boundary=1-glm::smoothstep(.85,1.0,std::max({edge.x,edge.y,edge.z}));
  if(boundary<=0)return 0;
  if(galaxy==0){const auto d=MilkyWayModel::Densities(MilkyWayModel::ToModelKpc(world));return boundary*std::clamp((d.x+d.y+d.z)*12.0,0.0,1.0);}
  const auto p=local/group.axesAu;const double r=std::hypot(p.x,p.z);
  double density;
  const int morphology=galaxy>=MainStreamGalaxies?(galaxy-MainStreamGalaxies)%3==2?5:0:galaxy>=4?NearbyGalaxies::Catalog[galaxy-4].morphology:galaxy==3?5:0;
  if(morphology==5)density=std::exp(-5*glm::length(p));
  else if(morphology==4)density=std::exp(-12*glm::length(p))+.8*std::exp(-std::pow((r-.73)/.14,2)-std::abs(p.y)*10);
  else {
    const double arms=.55+.45*std::pow(.5+.5*std::cos(4*std::atan2(p.z,p.x)-12*std::log(std::max(.02,r))),3);
    density=std::exp(-3*r-std::abs(p.y)*6)*arms+.15*std::exp(-10*glm::length(p));
  }
  return boundary*std::clamp(density,0.0,1.0);
}
bool Universe::StreamExists(int index,std::uint64_t seed){
  if(index<StarCount || index>=StarCount+StreamCount)return false;
  auto state=SystemSeed(StarId(index),seed)^0x4a8ec572ULL;
  return FastUnit(state)<StellarDensity(GalaxyForStar(index),StarPosition(index,seed));
}
bool Universe::Catalog::UpdateStream(const glm::dvec3& camera,float magnitude) const{
  if(streamQueryValid && streamMagnitude==magnitude && streamQueryCamera==camera)return false;
  streamQueryCamera=camera;streamQueryValid=true;
  std::vector<int> wanted;
  std::array<float,StreamGalaxyCount> radii{};
  for(int galaxy=0;galaxy<StreamGalaxyCount;++galaxy){
    const int grid=CellGrid(galaxy);
    const auto& group=CachedGalaxy(galaxy);const auto half=StreamHalfSize(galaxy),width=half*(2.0/grid);
    const auto local=glm::transpose(group.rotation)*(camera-group.centerAu);
    const double radius=std::min(12000*Astronomy::auPerLightYear*std::pow(10.0,.08*(magnitude-13)),3*std::max({width.x,width.y,width.z}));
    radii[galaxy]=static_cast<float>(radius);
    const auto outside=glm::max(glm::abs(local)-half,glm::dvec3(0));
    if(glm::dot(outside,outside)>radius*radius)continue;
    const auto a=glm::clamp(glm::floor((local-glm::dvec3(radius)+half)/width)-1.0,glm::dvec3(0),glm::dvec3(grid-1));
    const auto b=glm::clamp(glm::floor((local+glm::dvec3(radius)+half)/width)+1.0,glm::dvec3(0),glm::dvec3(grid-1));
    for(int z=int(a.z);z<=int(b.z);++z)for(int y=int(a.y);y<=int(b.y);++y)for(int x=int(a.x);x<=int(b.x);++x){
      const auto low=glm::dvec3(x,y,z)*width-half;
      const auto delta=local-glm::clamp(local,low,low+width);
      if(glm::dot(delta,delta)>(radius+glm::length(width)*.15)*(radius+glm::length(width)*.15))continue;
      wanted.push_back(FirstCell(galaxy)+x+grid*(y+grid*z));
    }
  }
  std::sort(wanted.begin(),wanted.end());
  bool changed=magnitude!=streamMagnitude || wanted.size()!=streamCells.size();
  for(const int key:wanted)if(!streamCells.count(key)){
    changed=true;auto& cell=streamCells[key];
    for(int slot=0;slot<CellSlots(CellGalaxy(key));++slot){
      const int index=CellStarIndex(key,slot);
      if(!StreamExists(index))continue;
      const auto properties=Properties(index);
      cell.push_back({index,StarPosition(index),static_cast<float>(properties.temperature),static_cast<float>(properties.luminositySolar),radii[CellGalaxy(key)]});
    }
  }
  if(!changed)return false;
  for(auto it=streamCells.begin();it!=streamCells.end();)if(!std::binary_search(wanted.begin(),wanted.end(),it->first))it=streamCells.erase(it);else ++it;
  streamStars.clear();
  for(const int key:wanted)for(auto star:streamCells.at(key)){star.radiusAu=radii[CellGalaxy(key)];streamStars.push_back(star);}
  streamMagnitude=magnitude;++streamRevision;return true;
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
