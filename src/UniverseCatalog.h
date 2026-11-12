#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace Universe {
inline constexpr std::uint64_t WorldSeed = 618;
inline constexpr int StarCount = 7000;
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

private:
  using Cell = std::array<std::int64_t, 3>;
  struct Hash {
    size_t operator()(const Cell &cell) const;
  };
  static Cell CellAt(const glm::dvec3 &position);
  std::vector<glm::dvec3> positions;
  std::unordered_map<Cell, std::vector<int>, Hash> cells;
};
} // namespace Universe
