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
inline int StarId(int index){return index<LegacyStarCount?index+10:ProceduralFirstId+index-LegacyStarCount;}
inline int StarIndex(int id){return id>=10 && id<10+LegacyStarCount?id-10:id>=ProceduralFirstId && id<ProceduralFirstId+StarCount-LegacyStarCount?id-ProceduralFirstId+LegacyStarCount:-1;}
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

// A fixed deterministic catalogue, with a spatial lookup for camera-local
// detail. Distant objects have no time-dependent CPU state or orbital
// integration.
class Catalog {
public:
  void Generate(std::uint64_t seed = WorldSeed);
  const std::vector<glm::dvec3> &Positions() const { return positions; }
  std::vector<int> Nearby(const glm::dvec3 &camera, double radiusAu = 3) const;
  int Nearest(const glm::dvec3& camera) const;
  std::vector<int> Closest(const glm::dvec3& camera,double radiusAu,size_t limit=1000) const;
  const std::vector<int>& DrawOrder() const {return drawOrder;}

private:
  std::vector<glm::dvec3> positions;
  std::vector<int> drawOrder;
  struct Node {int star,left=-1,right=-1,axis=0;};
  std::vector<Node> tree;
  int BuildTree(std::vector<int>& indices,int begin,int end,int depth);
  void SearchTree(int node,const glm::dvec3& camera,int& nearest,double& squared) const;
};
} // namespace Universe
