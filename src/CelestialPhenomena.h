#pragma once
#include <array>
#include <glm/glm.hpp>
#include <string>
namespace CelestialPhenomena {
inline constexpr int firstId=2000000;
struct Nebula {int id;const char* name;const char* classification;glm::dvec3 positionAu;double radiusLy;int style;glm::vec3 color;};
const std::array<Nebula,4>& Nebulae();
bool IsNebula(int id);
const Nebula& Get(int id);
std::string Information(int id);
}
