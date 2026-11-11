#include "CameraMotion.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

void CameraMotion::Look(glm::vec3 &front, glm::vec3 &up, float yaw, float pitch, bool surfaceUpright)
{
    front = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1), yaw, glm::normalize(up)) * glm::vec4(front, 0)));
    const auto right = glm::normalize(glm::cross(front, up));
    const auto rotation = glm::rotate(glm::mat4(1), pitch, right);
    const auto candidate = glm::normalize(glm::vec3(rotation * glm::vec4(front, 0)));
    if (surfaceUpright)
    {
        if (std::abs(glm::dot(candidate, glm::normalize(up))) < .995f)
            front = candidate;
    }
    else
    {
        front = candidate;
        up = glm::normalize(glm::vec3(rotation * glm::vec4(up, 0)));
        up = glm::normalize(up - front * glm::dot(front, up));
    }
}
void CameraMotion::Roll(glm::vec3 &front, glm::vec3 &up, float radians)
{
    front = glm::normalize(front);
    up = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1), radians, front) * glm::vec4(up, 0)));
    up = glm::normalize(up - front * glm::dot(front, up));
}
void CameraMotion::Carry(glm::dvec3 &position, glm::vec3 &front, glm::vec3 &up, const glm::dvec3 &oldCenter,
                         const glm::dvec3 &newCenter, const glm::dmat3 &rotationDelta)
{
    position = newCenter + rotationDelta * (position - oldCenter);
    front = glm::normalize(glm::vec3(rotationDelta * glm::dvec3(front)));
    up = glm::normalize(glm::vec3(rotationDelta * glm::dvec3(up)));
}
