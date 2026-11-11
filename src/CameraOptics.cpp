#include "CameraOptics.h"
#include "LensCalibration.h"
#include "glad/glad.h"
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <initializer_list>

unsigned int compileShaderProgram(const char *, const char *);

namespace
{
using LensCalibration::profiles;

float UndistortedRadius(int profile, float distorted)
{
    const auto &lens = profiles[profile];
    float radius = distorted;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        float value = radius;
        float derivative = 1.0f;
        if (lens.distortionModel == 1)
        {
            const float k = lens.distortion.x;
            value = radius * (1.0f - k + k * radius * radius);
            derivative = 1.0f - k + 3.0f * k * radius * radius;
        }
        else if (lens.distortionModel == 2)
        {
            const auto c = lens.distortion;
            const float d = 1.0f - c.x - c.y - c.z;
            value = radius * (((c.x * radius + c.y) * radius + c.z) * radius + d);
            derivative = ((4.0f * c.x * radius + 3.0f * c.y) * radius + 2.0f * c.z) * radius + d;
        }
        radius -= (value - distorted) / derivative;
    }
    return radius;
}
} // namespace

const char *CameraOptics::Name(int profile)
{
    return profiles[profile].name;
}

float CameraOptics::FieldOfView(int profile, float aspect)
{
    if (profile == 0)
        return 45.0f;
    if (profile == 1)
        return glm::degrees(2.0f * std::atan(std::tan(glm::radians(50.0f)) / aspect));
    // A 36 mm sensor width, vertically cropped to the viewport's aspect ratio.
    const auto &lens = profiles[profile];
    const float realFocal =
        lens.focalLength *
        (lens.distortionModel == 2 ? 1.0f - lens.distortion.x - lens.distortion.y - lens.distortion.z : 1.0f);
    return glm::degrees(2.0f * std::atan(18.0f / (realFocal * aspect)));
}

glm::vec2 CameraOptics::MapScreen(int profile, const glm::vec2 &screen, float aspect, float fieldOfView)
{
    const float zoom =
        std::tan(glm::radians(fieldOfView * 0.5f)) / std::tan(glm::radians(FieldOfView(profile, aspect) * 0.5f));
    const float radius = glm::length(screen * glm::vec2(1.5f, 1.5f / aspect)) * zoom;
    return radius > 0.00001f ? screen * (UndistortedRadius(profile, radius) / radius) : screen;
}

bool CameraOptics::Initialize()
{
    const char *vertex = R"(
#version 330 core
out vec2 screenPosition;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screenPosition = corner * 2.0 - 1.0;
    gl_Position = vec4(screenPosition, 0, 1);
}
)";
    const char *fragment = R"(
#version 330 core
in vec2 screenPosition;
out vec4 fragColor;
uniform sampler2D scene;
uniform sampler2D flareSources;
uniform float flareStrength;
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
    vec3 glow = max(textureLod(scene, uv, 2.0).rgb - 1.0, vec3(0)) * 0.04;
    for(int tap=0;tap<8;++tap) {
        float angle=float(tap)*.78539816;
        vec2 offset=vec2(cos(angle),sin(angle))*texel*12.0;
        glow+=max(textureLod(scene,uv+offset,4.0).rgb-.5,vec3(0))*.0035;
    }
    color += glow;
    // Flare is an illustrative optical profile. Only distortion/TCA/vignetting
    // use measured Lensfun coefficients.
    if(profile>0 && flareStrength>0) {
        for(int source=0;source<8;++source) {
            vec4 emitter=texelFetch(flareSources,ivec2(source*2,0),0);
            if(emitter.z<=0)continue;
            bool duplicate=false;
            for(int previous=0;previous<source;++previous)
                if(length(texelFetch(flareSources,ivec2(previous*2,0),0).xy-emitter.xy)<texel.y*5.0)duplicate=true;
            if(duplicate)continue;
            vec3 tint=texelFetch(flareSources,ivec2(source*2+1,0),0).rgb;
            vec2 p=(uv-emitter.xy)*vec2(texel.y/texel.x,1);
            float r=length(p);
            float sourceAngle=r>1e-7?atan(p.y,p.x):0.0;
            float energy=min(.3,log2(1+emitter.z)*.045)*flareStrength;
            float halo=.00004/(r*r+.00012)*exp(-r/.055);
            if(profile==1) {
                float irregular=.8+.2*cos(7*sourceAngle+sin(11*sourceAngle));
                color+=tint*energy*(halo+irregular*.00002/(r*r+.0003)*exp(-r/.09));
            }else{
                float blades=profile==2?8.0:profile==5?10.0:18.0;
                float angle=sourceAngle+float(profile)*.11;
                float spikes=pow(abs(cos(angle*blades*.5)),48.0);
                vec3 wavelength=vec3(1.18,1,.82);
                vec3 tail=exp(-r/(.075*wavelength))*.00007/(r*r+.00015);
                color+=tint*energy*(halo*.65+tail*spikes);
            }
        }
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
    if (!linked)
    {
        Release();
        return false;
    }
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &sceneTexture);
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &sceneColor);
    glGenRenderbuffers(1, &sceneDepth);
    const char *extract = R"(#version 330 core
out vec4 result;
uniform sampler2D scene;
float brightness(vec3 c){return max(c.r,max(c.g,c.b));}
void main(){
    int pixel=int(gl_FragCoord.x),tile=pixel/2;
    vec2 origin=vec2(tile%4,tile/4)/vec2(4,2);
    vec2 tileSize=1.0/vec2(4,2);
    vec2 dimensions=vec2(textureSize(scene,0));
    float level=max(0.0,floor(log2(max(dimensions.x/32.0,dimensions.y/16.0))));
    float best=-1;vec2 position=origin+tileSize*.5;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){
        vec2 candidate=origin+(vec2(x,y)+.5)*tileSize/8.0;
        float b=brightness(textureLod(scene,candidate,level).rgb);
        if(b>best){best=b;position=candidate;}
    }
    for(int step=7;step>=0;--step){
        if(float(step)>level)continue;
        vec2 spacing=exp2(float(step))/dimensions;
        vec2 center=(floor(position*dimensions/exp2(float(step+1)))+.5)*exp2(float(step+1))/dimensions;
        best=-1;
        for(int y=0;y<2;++y)for(int x=0;x<2;++x){
            vec2 candidate=center+(vec2(x,y)-.5)*spacing;
            float b=brightness(textureLod(scene,candidate,float(step)).rgb);
            if(b>best){best=b;position=candidate;}
        }
    }
    vec3 color=textureLod(scene,position,0).rgb;
    float value=brightness(color);
    float broad=brightness(textureLod(scene,position,3).rgb);
    float compact=smoothstep(2.0,8.0,value/max(broad,.00001))*smoothstep(1.25,2.0,value);
    if(pixel%2==0)result=vec4(position,value*compact,compact);
    else result=vec4(color/max(value,.00001),1);
})";
    flareProgram = compileShaderProgram(vertex, extract);
    glGetProgramiv(flareProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Release();
        return false;
    }
    glGenFramebuffers(1, &flareFramebuffer);
    glGenTextures(1, &flareTexture);
    glBindTexture(GL_TEXTURE_2D, flareTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 16, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, flareFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, flareTexture, 0);
    const bool flareComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!flareComplete)
    {
        Release();
        return false;
    }
    return true;
}

void CameraOptics::BeginFrame()
{
    frameActive = false;
    if (!program)
        return;
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    glActiveTexture(GL_TEXTURE0);
    if (width != viewport[2] || height != viewport[3])
    {
        width = viewport[2];
        height = viewport[3];
        for (const auto texture : {sceneTexture, sceneColor})
        {
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

void CameraOptics::Render(int profile, float fieldOfView, float exposureEv, bool hdrMode, float flareStrength,
                          bool reuseScene) const
{
    if (!program || !frameActive)
        return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    if (!reuseScene)
    {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (profile > 0 && flareStrength > 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, flareFramebuffer);
        glViewport(0, 0, 16, 1);
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_BLEND);
        glUseProgram(flareProgram);
        glUniform1i(glGetUniformLocation(flareProgram, "scene"), 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "scene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, flareTexture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program, "flareSources"), 1);
    glUniform1f(glGetUniformLocation(program, "flareStrength"), flareStrength);
    glUniform1i(glGetUniformLocation(program, "profile"), profile);
    const auto &lens = profiles[profile];
    glUniform1i(glGetUniformLocation(program, "distortionModel"), lens.distortionModel);
    glUniform3fv(glGetUniformLocation(program, "distortion"), 1, glm::value_ptr(lens.distortion));
    glUniform3fv(glGetUniformLocation(program, "redTca"), 1, glm::value_ptr(lens.redTca));
    glUniform3fv(glGetUniformLocation(program, "blueTca"), 1, glm::value_ptr(lens.blueTca));
    glUniform3fv(glGetUniformLocation(program, "vignetting"), 1, glm::value_ptr(lens.vignetting));
    const float aspect = static_cast<float>(width) / height;
    const float zoom =
        std::tan(glm::radians(fieldOfView * 0.5f)) / std::tan(glm::radians(FieldOfView(profile, aspect) * 0.5f));
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

void CameraOptics::Release()
{
    if (flareProgram)
        glDeleteProgram(flareProgram);
    if (flareFramebuffer)
        glDeleteFramebuffers(1, &flareFramebuffer);
    if (flareTexture)
        glDeleteTextures(1, &flareTexture);
    flareProgram = flareFramebuffer = flareTexture = 0;
    if (program)
        glDeleteProgram(program);
    if (vao)
        glDeleteVertexArrays(1, &vao);
    if (sceneTexture)
        glDeleteTextures(1, &sceneTexture);
    if (sceneColor)
        glDeleteTextures(1, &sceneColor);
    if (sceneDepth)
        glDeleteRenderbuffers(1, &sceneDepth);
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    program = vao = sceneTexture = sceneColor = sceneDepth = framebuffer = 0;
    frameActive = false;
    width = height = 0;
}
