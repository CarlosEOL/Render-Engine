#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "glad/glad.h"
#include "GalaxyBackground.h"
#include "AssetManager.h"
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*, const char*);

const std::array<GalaxyBackground::Group, 3>& GalaxyBackground::Groups() {
    static const std::array<Group, 3> groups = {{
        {{0, 12, -42}, 0.7f, 12.2f},
        {{48, -8, -55}, 1.0f, 12.2f},
        {{-45, 20, -65}, 1.3f, 12.2f}
    }};
    return groups;
}

bool GalaxyBackground::Initialize() {
    std::filesystem::path assetPath = "assets/galaxy.png";
#ifdef _WIN32
    wchar_t executable[32768];
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (length > 0 && length < 32768)
        assetPath = std::filesystem::path(executable).parent_path() / assetPath;
#endif
    texture = AssetManager::LoadTexture(assetPath.string().c_str(), true);
    if (!texture)
        return false;
    const char* vertex = R"(
#version 330 core
uniform mat4 transform;
uniform mat4 viewProjection;
out vec2 uv;
void main() {
    const vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    vec2 corner = corners[gl_VertexID];
    uv = corner * 0.5 + 0.5;
    gl_Position = viewProjection * transform * vec4(corner.x, 0, corner.y, 1);
}
)";
    const char* fragment = R"(
#version 330 core
uniform sampler2D galaxy;
in vec2 uv;
out vec4 color;
void main() {
    vec4 image = texture(galaxy, uv);
    color = vec4(image.rgb * image.a * 0.8, 1);
}
)";
    program = compileShaderProgram(vertex, fragment);
    int linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "galaxy"), 0);
    glUseProgram(0);
    viewProjectionLocation = glGetUniformLocation(program, "viewProjection");
    transformLocation = glGetUniformLocation(program, "transform");
    return true;
}

void GalaxyBackground::Render(const glm::mat4& viewProjection) const {
    if (!program || !texture)
        return;
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(vao);
    for (const auto& group : Groups()) {
        auto transform = glm::translate(glm::mat4(1), group.center);
        transform = glm::rotate(transform, group.tilt, glm::vec3(1,0,0));
        transform = glm::scale(transform, glm::vec3(group.radius));
        glUniformMatrix4fv(transformLocation, 1, GL_FALSE, glm::value_ptr(transform));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

void GalaxyBackground::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (texture) glDeleteTextures(1, &texture);
    program = vao = texture = 0;
}
