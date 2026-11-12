#include "UniverseCatalog.h"
#include "Astronomy.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <stdexcept>

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
            {-26000 * ly, 0, 0},
            {50000 * ly, 1500 * ly, 50000 * ly},
            glm::dmat3(1),
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
          {radius, radius * (index == 3 ? .72 : .10),
           radius * (index == 3 ? .80 : 1)},
          rotation,
          tilt};
}
std::uint64_t Universe::SystemSeed(int id, std::uint64_t seed) {
  return Mix(seed ^ Mix(static_cast<std::uint64_t>(id)));
}

glm::dvec3 Universe::StarPosition(int index, std::uint64_t seed) {
  if (index < 0 || index >= StarCount)
    throw std::invalid_argument("Unknown star catalogue entry");
  const int galaxy = index < 1600 ? 0 : 1 + (index - 1600) / 1800;
  const auto group = GalaxyAt(galaxy, seed);
  std::mt19937_64 rng(SystemSeed(index + 10, seed) ^ group.key);
  const double ly = Astronomy::auPerLightYear;
  if (galaxy == 0) {
    // A local survey volume inside the Milky Way, not a shell around the
    // camera.
    const double r = std::cbrt(Unit(rng)) * 75 * ly,
                 a = Unit(rng) * glm::two_pi<double>();
    const double z = (Unit(rng) * 2 - 1) * std::min(18 * ly, r * .35);
    const auto p = glm::dvec3(r * std::cos(a), z, r * std::sin(a));
    if (!group.Contains(p))
      throw std::runtime_error("Local star outside its galaxy");
    return p;
  }
  glm::dvec3 q;
  do {
    const double r = std::sqrt(Unit(rng)) * .90;
    const double a = (index % 3) * glm::two_pi<double>() / 3 + r * 6 +
                     (Unit(rng) - .5) * .65;
    q = {r * std::cos(a), (Unit(rng) * 2 - 1) * .75, r * std::sin(a)};
    if (galaxy == 2 && index % 5 == 0)
      q.z *= .18;
    if (galaxy == 3)
      q = {Unit(rng) * 2 - 1, Unit(rng) * 2 - 1, Unit(rng) * 2 - 1};
  } while (glm::dot(q, q) > .95 * .95);
  return group.centerAu + group.rotation * (q * group.axesAu);
}

size_t Universe::Catalog::Hash::operator()(const Cell &cell) const {
  return static_cast<size_t>(Mix(cell[0]) ^ Mix(cell[1] + 0x1234) ^
                             Mix(cell[2] + 0x5678));
}
Universe::Catalog::Cell Universe::Catalog::CellAt(const glm::dvec3 &p) {
  return {static_cast<std::int64_t>(std::floor(p.x / 8)),
          static_cast<std::int64_t>(std::floor(p.y / 8)),
          static_cast<std::int64_t>(std::floor(p.z / 8))};
}
void Universe::Catalog::Generate(std::uint64_t seed) {
  positions.clear();
  positions.reserve(StarCount);
  cells.clear();
  for (int index = 0; index < StarCount; ++index) {
    const auto p = StarPosition(index, seed);
    positions.push_back(p);
    cells[CellAt(p)].push_back(index + 10);
  }
}
std::vector<int> Universe::Catalog::Nearby(const glm::dvec3 &camera,
                                           double radius) const {
  std::vector<int> result;
  if (radius <= 0 || radius > 8)
    return result;
  const auto cell = CellAt(camera);
  for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x) {
        const auto found = cells.find({cell[0] + x, cell[1] + y, cell[2] + z});
        if (found == cells.end())
          continue;
        for (int id : found->second)
          if (glm::length(positions[id - 10] - camera) < radius)
            result.push_back(id);
      }
  return result;
}
