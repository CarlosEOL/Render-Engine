#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "glad/glad.h"
#include "GalaxyBackground.h"
#include "AssetManager.h"
#include "Astronomy.h"
#include "UniverseCatalog.h"
#include "MilkyWayModel.h"
#include <filesystem>
#include <vector>
#include <random>
#include <cstddef>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*, const char*);

const std::array<GalaxyBackground::Group, 3>& GalaxyBackground::Groups() {
    static const std::array<Group,3> groups=[](){
        std::array<Group,3> result;
        for(int i=0;i<3;++i){const auto galaxy=Universe::GalaxyAt(i+1);result[i]={galaxy.centerAu,galaxy.tilt,static_cast<float>(galaxy.axesAu.x)};}
        return result;
    }();
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
    for(auto id:{texture,variants})if(id){glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);}
    glBindTexture(GL_TEXTURE_2D, 0);
    const char* vertex = R"(
#version 330 core
layout(location=0) in vec3 centerHigh;
layout(location=1) in vec3 centerLow;
layout(location=2) in vec3 normal;
layout(location=3) in vec3 majorAxis;
layout(location=4) in vec4 properties; // radius, intrinsic thickness, atlas variant, absolute magnitude
uniform mat4 viewProjection;
uniform vec3 cameraHigh,cameraLow;
uniform float magnitudeLimit;
out vec2 uv;
out float visibility;
out float morphology;
void main() {
    const vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    vec2 corner = corners[gl_VertexID%6];
    uv = corner * 0.5 + 0.5;
    vec3 relative=(centerHigh-cameraHigh)+(centerLow-cameraLow);
    float distance=max(length(relative),1.0);
    float apparent=properties.w+5.0*log(max(distance/206264.806247/10.0,1e-12))/log(10.0);
    visibility=(1.0-smoothstep(magnitudeLimit-.3,magnitudeLimit+.3,apparent))
        *smoothstep(properties.x*1.25,properties.x*2.5,distance);
    morphology=properties.z;
    if(visibility<=0.0){gl_Position=vec4(2,2,2,1);return;}
    vec3 direction=relative/distance;
    vec3 projectedAxis=majorAxis-direction*dot(majorAxis,direction);
    if(length(projectedAxis)<.01)projectedAxis=cross(normal,direction);
    vec3 right=normalize(projectedAxis),up=normalize(cross(direction,right));
    float cosine=dot(normal,direction);
    float ratio=sqrt(properties.y*properties.y+(1.0-properties.y*properties.y)*cosine*cosine);
    // Finite-thickness projected galaxies remain visible when edge-on. Their
    // centers stay in world space; these are not camera-attached sky stickers.
    gl_Position = viewProjection * vec4(relative+properties.x*(corner.x*right+corner.y*up*ratio),1);
    gl_Position.z=gl_Position.w*.999999;
}
)";
    const char* fragment = R"(
#version 330 core
uniform sampler2D galaxy;
uniform sampler2D galaxyVariants;
in vec2 uv;
in float visibility;
in float morphology;
out vec4 color;
void main() {
    vec2 coordinate=uv;
    int variant=int(morphology+.5);
    if(variant>0)coordinate.y=uv.y*.5+(variant==1?.5:0.0);
    vec2 atlasSize=vec2(textureSize(galaxyVariants,0));
    float atlasLod=clamp(log2(max(max(length(dFdx(coordinate)*atlasSize),length(dFdy(coordinate)*atlasSize)),1.0)),0.0,5.0);
    vec4 image = variant==0?texture(galaxy,coordinate):textureLod(galaxyVariants,coordinate,atlasLod);
    // Keep atlas filtering inside one image instead of leaking the adjacent
    // galaxy through the seam at coarse mip levels.
    float border=smoothstep(0.0,.06,min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y)));
    color = vec4(image.rgb * image.a * .9 * visibility * border, 0);
}
)";
    program = compileShaderProgram(vertex, fragment);
    int linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        Release();
        return false;
    }
    struct Vertex {glm::vec3 high,low,normal,major;glm::vec4 properties;};
    std::vector<Vertex> vertices;vertices.reserve((3+DistantGalaxyCount)*6);
    const auto append=[&](const glm::dvec3& center,double radius,const glm::dmat3& rotation,int variant,float magnitude){
        const glm::vec3 high(center),low(center-glm::dvec3(high));
        const Vertex v{high,low,glm::vec3(rotation[1]),glm::vec3(rotation[0]),
            {static_cast<float>(radius),variant==2?.65f:.13f,static_cast<float>(variants?variant:0),magnitude}};
        for(int i=0;i<6;++i)vertices.push_back(v);
    };
    for(int i=0;i<3;++i){const auto group=Universe::GalaxyAt(i+1);append(group.centerAu,group.axesAu.x,group.rotation,i,-21.4f);}
    // Fixed seeded far-field sample. Upload once: no per-frame CPU galaxy
    // generation, movement, sorting, or gravitational integration.
    std::mt19937_64 rng(Universe::WorldSeed^0x67616c617879ULL);
    const auto unit=[&](){return static_cast<double>(rng()>>11)*0x1.0p-53;};
    for(int i=0;i<DistantGalaxyCount;++i){
        const double z=unit()*2-1,angle=unit()*glm::two_pi<double>();
        const double distance=1.8e6*std::pow(80.0/1.8,unit())*Astronomy::auPerLightYear;
        const glm::dvec3 direction(std::sqrt(1-z*z)*std::cos(angle),z,std::sqrt(1-z*z)*std::sin(angle));
        const double radius=(18000+unit()*62000)*Astronomy::auPerLightYear;
        auto rotation=glm::rotate(glm::dmat4(1),unit()*glm::two_pi<double>(),glm::dvec3(0,1,0));
        rotation=glm::rotate(rotation,std::acos(unit()*2-1),glm::dvec3(1,0,0));
        append(MilkyWayModel::CenterAu()+direction*distance,radius,glm::dmat3(rotation),i%3,
            static_cast<float>(-20.5-5*std::log10(radius/(50000*Astronomy::auPerLightYear))));
    }
    vertexCount=static_cast<int>(vertices.size());
    glGenVertexArrays(1, &vao);glGenBuffers(1,&buffer);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
    const size_t offsets[]={offsetof(Vertex,high),offsetof(Vertex,low),offsetof(Vertex,normal),offsetof(Vertex,major),offsetof(Vertex,properties)};
    for(unsigned i=0;i<5;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==4?4:3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsets[i]));}
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    return true;
}

void GalaxyBackground::Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition,float magnitudeLimit) const {
    if (!program || !texture)
        return;
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glUniform1i(glGetUniformLocation(program, "galaxy"), 0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,variants?variants:texture);
    glUniform1i(glGetUniformLocation(program,"galaxyVariants"),1);glActiveTexture(GL_TEXTURE0);
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    const glm::vec3 high(cameraPosition),low(cameraPosition-glm::dvec3(high));
    glUniform3fv(glGetUniformLocation(program,"cameraHigh"),1,glm::value_ptr(high));
    glUniform3fv(glGetUniformLocation(program,"cameraLow"),1,glm::value_ptr(low));
    glUniform1f(glGetUniformLocation(program,"magnitudeLimit"),magnitudeLimit);
    GLboolean depthMask;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    const bool culling=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
    GLint src,dst,srcAlpha,dstAlpha;glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
    glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
    const bool blending=glIsEnabled(GL_BLEND)==GL_TRUE;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES,0,vertexCount);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFuncSeparate(src,dst,srcAlpha,dstAlpha);
    glDepthMask(depthMask);if(culling)glEnable(GL_CULL_FACE);
    if(!blending)glDisable(GL_BLEND);
}

void GalaxyBackground::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if(buffer)glDeleteBuffers(1,&buffer);
    if (texture) glDeleteTextures(1, &texture);
    if (variants) glDeleteTextures(1,&variants);
    program = vao = buffer = texture = variants = 0;vertexCount=0;
}
