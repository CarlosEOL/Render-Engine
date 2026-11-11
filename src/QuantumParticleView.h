#pragma once
#include "QuantumStates.h"
#include <glm/glm.hpp>

class QuantumParticleView
{
  public:
    bool Initialize();
    void Update(const QuantumStates::Example &example, double time,
                const std::vector<std::complex<double>> *numericalWave = nullptr);
    void Render(const QuantumStates::Example &example, double time, int width, int height, float fieldOfView,
                const glm::vec3 &camera, const glm::vec3 &front, const glm::vec3 &up) const;
    void Release();
    float SuggestedDistance(const QuantumStates::Example &example) const;

  private:
    struct Point
    {
        glm::vec3 position;
        float phase;
    };
    unsigned int program = 0, pointVao = 0, pointBuffer = 0, lineVao = 0, lineBuffer = 0;
    int pointCount = 0;
    int cachedOrbital = -1;
    std::vector<Point> points;
};
