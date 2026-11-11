#include "glad/glad.h"
#include "Atmosphere.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

unsigned int compileShaderProgram(const char*, const char*);

bool Atmosphere::Initialize() {
    const char* vertex = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0.0, 1.0);
}
)";
    const char* fragment = R"(
#version 330 core
in vec2 screenPosition;
// Dual-source outputs: the framebuffer becomes scattering + framebuffer * transmittance, so the scene
// never has to be copied out and later planets see earlier planets' atmospheres.
layout(location = 0, index = 0) out vec4 fragColor;
layout(location = 0, index = 1) out vec4 fragTransmittance;
uniform sampler2D sceneDepth;
uniform bool logarithmicDepth;
uniform mat4 inverseViewProjection;
uniform vec3 cameraPosition;
uniform vec3 planetCenter;
uniform vec3 lightPosition;
uniform float planetRadius;

vec2 sphereInterval(vec3 origin, vec3 direction, float radius) {
    float along = dot(origin, direction);
    float discriminant = along * along - dot(origin, origin) + radius * radius;
    if (discriminant < 0.0) return vec2(1.0, -1.0);
    float root = sqrt(discriminant);
    return vec2(-along - root, -along + root);
}

void main() {
    vec4 farPoint = inverseViewProjection * vec4(screenPosition, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz - cameraPosition * farPoint.w);
    vec3 origin = cameraPosition - planetCenter;
    vec2 shell = sphereInterval(origin, direction, planetRadius * 1.025);
    float start = max(shell.x, 0.0);
    float finish = shell.y;
    float opaqueDepth = texture(sceneDepth, screenPosition * 0.5 + 0.5).r;
    if (opaqueDepth < 1.0) {
        float geometryDistance;
        if (logarithmicDepth) {
            geometryDistance = (exp2(opaqueDepth * log2(1.0 + 20000.0 * 1e12)) - 1.0) / 1e12;
        } else {
            vec4 geometry = inverseViewProjection * vec4(screenPosition, opaqueDepth * 2.0 - 1.0, 1.0);
            geometryDistance = dot(geometry.xyz / geometry.w - cameraPosition, direction);
        }
        finish = min(finish, geometryDistance);
    }
    vec2 ground = sphereInterval(origin, direction, planetRadius * 1.0035);
    if (ground.x > 0.0 && ground.y >= ground.x)
        finish = min(finish, ground.x);
    if (finish <= start)
        discard;
    const int samples = 12;
    float interval = (finish - start) / float(samples);
    vec3 beta = vec3(37.0, 86.0, 210.0) / planetRadius;
    vec3 transmittance = vec3(1);
    vec3 scattering = vec3(0);
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        vec3 samplePosition = origin + direction * (start + (float(sampleIndex) + 0.5) * interval);
        float altitude = max(length(samplePosition) - planetRadius, 0.0);
        float density = exp(-altitude / (planetRadius * 0.00125));
        vec3 sunDirection = normalize(lightPosition - planetCenter - samplePosition);
        vec2 shadow = sphereInterval(samplePosition, sunDirection, planetRadius);
        float illumination = shadow.x > 0.0 && shadow.y >= shadow.x ? 0.0 : 1.0;
        float cosine = dot(direction, sunDirection);
        float phase = 0.75 * (1.0 + cosine * cosine);
        vec3 segment = exp(-beta * density * interval);
        scattering += transmittance * (1.0 - segment) * vec3(0.32, 0.55, 1.0)
            * phase * (0.03 + 0.97 * illumination);
        transmittance *= segment;
    }
    fragColor = vec4(scattering, 1);
    fragTransmittance = vec4(transmittance, 1);
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
    glGenTextures(1, &depthTexture);
    uniforms = {.inverseViewProjection = glGetUniformLocation(program, "inverseViewProjection"),
        .cameraPosition = glGetUniformLocation(program, "cameraPosition"),
        .planetCenter = glGetUniformLocation(program, "planetCenter"),
        .lightPosition = glGetUniformLocation(program, "lightPosition"),
        .planetRadius = glGetUniformLocation(program, "planetRadius"),
        .logarithmicDepth = glGetUniformLocation(program, "logarithmicDepth")};
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "sceneDepth"), 0);
    glUseProgram(0);
    return true;
}

namespace {
    // Pixel rectangle {x0, y0, x1, y1} (viewport-relative, half-open) that a sphere can cover on screen,
    // built from the two tangent planes through the projection apex per axis. Returns false when the
    // sphere cannot reach the screen or covers under two pixels in both directions.
    bool SphereRectangle(const glm::mat4& viewProjection, const glm::vec3& center, float radius,
                         int width, int height, int rectangle[4]) {
        const auto row = [&](int index) {
            return glm::dvec4(viewProjection[0][index], viewProjection[1][index],
                              viewProjection[2][index], viewProjection[3][index]);
        };
        const glm::dvec4 point(center, 1.0);
        const glm::dvec4 depthRow = row(3);
        const double radiusSquared = static_cast<double>(radius) * radius;
        const double depth = glm::dot(depthRow, point);
        const double depthLengthSquared = glm::dot(glm::dvec3(depthRow), glm::dvec3(depthRow));
        rectangle[0] = rectangle[1] = 0;
        rectangle[2] = width;
        rectangle[3] = height;
        if (depth < 0.0 && depth * depth > radiusSquared * depthLengthSquared)
            return false;
        // Straddling the camera plane (including the camera inside the sphere) has no finite bounds.
        const double inFront = depth * depth - radiusSquared * depthLengthSquared;
        if (depth <= 0.0 || inFront <= 0.0)
            return true;
        double low[2], high[2];
        for (int axis = 0; axis < 2; ++axis) {
            const glm::dvec4 axisRow = row(axis);
            const glm::dvec3 axisDirection(axisRow), depthDirection(depthRow);
            const double along = glm::dot(axisRow, point);
            const double b = along * depth - radiusSquared * glm::dot(axisDirection, depthDirection);
            const double c = along * along - radiusSquared * glm::dot(axisDirection, axisDirection);
            const double discriminant = b * b - inFront * c;
            if (discriminant < 0.0)
                return true;
            const double root = std::sqrt(discriminant);
            low[axis] = (b - root) / inFront;
            high[axis] = (b + root) / inFront;
        }
        const double size[2] = {static_cast<double>(width), static_cast<double>(height)};
        double pixelLow[2], pixelHigh[2];
        for (int axis = 0; axis < 2; ++axis) {
            pixelLow[axis] = (low[axis] * 0.5 + 0.5) * size[axis];
            pixelHigh[axis] = (high[axis] * 0.5 + 0.5) * size[axis];
        }
        if (pixelHigh[0] - pixelLow[0] < 2.0 && pixelHigh[1] - pixelLow[1] < 2.0)
            return false;
        for (int axis = 0; axis < 2; ++axis) {
            rectangle[axis] = std::max(0, static_cast<int>(std::floor(pixelLow[axis])) - 1);
            rectangle[axis + 2] = std::min(static_cast<int>(size[axis]), static_cast<int>(std::ceil(pixelHigh[axis])) + 1);
            if (rectangle[axis + 2] <= rectangle[axis])
                return false;
        }
        return true;
    }
}

void Atmosphere::Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                        const glm::vec3& center, float radius, bool logarithmicDepth,
                        const glm::vec3& lightPosition) const {
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    // Only pixels inside the shell's screen rectangle can change, so that is all that is copied and drawn.
    int rectangle[4];
    if (!SphereRectangle(viewProjection, center, radius * 1.025f, viewport[2], viewport[3], rectangle))
        return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    if (textureWidth != viewport[2] || textureHeight != viewport[3]) {
        textureWidth = viewport[2];
        textureHeight = viewport[3];
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, textureWidth, textureHeight, 0,
            GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, rectangle[0], rectangle[1], viewport[0] + rectangle[0],
        viewport[1] + rectangle[1], rectangle[2] - rectangle[0], rectangle[3] - rectangle[1]);
    glUseProgram(program);
    const auto inverse = glm::inverse(viewProjection);
    glUniformMatrix4fv(uniforms.inverseViewProjection, 1, GL_FALSE, glm::value_ptr(inverse));
    glUniform3fv(uniforms.cameraPosition, 1, glm::value_ptr(camera));
    glUniform3fv(uniforms.planetCenter, 1, glm::value_ptr(center));
    glUniform3fv(uniforms.lightPosition, 1, glm::value_ptr(lightPosition));
    glUniform1f(uniforms.planetRadius, radius);
    glUniform1i(uniforms.logarithmicDepth, logarithmicDepth);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_SRC1_COLOR, GL_ONE, GL_ZERO);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport[0] + rectangle[0], viewport[1] + rectangle[1],
        rectangle[2] - rectangle[0], rectangle[3] - rectangle[1]);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Atmosphere::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (depthTexture) glDeleteTextures(1, &depthTexture);
    program = vao = depthTexture = 0;
    textureWidth = textureHeight = 0;
}
