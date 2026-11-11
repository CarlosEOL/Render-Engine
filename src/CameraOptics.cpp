#include "glad/glad.h"
#include "CameraOptics.h"
#include "LensCalibration.h"
#include <cmath>
#include <initializer_list>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*, const char*);

namespace {
    using LensCalibration::profiles;

    float UndistortedRadius(int profile, float distorted) {
        const auto& lens = profiles[profile];
        float radius = distorted;
        for (int iteration = 0; iteration < 8; ++iteration) {
            float value = radius;
            float derivative = 1.0f;
            if (lens.distortionModel == 1) {
                const float k = lens.distortion.x;
                value = radius * (1.0f - k + k * radius * radius);
                derivative = 1.0f - k + 3.0f * k * radius * radius;
            } else if (lens.distortionModel == 2) {
                const auto c = lens.distortion;
                const float d = 1.0f - c.x - c.y - c.z;
                value = radius * (((c.x * radius + c.y) * radius + c.z) * radius + d);
                derivative = ((4.0f * c.x * radius + 3.0f * c.y) * radius + 2.0f * c.z) * radius + d;
            }
            radius -= (value - distorted) / derivative;
        }
        return radius;
    }
}

const char* CameraOptics::Name(int profile) {
    return profiles[profile].name;
}

float CameraOptics::FieldOfView(int profile, float aspect) {
    if (profile == 0) return 45.0f;
    if (profile == 1)
        return glm::degrees(2.0f * std::atan(std::tan(glm::radians(50.0f)) / aspect));
    // A 36 mm sensor width, vertically cropped to the viewport's aspect ratio.
    const auto& lens = profiles[profile];
    const float realFocal = lens.focalLength * (lens.distortionModel == 2
        ? 1.0f - lens.distortion.x - lens.distortion.y - lens.distortion.z : 1.0f);
    return glm::degrees(2.0f * std::atan(18.0f / (realFocal * aspect)));
}

glm::vec2 CameraOptics::MapScreen(int profile, const glm::vec2& screen, float aspect, float fieldOfView) {
    const float zoom = std::tan(glm::radians(fieldOfView * 0.5f))
        / std::tan(glm::radians(FieldOfView(profile, aspect) * 0.5f));
    const float radius = glm::length(screen * glm::vec2(1.5f, 1.5f / aspect)) * zoom;
    return radius > 0.00001f ? screen * (UndistortedRadius(profile, radius) / radius) : screen;
}

bool CameraOptics::Initialize() {
    const char* vertex = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0, 1);
}
)";
    const char* fragment = R"(
#version 330 core
in vec2 screenPosition;
out vec4 fragColor;
uniform sampler2D scene;
uniform int profile;
uniform int distortionModel;
uniform vec3 distortion;
uniform vec3 redTca;
uniform vec3 blueTca;
uniform vec3 vignetting;
uniform vec2 sensorScale;
uniform vec2 texel;
uniform float exposureGain;
uniform bool hdrMode;

float undoTca(float distorted, vec3 terms) {
    float radius = distorted;
    for (int i = 0; i < 6; ++i) {
        float value = radius * ((terms.x * radius + terms.y) * radius + terms.z);
        float derivative = (3.0 * terms.x * radius + 2.0 * terms.y) * radius + terms.z;
        radius -= (value - distorted) / derivative;
    }
    return radius;
}

float undoDistortion(float distorted) {
    float radius = distorted;
    for (int i = 0; i < 8; ++i) {
        float value = radius;
        float derivative = 1.0;
        if (distortionModel == 1) {
            value = radius * (1.0 - distortion.x + distortion.x * radius * radius);
            derivative = 1.0 - distortion.x + 3.0 * distortion.x * radius * radius;
        } else if (distortionModel == 2) {
            float d = 1.0 - distortion.x - distortion.y - distortion.z;
            value = radius * (((distortion.x * radius + distortion.y) * radius + distortion.z) * radius + d);
            derivative = ((4.0 * distortion.x * radius + 3.0 * distortion.y) * radius + 2.0 * distortion.z) * radius + d;
        }
        radius -= (value - distorted) / derivative;
    }
    return radius;
}

void main() {
    float radiusSquared = dot(screenPosition, screenPosition);
    float radius = length(screenPosition * sensorScale);
    float scale = radius > 0.00001 ? undoDistortion(radius) / radius : 1.0;
    vec2 mapped = screenPosition * scale;
    vec2 uv = 0.5 + 0.5 * mapped;
    if (any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1)))) {
        fragColor = vec4(0, 0, 0, 1);
        return;
    }
    float redScale = radius > 0.00001 ? undoDistortion(undoTca(radius, redTca)) / radius : 1.0;
    float blueScale = radius > 0.00001 ? undoDistortion(undoTca(radius, blueTca)) / radius : 1.0;
    vec3 color = vec3(texture(scene, 0.5 + 0.5 * screenPosition * redScale).r,
        texture(scene, uv).g, texture(scene, 0.5 + 0.5 * screenPosition * blueScale).b);
    if (profile == 1) {
        vec2 offset = texel * smoothstep(0.05, 1.0, radiusSquared) * 2.5;
        vec3 peripheral = (texture(scene, uv + vec2(offset.x, 0)).rgb
            + texture(scene, uv - vec2(offset.x, 0)).rgb
            + texture(scene, uv + vec2(0, offset.y)).rgb
            + texture(scene, uv - vec2(0, offset.y)).rgb) * 0.25;
        color = mix(color, peripheral, smoothstep(0.05, 0.8, radiusSquared) * 0.65);
    }
    // PA vignetting uses radius normalised to the full-frame corner.
    float vignetteRadiusSquared = radius * radius / 3.25;
    color *= max(0.01, 1.0 + vignetteRadiusSquared * (vignetting.x
        + vignetteRadiusSquared * (vignetting.y + vignetteRadiusSquared * vignetting.z)));
    vec3 glow = max(textureLod(scene, uv, 2.0).rgb - 1.0, vec3(0)) * 0.12
        + max(textureLod(scene, uv, 4.0).rgb - 0.4, vec3(0)) * 0.12
        + max(textureLod(scene, uv, 6.0).rgb - 0.1, vec3(0)) * 0.08;
    color += glow;
    // Flare is an illustrative optical profile. Only distortion/TCA/vignetting
    // use measured Lensfun coefficients.
    if (profile == 1) {
        for (int ray = 0; ray < 8; ++ray) {
            float angle = float(ray) * 0.78539816;
            vec2 axis = vec2(cos(angle), sin(angle));
            color += max(textureLod(scene, uv + axis * texel * 7.0, 2.0).rgb - 1.0, vec3(0)) * 0.018;
        }
    } else if (profile >= 2) {
        for (int ghost = 1; ghost <= 5; ++ghost) {
            float scale = 0.28 + float(ghost) * 0.31 + float(profile) * 0.025;
            vec2 ghostUv = 0.5 - screenPosition * scale * 0.5;
            if (all(greaterThan(ghostUv, vec2(0))) && all(lessThan(ghostUv, vec2(1)))) {
                vec3 tint = 0.55 + 0.45 * cos(vec3(0, 2.1, 4.2) + float(ghost + profile));
                color += max(textureLod(scene, ghostUv, 3.0).rgb - 0.35, vec3(0))
                    * tint * 0.06 / float(ghost);
            }
        }
        float angle = profile == 3 ? 0.0 : float(profile) * 0.17;
        vec2 axis = vec2(cos(angle), sin(angle));
        for (int streak = -4; streak <= 4; ++streak)
            color += max(textureLod(scene, uv + axis * texel * float(streak) * 6.0, 2.0).rgb - 1.0, vec3(0))
                * (profile == 3 ? 0.025 : 0.009);
    }
    color = max(color * exposureGain, vec3(0));
    if (hdrMode)
        color = clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
    else
        color = clamp(color, 0.0, 1.0);
    color = mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), color));
    fragColor = vec4(color, 1.0);
}
)";
    program = compileShaderProgram(vertex, fragment);
    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &sceneTexture);
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &sceneColor);
    glGenRenderbuffers(1, &sceneDepth);
    return true;
}

void CameraOptics::BeginFrame() {
    frameActive = false;
    if (!program) return;
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    glActiveTexture(GL_TEXTURE0);
    if (width != viewport[2] || height != viewport[3]) {
        width = viewport[2];
        height = viewport[3];
        for (const auto texture : {sceneTexture, sceneColor}) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
    frameActive = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!frameActive)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void CameraOptics::Render(int profile, float fieldOfView, float exposureEv, bool hdrMode) const {
    if (!program || !frameActive) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "scene"), 0);
    glUniform1i(glGetUniformLocation(program, "profile"), profile);
    const auto& lens = profiles[profile];
    glUniform1i(glGetUniformLocation(program, "distortionModel"), lens.distortionModel);
    glUniform3fv(glGetUniformLocation(program, "distortion"), 1, glm::value_ptr(lens.distortion));
    glUniform3fv(glGetUniformLocation(program, "redTca"), 1, glm::value_ptr(lens.redTca));
    glUniform3fv(glGetUniformLocation(program, "blueTca"), 1, glm::value_ptr(lens.blueTca));
    glUniform3fv(glGetUniformLocation(program, "vignetting"), 1, glm::value_ptr(lens.vignetting));
    const float aspect = static_cast<float>(width) / height;
    const float zoom = std::tan(glm::radians(fieldOfView * 0.5f))
        / std::tan(glm::radians(FieldOfView(profile, aspect) * 0.5f));
    glUniform2f(glGetUniformLocation(program, "sensorScale"), 1.5f * zoom, 1.5f * zoom / aspect);
    glUniform1f(glGetUniformLocation(program, "exposureGain"), std::exp2(exposureEv));
    glUniform1i(glGetUniformLocation(program, "hdrMode"), hdrMode);
    glUniform2f(glGetUniformLocation(program, "texel"), 1.0f / width, 1.0f / height);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CameraOptics::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (sceneTexture) glDeleteTextures(1, &sceneTexture);
    if (sceneColor) glDeleteTextures(1, &sceneColor);
    if (sceneDepth) glDeleteRenderbuffers(1, &sceneDepth);
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    program = vao = sceneTexture = sceneColor = sceneDepth = framebuffer = 0;
    frameActive = false;
    width = height = 0;
}
