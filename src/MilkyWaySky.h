#pragma once
#include <glm/glm.hpp>

// A bounded GPU radiative-transfer sky cache, refreshed on translation rather
// than rotation. Resolved/navigable catalogue stars remain a separate pass.
class MilkyWaySky {
public:
    bool Initialize();
    void Render(const glm::mat4& viewProjection,const glm::dvec3& worldCamera) const;
    void Invalidate() const { valid=false; }
    void Release();
    unsigned CacheBuilds() const { return builds; }
    unsigned Texture() const { return texture; }
private:
    void Bake(const glm::dvec3& worldCamera) const;
    unsigned bakeProgram=0,displayProgram=0,vao=0,texture=0,framebuffer=0;
    mutable glm::dvec3 bakedCamera{};
    mutable double lastBake=-1;
    mutable bool valid=false;
    mutable unsigned builds=0;
};
