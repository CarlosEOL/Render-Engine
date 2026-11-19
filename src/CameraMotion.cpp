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

void CameraMotion::SurfaceAnchor::Set(const glm::dmat3& rotation,const glm::dvec3& offset,const glm::vec3& front) {
    const auto inverse=glm::transpose(rotation);
    direction=glm::length(offset)>0?glm::normalize(inverse*offset):glm::dvec3(0,0,1);
    localFront=glm::normalize(inverse*glm::dvec3(front));
    lastWorldFront=front;
}

void CameraMotion::SurfaceAnchor::CaptureLook(const glm::dmat3& rotation,const glm::vec3& front) {
    if(front!=lastWorldFront)localFront=glm::normalize(glm::transpose(rotation)*glm::dvec3(front));
}

void CameraMotion::SurfaceAnchor::Walk(const glm::dmat3& rotation,const glm::dvec3& displacement) {
    const auto local=glm::transpose(rotation)*displacement;
    const auto tangent=local-direction*glm::dot(direction,local);
    const auto next=glm::normalize(direction*radiusAu+tangent);
    const auto axis=glm::cross(direction,next);
    const double sine=glm::length(axis);
    if(sine>1e-14){
        const auto transport=glm::rotate(glm::dmat4(1),std::atan2(sine,glm::dot(direction,next)),axis/sine);
        localFront=glm::normalize(glm::dvec3(transport*glm::dvec4(localFront,0)));
    }
    direction=next;
}

void CameraMotion::SurfaceAnchor::Place(const glm::dmat3& rotation,const glm::dvec3& centre,
    glm::dvec3& position,glm::vec3& front,glm::vec3& up) {
    const auto radial=rotation*direction;
    position=centre+radiusAu*radial;
    up=glm::vec3(radial);
    lastWorldFront=front=glm::vec3(rotation*localFront);
}
