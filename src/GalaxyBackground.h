#pragma once
#include <array>
#include <glm/glm.hpp>

class GalaxyBackground {
public:
    struct Group {
        glm::vec3 center;
        float tilt;
        float radius;
    };
    static const std::array<Group, 3>& Groups();
    static constexpr float ReferenceRadius = 12.2f;
    static constexpr float StarDiskRadiusFraction = 11.0f / ReferenceRadius;
    bool Initialize();
    void Render(const glm::mat4& viewProjection, const glm::vec3& cameraPosition) const;
    void Release();
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int texture = 0;
    int viewProjectionLocation = -1;
    int transformLocation = -1;
    int opacityLocation = -1;
};
