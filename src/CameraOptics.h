#pragma once
#include <glm/glm.hpp>

class CameraOptics {
public:
    bool Initialize();
    void BeginFrame();
    void Render(int profile, float fieldOfView, float exposureEv, bool hdrMode) const;
    void Release();
    static const char* Name(int profile);
    static float FieldOfView(int profile, float aspect);
    static glm::vec2 MapScreen(int profile, const glm::vec2& screen, float aspect, float fieldOfView);
private:
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int framebuffer = 0;
    unsigned int sceneColor = 0;
    unsigned int sceneDepth = 0;
    struct Uniforms {
        int profile, distortionModel, distortion, redTca, blueTca, vignetting, sensorScale, texel, exposureGain, hdrMode;
    } uniforms = {};
    bool frameActive = false;
    bool framebufferComplete = false;
    mutable int width = 0;
    mutable int height = 0;
};
