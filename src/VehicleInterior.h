#pragma once
#include <glm/glm.hpp>

class VehicleInterior {
public:
  bool Initialize();
  void Render(const glm::mat4 &vp, const glm::mat3 &cabinFrame, bool ranger,
              float fuelFraction) const;
  void Release();

private:
  unsigned int program = 0, vao[2] = {}, buffer[2] = {};
  int count[2] = {};
};
