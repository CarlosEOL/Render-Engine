#pragma once
#include <glm/glm.hpp>
#include <limits>
#include <string>

// Procedural visual plasma, evaluated on the GPU from the simulation clock.
// This does not solve magnetohydrodynamics or model space-weather forces.
class StellarActivity {
public:
  bool Initialize();
  void Release();
  void SetEnabled(bool value) { enabled = value; }
  void Trigger(double seconds) { eruptionStart = seconds; }
  void BindSurface(unsigned int surfaceProgram, int seed, double seconds) const;
  void Render(const glm::mat4 &vp, const glm::mat4 &model, float temperature,
              int seed, double seconds) const;
  static std::string ShaderFunctions();

private:
  bool enabled = true;
  double eruptionStart = -std::numeric_limits<double>::infinity();
  unsigned int program = 0, vao = 0, buffer = 0;
  int vertices = 0;
  void BindTime(unsigned int target, int seed, double seconds) const;
};
