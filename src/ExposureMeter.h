#pragma once
#include <glm/vec3.hpp>

// A bounded GPU meter and temporal exposure history; no runtime pixel readback.
class ExposureMeter {
public:
  bool Initialize();
  void Update(unsigned int scene, const glm::vec3 &stellarHint,
              float realSeconds) const;
  void Reset() const { valid = false; }
  unsigned int Texture() const { return textures[current]; }
  void Release();

private:
  unsigned int program = 0, vao = 0, framebuffer = 0, textures[2] = {};
  mutable int current = 0;
  mutable bool valid = false;
};
