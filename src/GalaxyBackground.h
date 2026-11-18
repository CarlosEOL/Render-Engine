#pragma once
#include <array>
#include <glm/glm.hpp>
#include <vector>

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
    bool SetAutoExposure(bool enabled){const bool changed=autoExposure!=enabled;autoExposure=enabled;return changed;}
    static constexpr int DistantGalaxyCount = 256;
    void Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition,float magnitudeLimit=7.f,bool localVolumes=true) const;
    void ObserveCamera(const glm::dvec3& camera,double seconds) const;
    void SetAdaptiveQuality(bool enabled) const {adaptive=enabled;motionValid=false;}
    bool NeedsFrame() const;
    unsigned VolumeBuilds() const {return volumeBuilds;}
    int VolumeWidth(int galaxyId) const {for(const auto& cache:volumeCaches)if(cache.valid && cache.id==galaxyId)return cache.width;return 0;}
    void Release();
private:
    bool autoExposure=false;
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int buffer = 0;
    int vertexCount = 0;
    unsigned int texture = 0;
    unsigned int variants = 0;
    unsigned int hoagTexture = 0;
    struct VolumeGalaxy {int id,morphology;glm::dvec3 centre;glm::dmat3 frame;double radius;float absoluteMagnitude;};
    struct VolumeCache {int id=-1,width=0,previousWidth=0;unsigned texture=0,previous=0;glm::dvec3 camera{};double baked=-1;bool valid=false,moving=false,blending=false;mutable float lastBlend=1;};
    std::vector<VolumeGalaxy> volumeGalaxies;
    mutable std::array<VolumeCache,2> volumeCaches{};
    unsigned volumeBake=0,volumeDisplay=0,volumeFramebuffer=0;
    mutable unsigned volumeBuilds=0;
    mutable bool adaptive=true,motionValid=false;
    mutable glm::dvec3 observedCamera{};
    mutable double lastMotion=-1e30;
    void BakeVolume(VolumeCache& cache,const VolumeGalaxy& galaxy,const glm::dvec3& local,bool moving) const;
    void DrawVolume(VolumeCache& cache,const VolumeGalaxy& galaxy,const glm::mat4& vp,float weight,float brightness) const;
};
