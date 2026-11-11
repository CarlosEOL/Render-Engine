#pragma once
#include "QuantumParticleView.h"
#include "QuantumStates.h"

class QuantumSimulation
{
  public:
    bool Initialize();
    void Release();
    void Select(int example);
    void Advance(double seconds);
    void Render(int width, int height, float fieldOfView, const glm::vec3 &camera, const glm::vec3 &front,
                const glm::vec3 &up) const;
    float ViewDistance() const
    {
        return particles.SuggestedDistance(QuantumStates::Examples()[selected]);
    }
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
    QuantumParticleView particles;
    void Upload();
};
