#include "glad/glad.h"
#include "Atmosphere.h"
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

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
out vec4 fragColor;
uniform sampler2D scene;
uniform sampler2D sceneDepth;
uniform bool logarithmicDepth;
uniform mat4 inverseViewProjection;
uniform vec3 cameraPosition;
uniform vec3 planetCenter;
uniform vec3 lightPosition;
uniform float planetRadius;
uniform bool earthClouds;
uniform float cloudTime;
uniform mat3 cloudFrame;

float cloudHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float cloudNoise(vec3 p){
 vec3 b=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(mix(cloudHash(b),cloudHash(b+vec3(1,0,0)),f.x),mix(cloudHash(b+vec3(0,1,0)),cloudHash(b+vec3(1,1,0)),f.x),f.y),
 mix(mix(cloudHash(b+vec3(0,0,1)),cloudHash(b+vec3(1,0,1)),f.x),mix(cloudHash(b+vec3(0,1,1)),cloudHash(b+vec3(1,1,1)),f.x),f.y),f.z);
}
float weather(vec3 p){
 vec3 n=normalize(cloudFrame*p);n+=vec3(cloudTime*.0000007,0,cloudTime*.0000003);
 float field=.58*cloudNoise(n*11.0)+.28*cloudNoise(n*29.0+17.0)+.14*cloudNoise(n*73.0+31.0);
 return smoothstep(.43,.65,field);
}
float cloudDensity(vec3 p){
 float height=(length(p)-planetRadius)*149597870.7;
 if(height<2.0 || height>12.0)return 0.0;
 float coverage=weather(p);if(coverage<.03)return 0.0;
 float profile=smoothstep(2.0,3.2,height)*(1.0-smoothstep(7.0,12.0,height));
 vec3 local=cloudFrame*p*149597870.7;
 float erosion=.65*cloudNoise(local*.13+vec3(cloudTime*.00001,0,0))+.35*cloudNoise(local*.47);
 return max(0.0,coverage-erosion*.35)*profile*.75;
}

vec2 sphereInterval(vec3 origin, vec3 direction, float radius) {
    float along = dot(origin, direction);
    float discriminant = along * along - dot(origin, origin) + radius * radius;
    if (discriminant < 0.0) return vec2(1.0, -1.0);
    float root = sqrt(discriminant);
    return vec2(-along - root, -along + root);
}

void main() {
    vec4 source = texture(scene, screenPosition * 0.5 + 0.5);
    vec3 background = source.rgb;
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
    vec2 ground = sphereInterval(origin, direction, planetRadius * (earthClouds?1.0:1.0035));
    if (ground.x > 0.0 && ground.y >= ground.x)
        finish = min(finish, ground.x);
    if (finish <= start) {
        fragColor = vec4(background, source.a);
        return;
    }
    float sourceEligibility=source.a;
    if(earthClouds){
        // A shell approximation and a bounded volume share weather and optical
        // depth. Fade by altitude; never replace the weather map on descent.
        float altitudeKm=(length(origin)-planetRadius)*149597870.7;
        float volumeWeight=1.0-smoothstep(60.0,200.0,altitudeKm);
        vec2 cloudShell=sphereInterval(origin,direction,planetRadius+12000.0/149597870700.0);
        vec2 cloudBase=sphereInterval(origin,direction,planetRadius+2000.0/149597870700.0);
        float cloudBegin=max(0.0,cloudShell.x),cloudEnd=min(finish,cloudShell.y);
        if(cloudBase.x>cloudBegin)cloudEnd=min(cloudEnd,cloudBase.x);
        // From below the clouds, skip the empty cavity before entering the base.
        if(length(origin)<planetRadius+2000.0/149597870700.0)cloudBegin=max(cloudBegin,cloudBase.y);
        vec2 sheet=sphereInterval(origin,direction,planetRadius+5500.0/149597870700.0);
        float sheetDistance=sheet.x>0.0?sheet.x:sheet.y;
        float sheetT=1.0;vec3 sheetLight=vec3(0);
        if(sheetDistance>=start && sheetDistance<finish){
            vec3 p=origin+direction*sheetDistance;
            float grazing=max(.10,abs(dot(normalize(p),direction)));
            sheetT=exp(-weather(p)*2.8/grazing);
            vec3 sun=normalize(lightPosition-planetCenter-p);
            float illumination=smoothstep(-.12,.12,dot(normalize(p),sun));
            sheetLight=vec3(.83,.87,.94)*(.025+.975*illumination)*(1.0-sheetT);
        }
        vec3 volumeLight=vec3(0);float volumeT=1.0;
        if(volumeWeight>0.0 && cloudEnd>cloudBegin){
            cloudEnd=min(cloudEnd,cloudBegin+200000.0/149597870700.0);
            float step=(cloudEnd-cloudBegin)/32.0;
            for(int i=0;i<32;++i){
                vec3 p=origin+direction*(cloudBegin+(float(i)+.5)*step);
                float density=cloudDensity(p);if(density<.001)continue;
                vec3 sun=normalize(lightPosition-planetCenter-p);
                float shadow=cloudDensity(p+sun*(1500.0/149597870700.0))*1.5
                    +cloudDensity(p+sun*(4500.0/149597870700.0))*3.0;
                float daylight=smoothstep(-.12,.10,dot(normalize(p),sun));
                float phase=.25+.65*pow(max(dot(direction,sun),0.0),8.0);
                vec3 lighting=vec3(.12,.17,.25)*(.08+.92*daylight)
                    +vec3(1.0,.96,.88)*daylight*exp(-shadow)*(.55+phase);
                float passing=exp(-density*step*149597870.7);
                volumeLight+=volumeT*(1.0-passing)*lighting;volumeT*=passing;
                if(volumeT<.01)break;
            }
        }
        float cloudT=mix(sheetT,volumeT,volumeWeight);
        background=background*cloudT+mix(sheetLight,volumeLight,volumeWeight);
        sourceEligibility*=cloudT;
    }
    const int samples = 12;
    float interval = (finish - start) / float(samples);
    // Earth-like Rayleigh and aerosol scale heights, scaled with body radius.
    // Light-path extinction produces a warm low Sun; Mie forward scattering
    // supplies the circumsolar aureole without introducing an optical emitter.
    vec3 betaRayleigh = vec3(37.0, 86.0, 210.0) / planetRadius;
    vec3 betaMie = vec3(22.0) / planetRadius;
    vec3 betaOzone = vec3(4.1, 12.0, .55) / planetRadius;
    vec3 transmittance = vec3(1);
    vec3 scattering = vec3(0);
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        vec3 samplePosition = origin + direction * (start + (float(sampleIndex) + 0.5) * interval);
        float altitude = max(length(samplePosition) - planetRadius, 0.0);
        float density = exp(-altitude / (planetRadius * 0.00125));
        float haze = exp(-altitude / (planetRadius * .000188));
        float ozone = max(0.0,1.0-abs(altitude/planetRadius-.00392)/.00235);
        vec3 sunDirection = normalize(lightPosition - planetCenter - samplePosition);
        vec2 shadow = sphereInterval(samplePosition, sunDirection, planetRadius);
        float illumination = shadow.x > 0.0 && shadow.y >= shadow.x ? 0.0 : 1.0;
        float cosine = dot(direction, sunDirection);
        float rayleighPhase = .75 * (1.0 + cosine * cosine);
        const float g=.76;
        float miePhase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*cosine,.001),1.5);
        float sunlightLength=max(0.0,sphereInterval(samplePosition,sunDirection,planetRadius*1.025).y);
        vec3 sunlightDepth=vec3(0);
        if(illumination>0.0){
            // Four quadratically spaced samples concentrate work near dense air.
            for(int lightSample=0;lightSample<4;++lightSample){
                float u=(float(lightSample)+.5)/4.0;
                float h=max(0.0,length(samplePosition+sunDirection*(sunlightLength*u*u))-planetRadius);
                float rd=exp(-h/(planetRadius*.00125)),md=exp(-h/(planetRadius*.000188));
                float od=max(0.0,1.0-abs(h/planetRadius-.00392)/.00235);
                sunlightDepth+=(betaRayleigh*rd+betaMie*md*1.1+betaOzone*od)*(sunlightLength*2.0*u/4.0);
            }
        }
        vec3 sunTransmission=exp(-sunlightDepth)*illumination;
        vec3 extinction=betaRayleigh*density+betaMie*haze*1.1+betaOzone*ozone;
        vec3 segment=exp(-extinction*interval);
        vec3 scatterCoefficient=betaRayleigh*density*rayleighPhase+betaMie*haze*miePhase;
        vec3 single=scatterCoefficient/max(extinction,vec3(1e-10))*sunTransmission*.30;
        // Bounded second-order approximation brightens the lower blue sky.
        vec3 multiple=vec3(.055,.095,.16)*(1.0-exp(-density*.6))*(.03+.97*illumination);
        scattering+=transmittance*(1.0-segment)*(single+multiple);
        transmittance *= segment;
    }
    fragColor = vec4(background * transmittance + scattering, sourceEligibility);
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
    glGenTextures(1, &depthTexture);
    return true;
}

void Atmosphere::Render(const glm::mat4& viewProjection, const glm::vec3& camera,
                        const glm::vec3& center, float radius, bool logarithmicDepth,
                        const glm::vec3& lightPosition,bool earthClouds,double seconds,const glm::mat3& rotation) const {
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    if(glm::length(center-camera)>radius*2) {
        const auto clip=viewProjection*glm::vec4(center,1);
        if(clip.w<=0)return;
        const float projectionScale=glm::length(glm::vec3(viewProjection[0][1],viewProjection[1][1],viewProjection[2][1]));
        const float screenRadius=radius*1.03f*projectionScale/clip.w;
        if(screenRadius*viewport[3]*.5f<1.0f)return;
        const auto ndc=glm::vec2(clip)/clip.w;
        if(std::abs(ndc.x)>1+screenRadius || std::abs(ndc.y)>1+screenRadius)return;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    if (textureWidth != viewport[2] || textureHeight != viewport[3]) {
        textureWidth = viewport[2];
        textureHeight = viewport[3];
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, textureWidth, textureHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, textureWidth, textureHeight, 0,
            GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], textureWidth, textureHeight);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], textureWidth, textureHeight);
    glUseProgram(program);
    const auto inverse = glm::inverse(viewProjection);
    glUniformMatrix4fv(glGetUniformLocation(program, "inverseViewProjection"), 1, GL_FALSE, glm::value_ptr(inverse));
    glUniform3fv(glGetUniformLocation(program, "cameraPosition"), 1, glm::value_ptr(camera));
    glUniform3fv(glGetUniformLocation(program, "planetCenter"), 1, glm::value_ptr(center));
    glUniform3fv(glGetUniformLocation(program, "lightPosition"), 1, glm::value_ptr(lightPosition));
    glUniform1f(glGetUniformLocation(program, "planetRadius"), radius);
    glUniform1i(glGetUniformLocation(program,"earthClouds"),earthClouds);
    glUniform1f(glGetUniformLocation(program,"cloudTime"),static_cast<float>(std::remainder(seconds,86400.0*100)));
    const auto cloudFrame=glm::transpose(rotation);
    glUniformMatrix3fv(glGetUniformLocation(program,"cloudFrame"),1,GL_FALSE,glm::value_ptr(cloudFrame));
    glUniform1i(glGetUniformLocation(program, "scene"), 0);
    glUniform1i(glGetUniformLocation(program, "sceneDepth"), 1);
    glUniform1i(glGetUniformLocation(program, "logarithmicDepth"), logarithmicDepth);
    const bool blending=glIsEnabled(GL_BLEND);glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);if(blending)glEnable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Atmosphere::Release() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (sceneTexture) glDeleteTextures(1, &sceneTexture);
    if (depthTexture) glDeleteTextures(1, &depthTexture);
    program = vao = sceneTexture = depthTexture = 0;
    textureWidth = textureHeight = 0;
}
