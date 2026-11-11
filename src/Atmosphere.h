#pragma once
#include <glm/glm.hpp>

class Atmosphere {
public:
    bool Initialize();
    void Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                const glm::vec3& center, float radius, bool logarithmicDepth = false,
                const glm::vec3& lightPosition = glm::vec3(0)) const;
    void Release();
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int depthTexture = 0;
    struct Uniforms {
        int inverseViewProjection, cameraPosition, planetCenter, lightPosition, planetRadius, logarithmicDepth;
    } uniforms = {};
    mutable int textureWidth = 0;
    mutable int textureHeight = 0;
};
