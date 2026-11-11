#pragma once
#include "QuantumStates.h"
#include <glm/vec4.hpp>
#include <vector>

class QuantumSimulation
{
  public:
    bool Initialize();
    void Release();
    void Select(int example);
    void Advance(double seconds);
    void Render(float aspect) const;
    int Selected() const
    {
        return selected;
    }
    double Time() const
    {
        return time;
    }
    std::string Information() const;

  private:
    int selected = 0;
    double time = 0;
    double visibleNorm = 1;
    double mean = 0;
    double deviation = 0;
    double peakDensity = 1;
    unsigned int program = 0, vao = 0, texture = 0;
    QuantumStates::Solver solver;
    std::vector<glm::vec4> pixels; // reused upload buffer
    int textureWidth = 0, textureHeight = 0;
    void Upload();
};
