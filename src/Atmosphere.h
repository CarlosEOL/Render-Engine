#pragma once
#include <glm/glm.hpp>
#include "PlanetWeather.h"

class Atmosphere {
public:
    bool Initialize();
    void Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                const glm::vec3& center, float radius, bool logarithmicDepth = false,
                const glm::vec3& lightPosition = glm::vec3(0),bool earthClouds=false,
                double seconds=0,const glm::mat3& rotation=glm::mat3(1),bool oceanReflections=false,float waterSpecularScale=1,
                const PlanetWeather::State* weather=nullptr) const;
    void Release();
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int sceneTexture = 0;
    unsigned int depthTexture = 0;
    mutable int textureWidth = 0;
    mutable int textureHeight = 0;
};
