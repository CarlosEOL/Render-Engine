#pragma once
#include <glm/glm.hpp>
#include <array>
#include "ExposureMeter.h"

class CameraOptics
{
  public:
    bool Initialize();
    void BeginFrame();
    void Render(int profile, float fieldOfView, float exposureEv, bool hdrMode, float flareStrength = .45f,
                bool reuseScene = false, float realSeconds = 1.0f/60) const;
    void SetAutoExposure(bool value) { autoExposure = value; exposureMeter.Reset(); }
    bool IsAutoExposure() const { return autoExposure; }
    void SetReferenceGlare(bool value) { referenceGlare = value; }
    void SetStellarHint(const glm::vec3& value) { stellarHint = value; }
    // xy = projected centre, z = angular radius / vertical FOV, w = 1 star,
    // 0 accretion flow. A count of zero deliberately accepts no sources.
    void SetSystemSources(const std::array<glm::vec4,8>& sources,int count){systemSources=sources;systemSourceCount=glm::clamp(count,0,8);}
    void SetBlackHoleHint(const glm::vec3& value) { blackHoleHint=value; }
    void SetDiskForegroundTexture(unsigned int texture) { diskForegroundTexture=texture; }
    void SetBlackHoleVisibility(bool value) { blackHoleVisibility=value;exposureMeter.Reset(); }
    void Release();
    unsigned int SceneTexture() const { return sceneTexture; }
    unsigned int SceneInputTexture() const { return sceneColor; }
    static const char *Name(int profile);
    static float FieldOfView(int profile, float aspect);
    static glm::vec2 MapScreen(int profile, const glm::vec2 &screen, float aspect, float fieldOfView);

  private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int sceneTexture = 0;
    unsigned int framebuffer = 0;
    unsigned int sceneColor = 0;
    unsigned int sceneDepth = 0;
    unsigned int depthTexture = 0;
    unsigned int hdrDiskProgram=0,hdrDiskFramebuffer=0,hdrDiskTexture=0;
    unsigned int diskForegroundTexture=0;
    mutable bool hdrDiskValid=false;
    unsigned int flareProgram = 0, flareFramebuffer = 0, flareTexture = 0;
    bool frameActive = false;
    bool autoExposure = true, referenceGlare = true;
    bool blackHoleVisibility=true;
    ExposureMeter exposureMeter;
    glm::vec3 stellarHint = glm::vec3(-1,-1,0);
    glm::vec3 blackHoleHint=glm::vec3(-1,-1,0);
    std::array<glm::vec4,8> systemSources{};
    int systemSourceCount=-1; // Standalone image probes may use image extraction.
    mutable int width = 0;
    mutable int height = 0;
};
