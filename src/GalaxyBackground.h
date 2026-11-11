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
    bool Initialize();
    void Render(const glm::mat4& viewProjection) const;
    void Release();
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int texture = 0;
};
