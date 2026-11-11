#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "glad/glad.h"
#include "GalaxyBackground.h"
#include "AssetManager.h"
#include "Astronomy.h"
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*, const char*);

const std::array<GalaxyBackground::Group, 3>& GalaxyBackground::Groups() {
    static const std::array<Group, 3> groups = {{
        {{0, static_cast<float>(.4e6*Astronomy::auPerLightYear), static_cast<float>(-2.5e6*Astronomy::auPerLightYear)}, .7f, static_cast<float>(50000*Astronomy::auPerLightYear)},
        {{static_cast<float>(1.2e6*Astronomy::auPerLightYear),static_cast<float>(-.3e6*Astronomy::auPerLightYear),static_cast<float>(-3e6*Astronomy::auPerLightYear)},1,static_cast<float>(48000*Astronomy::auPerLightYear)},
        {{static_cast<float>(-1.7e6*Astronomy::auPerLightYear),static_cast<float>(.8e6*Astronomy::auPerLightYear),static_cast<float>(-4e6*Astronomy::auPerLightYear)},1.3f,static_cast<float>(60000*Astronomy::auPerLightYear)}
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
    texture = AssetManager::LoadTexture(assetPath.string().c_str(), false);
    const auto variantsPath=assetPath.parent_path()/"galaxy-variants.png";
    variants=AssetManager::LoadTexture(variantsPath.string().c_str(),false);
    if (!texture)
        return false;
    // The sprites are now distant, so use the generated mip chain to avoid
    // shimmering and losing the galaxy's brightness to point sampling.
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    gl_Position.z=gl_Position.w*.999999;
}
)";
    const char* fragment = R"(
#version 330 core
uniform sampler2D galaxy;
uniform float opacity;
uniform int variant;
in vec2 uv;
out vec4 color;
void main() {
    vec2 coordinate=uv;
    if(variant>0)coordinate.y=uv.y*.5+(variant==1?.5:0.0);
    vec4 image = texture(galaxy, coordinate);
    color = vec4(image.rgb * image.a * 0.9 * opacity, 1);
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
    return true;
}

void GalaxyBackground::Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition) const {
    if (!program || !texture)
        return;
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glUniform1i(glGetUniformLocation(program, "galaxy"), 0);
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    const int opacityLocation = glGetUniformLocation(program, "opacity");
    glDepthMask(GL_FALSE);
    const bool blending=glIsEnabled(GL_BLEND)==GL_TRUE;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(vao);
    int index=0;
    for (const auto& group : Groups()) {
        // A galaxy sprite is only a backdrop. Fade it out before the camera can
        // reach the flat image, otherwise it fills the view as a giant PNG.
        const float distance = static_cast<float>(glm::length(cameraPosition - glm::dvec3(group.center)));
        const float opacity = glm::smoothstep(group.radius * 1.25f, group.radius * 2.5f, distance);
        if (opacity <= 0.001f){++index;continue;}
        const int variant=variants?index:0;
        glBindTexture(GL_TEXTURE_2D,variant?variants:texture);
        glUniform1i(glGetUniformLocation(program,"variant"),variant);
        auto transform = glm::translate(glm::mat4(1), glm::vec3(glm::dvec3(group.center)-cameraPosition));
        transform = glm::rotate(transform, group.tilt, glm::vec3(1,0,0));
        transform = glm::scale(transform, glm::vec3(group.radius,group.radius,group.radius*(variant?.75f:1.0f)));
        glUniform1f(opacityLocation, opacity);
        glUniformMatrix4fv(glGetUniformLocation(program, "transform"), 1, GL_FALSE, glm::value_ptr(transform));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        ++index;
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    if(!blending)glDisable(GL_BLEND);
}

void GalaxyBackground::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (texture) glDeleteTextures(1, &texture);
    if (variants) glDeleteTextures(1,&variants);
    program = vao = texture = variants = 0;
}
