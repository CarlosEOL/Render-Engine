#pragma once
#include <glm/glm.hpp>

namespace CameraMotion
{
void Look(glm::vec3 &front, glm::vec3 &up, float yaw, float pitch, bool surfaceUpright);
void Roll(glm::vec3 &front, glm::vec3 &up, float radians);
void Carry(glm::dvec3 &position, glm::vec3 &front, glm::vec3 &up, const glm::dvec3 &oldCenter,
           const glm::dvec3 &newCenter, const glm::dmat3 &rotationDelta);

// Keep a landed pose on the rotating body without feeding rounded world
// coordinates or rendered wave heights back into the next frame.
struct SurfaceAnchor {
    glm::dvec3 direction{0,0,1};
    double radiusAu=0;

    void Set(const glm::dmat3& rotation,const glm::dvec3& offset,const glm::vec3& front);
    void CaptureLook(const glm::dmat3& rotation,const glm::vec3& front);
    void Walk(const glm::dmat3& rotation,const glm::dvec3& displacement);
    void Place(const glm::dmat3& rotation,const glm::dvec3& centre,
        glm::dvec3& position,glm::vec3& front,glm::vec3& up);

private:
    glm::dvec3 localFront{0,0,-1};
    glm::vec3 lastWorldFront{0,0,-1};
};
} // namespace CameraMotion
