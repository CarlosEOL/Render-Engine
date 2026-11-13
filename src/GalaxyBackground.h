#pragma once
#include <array>
#include <glm/glm.hpp>

class GalaxyBackground {
public:
    struct Group {
        glm::dvec3 center;
        float tilt;
        float radius;
    };
    static const std::array<Group, 3>& Groups();
    static constexpr float ReferenceRadius = 12.2f;
    static constexpr float StarDiskRadiusFraction = 11.0f / ReferenceRadius;
    bool Initialize();
    static constexpr int DistantGalaxyCount = 256;
    void Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition,float magnitudeLimit=7.f) const;
    void Release();
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int buffer = 0;
    int vertexCount = 0;
    unsigned int texture = 0;
    unsigned int variants = 0;
};
