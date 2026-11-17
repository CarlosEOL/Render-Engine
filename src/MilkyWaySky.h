#pragma once
#include <glm/glm.hpp>

// A bounded GPU radiative-transfer sky cache, refreshed on translation rather
// than rotation. Resolved/navigable catalogue stars remain a separate pass.
class MilkyWaySky {
public:
    bool Initialize();
    void Render(const glm::mat4& viewProjection,const glm::dvec3& worldCamera) const;
    void Invalidate() const { valid=false; blending=false; motionSampleValid=false; lastMotion=-1e30; }
    void ObserveCamera(const glm::dvec3& worldCamera,double realSeconds,float fieldOfView,int viewportHeight) const;
    void SetAdaptiveQuality(bool enabled) const {adaptive=enabled;motionSampleValid=false;lastMotion=-1e30;}
    bool MovingQuality() const;
    bool BakedMovingQuality() const {return bakedMoving;}
    int CacheWidth() const {return textureWidth;}
    int RaySteps() const {return bakedMoving?48:192;}
    bool NeedsFrame(const glm::dvec3& worldCamera) const;
    void Release();
    unsigned CacheBuilds() const { return builds; }
    unsigned Texture() const { return texture; }
    double CacheTimestamp() const {return lastBake;}
    float BlendFraction() const;
    float LastDrawBlend() const {return lastDrawBlend;}
private:
    void Bake(const glm::dvec3& worldCamera,bool moving) const;
    unsigned bakeProgram=0,displayProgram=0,vao=0,framebuffer=0,illustration=0;
    mutable unsigned texture=0,previousTexture=0;
    mutable glm::dvec3 bakedCamera{};
    mutable glm::dvec3 sampledCamera{};
    mutable bool adaptive=true,motionSampleValid=false,bakedMoving=false,previousMoving=false;
    mutable double lastMotion=-1e30,fadeSeconds=.32;
    mutable int textureWidth=2048,previousWidth=2048;
    mutable double lastBake=-1;
    mutable bool valid=false;
    mutable bool blending=false;
    mutable float lastDrawBlend=1;
    mutable unsigned builds=0;
};
