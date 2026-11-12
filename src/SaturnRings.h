#pragma once
#include <glm/glm.hpp>

class SaturnRings {
public:
  bool Initialize();
  void Render(const glm::mat4 &vp, const glm::dvec3 &centerRelativeAu,
              const glm::dmat3 &orientation,
              const glm::dvec3 &sunlightDirection, double planetRadiusAu,
              double massGravityAu, double seconds) const;
  void Release();
  static const char *ShaderFunctions();

private:
  unsigned int program = 0, vao = 0, buffer = 0, rockVao = 0, rockBuffer = 0;
  int count = 0, rockCount = 0;
};
