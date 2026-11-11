#pragma once
#include <glm/glm.hpp>

class CameraOptics
{
  public:
    bool Initialize();
    void BeginFrame();
    void Render(int profile, float fieldOfView, float exposureEv, bool hdrMode, float flareStrength = .45f,
                bool reuseScene = false) const;
    void Release();
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
    unsigned int flareProgram = 0, flareFramebuffer = 0, flareTexture = 0;
    bool frameActive = false;
    mutable int width = 0;
    mutable int height = 0;
};
