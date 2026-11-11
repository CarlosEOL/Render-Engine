#pragma once
#include <glm/glm.hpp>

namespace CameraMotion
{
void Look(glm::vec3 &front, glm::vec3 &up, float yaw, float pitch, bool surfaceUpright);
void Roll(glm::vec3 &front, glm::vec3 &up, float radians);
void Carry(glm::dvec3 &position, glm::vec3 &front, glm::vec3 &up, const glm::dvec3 &oldCenter,
           const glm::dvec3 &newCenter, const glm::dmat3 &rotationDelta);
} // namespace CameraMotion
