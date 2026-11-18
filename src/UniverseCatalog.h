#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>

namespace Universe {
inline constexpr std::uint64_t WorldSeed = 618;
inline constexpr int LegacyStarCount = 16000;
inline constexpr int StarCount = 2000000;
inline constexpr int ProceduralFirstId = 3000000; // Keep core/nebula namespaces intact.
// Collision-free virtual identities: galaxy / fixed spatial cell / candidate.
// Only camera-local cells are materialized; no billion-entry allocation.
inline constexpr int StreamFirstId=100000000, StreamGrid=64, StreamSlots=512;
inline constexpr int StreamCells=StreamGrid*StreamGrid*StreamGrid;
inline constexpr int MainStreamGalaxies=10, DistantGalaxies=256;
inline constexpr int StreamGalaxyCount=MainStreamGalaxies+DistantGalaxies;
inline constexpr int FieldGrid=32,FieldSlots=64,FieldCells=FieldGrid*FieldGrid*FieldGrid;
inline constexpr int MainStreamCount=MainStreamGalaxies*StreamCells*StreamSlots;
inline constexpr int StreamCount=MainStreamCount+DistantGalaxies*FieldCells*FieldSlots;
inline int CellGrid(int galaxy){return galaxy<MainStreamGalaxies?StreamGrid:FieldGrid;}
inline int CellSlots(int galaxy){return galaxy<MainStreamGalaxies?StreamSlots:FieldSlots;}
inline int FirstCell(int galaxy){return galaxy<MainStreamGalaxies?galaxy*StreamCells:MainStreamGalaxies*StreamCells+(galaxy-MainStreamGalaxies)*FieldCells;}
inline int CellGalaxy(int key){return key<MainStreamGalaxies*StreamCells?key/StreamCells:MainStreamGalaxies+(key-MainStreamGalaxies*StreamCells)/FieldCells;}
inline int CellStarIndex(int key,int slot){return StarCount+(key<MainStreamGalaxies*StreamCells?key*StreamSlots+slot:MainStreamCount+(key-MainStreamGalaxies*StreamCells)*FieldSlots+slot);}
inline int StarId(int index){return index>=StarCount?StreamFirstId+index-StarCount:index<LegacyStarCount?index+10:ProceduralFirstId+index-LegacyStarCount;}
inline int StarIndex(int id){return id>=StreamFirstId && id<StreamFirstId+StreamCount?StarCount+id-StreamFirstId:id>=10 && id<10+LegacyStarCount?id-10:id>=ProceduralFirstId && id<ProceduralFirstId+StarCount-LegacyStarCount?id-ProceduralFirstId+LegacyStarCount:-1;}
inline bool IsStar(int id){return StarIndex(id)>=0;}
struct ProceduralProperties {double massSolar,radiusSolar,temperature,luminositySolar,ageGyr;char spectral;};
ProceduralProperties Properties(int index);
int GalaxyForStar(int index);
std::string StarName(int index);
enum class Population { LocalDisk, ThinDisk, SpiralArm, Bulge, Halo };
Population StarPopulation(int index);
struct Galaxy {
  std::uint64_t key;
  glm::dvec3 centerAu, axesAu;
  glm::dmat3 rotation;
  float tilt;
  bool Contains(const glm::dvec3 &position) const;
};
Galaxy GalaxyAt(int index, std::uint64_t seed = WorldSeed);
glm::dvec3 StarPosition(int index, std::uint64_t seed = WorldSeed);
std::uint64_t SystemSeed(int catalogueId, std::uint64_t seed = WorldSeed);
glm::dvec3 StreamHalfSize(int galaxy);
double StellarDensity(int galaxy,const glm::dvec3& worldAu);
bool StreamExists(int index,std::uint64_t seed=WorldSeed);
struct StreamStar {int index;glm::dvec3 position;float temperature,luminosity,radiusAu;};

// A fixed deterministic catalogue, with a spatial lookup for camera-local
// detail. Distant objects have no time-dependent CPU state or orbital
// integration.
class Catalog {
public:
  void Generate(std::uint64_t seed = WorldSeed);
  const std::vector<glm::dvec3> &Positions() const { return positions; }
  std::vector<int> Nearby(const glm::dvec3 &camera, double radiusAu = 3) const;
  int Nearest(const glm::dvec3& camera) const;
  size_t LastNearestTreeVisits() const {return nearestTreeVisits;}
  std::vector<int> Closest(const glm::dvec3& camera,double radiusAu,size_t limit=1000) const;
  const std::vector<int>& DrawOrder() const {return drawOrder;}
  glm::dvec3 Position(int index) const {return index<StarCount?positions.at(index):StarPosition(index);}
  // Complete padded cells, never a camera-centred random shell. Coverage fades
  // before an unloaded cell can enter the visible range.
  bool UpdateStream(const glm::dvec3& camera,float magnitude) const;
  const std::vector<StreamStar>& StreamStars() const {return streamStars;}
  unsigned StreamRevision() const {return streamRevision;}
  size_t ResidentCells() const {return streamCells.size();}

private:
  std::vector<glm::dvec3> positions;
  std::vector<int> drawOrder;
  struct Bounds {glm::dvec3 low,high;};
  struct Node {int star,left=-1,right=-1,axis=0,bounds=-1;};
  std::vector<Node> tree;
  std::vector<Bounds> treeBounds;
  mutable std::unordered_map<int,std::vector<StreamStar>> streamCells;
  mutable std::vector<StreamStar> streamStars;
  mutable unsigned streamRevision=0;
  mutable float streamMagnitude=-100;
  mutable bool streamQueryValid=false,nearestQueryValid=false;
  mutable glm::dvec3 streamQueryCamera{},nearestQueryCamera{};
  mutable unsigned nearestQueryRevision=0;
  mutable int nearestQueryResult=-2;
  mutable size_t nearestTreeVisits=0;
  int BuildTree(std::vector<int>& indices,int begin,int end,int depth,Bounds& bounds);
  void SearchTree(int node,const glm::dvec3& camera,int& nearest,double& squared) const;
};
} // namespace Universe
