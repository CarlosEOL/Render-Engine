#include "KerrWormhole.h"
#include "WormholePhysics.h"
#include "glad/glad.h"
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <algorithm>
#include <cmath>

unsigned int compileShaderProgram(const char*,const char*);

bool KerrWormhole::Initialize() {
    const char* vertex=R"(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)";
    const auto fragment=std::string(R"(#version 330 core
in vec2 uv;out vec4 color;
uniform sampler2D scene,sceneDepth;
uniform samplerCube sky;
uniform mat4 inverseVP,viewProjection;
uniform vec3 mouth;
uniform float radius;
uniform bool diagnostic;
)" )+WormholePhysics::RayShaderFunctions()+R"(
void main(){
    vec4 original=texture(scene,uv);color=original;
    vec4 unprojected=inverseVP*vec4(uv*2.0-1.0,1,1);
    // Camera-relative perspective: w can be zero at the far plane when the
    // near/far ratio rounds to an infinite projection. xyz still gives the ray.
    vec3 ray=normalize(unprojected.xyz);
    float along=dot(mouth,ray),impact=length(mouth-ray*along)/radius;
    if(along<=0.0 || impact>5.0)return;
    float foregroundDepth=texture(sceneDepth,uv).r;
    if(foregroundDepth<.9999999){
        vec4 p=inverseVP*vec4(uv*2.0-1.0,foregroundDepth*2.0-1.0,1);
        if(length(p.xyz/p.w)<max(0.0,along-radius*2.5))return;
    }
    // Ecliptic +Y is the metric's spin axis (+Z).
    vec3 camera=-mouth/radius,dir=ray;
    camera=camera.xzy;dir=dir.xzy;
    vec3 escaped;int side=whTrace(camera,dir,escaped);escaped=escaped.xzy;
    if(diagnostic){color=vec4(side<0?vec3(0,1,0):side>0?vec3(0,0,1):vec3(1,0,1),0);return;}
    if(side==0){color=vec4(0,0,0,0);return;}
    vec4 result;
    if(side<0){
        // Authored orientation of the other exterior, using the cached sky.
        // This is an optical view; camera teleportation is not implied.
        escaped=mat3(.36,0,.93295,0,1,0,-.93295,0,.36)*escaped;
        result=vec4(texture(sky,escaped).rgb,0);
    }else{
        vec4 projected=viewProjection*vec4(escaped,0);
        vec2 sampleUV=projected.xy/max(projected.w,.000001)*.5+.5;
        if(projected.w>0.0 && all(greaterThanEqual(sampleUV,vec2(0))) && all(lessThanEqual(sampleUV,vec2(1))))
            result=texture(scene,sampleUV);
        else result=vec4(texture(sky,escaped).rgb,0);
    }
    // Compact local metric patch: smoothly match to the host scene, not a
    // hard circular edge. The throat is nonemissive and adds no camera flare.
    color=mix(original,result,1.0-smoothstep(3.5,5.0,impact));
}
)";
    program=compileShaderProgram(vertex,fragment.c_str());
    int linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){Release();return false;}
    glGenVertexArrays(1,&vao);glGenTextures(1,&color);glGenTextures(1,&depth);
    return true;
}

void KerrWormhole::Render(const glm::mat4& vp,const glm::dvec3& center,double radiusAu,unsigned int skyCube) const {
    if(!enabled || !program || !skyCube || radiusAu<=0)return;
    int viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    const auto clip=vp*glm::vec4(glm::vec3(center),1);
    const float verticalScale=glm::length(glm::vec3(vp[0][1],vp[1][1],vp[2][1]));
    const float horizontalScale=glm::length(glm::vec3(vp[0][0],vp[1][0],vp[2][0]));
    if(clip.w<=0 || radiusAu/std::max(radiusAu,glm::length(center))*verticalScale*viewport[3]<.75)return;
    const float reach=static_cast<float>(radiusAu)*5*std::max(horizontalScale,verticalScale);
    if(std::abs(clip.x)>clip.w+reach || std::abs(clip.y)>clip.w+reach)return;
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);
    if(width!=viewport[2] || height!=viewport[3]){
        width=viewport[2];height=viewport[3];
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D,depth);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D,color);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],width,height);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,depth);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],width,height);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_CUBE_MAP,skyCube);
    glUseProgram(program);
    const auto inverse=glm::inverse(vp);const auto mouth=glm::vec3(center);
    glUniformMatrix4fv(glGetUniformLocation(program,"inverseVP"),1,GL_FALSE,glm::value_ptr(inverse));
    glUniformMatrix4fv(glGetUniformLocation(program,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    glUniform3fv(glGetUniformLocation(program,"mouth"),1,glm::value_ptr(mouth));
    glUniform1f(glGetUniformLocation(program,"radius"),static_cast<float>(radiusAu));
    glUniform2f(glGetUniformLocation(program,"metricParameters"),static_cast<float>(WormholePhysics::MassLength()),static_cast<float>(WormholePhysics::SpinLength()));
    glUniform1i(glGetUniformLocation(program,"diagnostic"),diagnostic);
    glUniform1i(glGetUniformLocation(program,"scene"),0);glUniform1i(glGetUniformLocation(program,"sceneDepth"),1);glUniform1i(glGetUniformLocation(program,"sky"),2);
    const bool blending=glIsEnabled(GL_BLEND),testing=glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindVertexArray(0);if(blending)glEnable(GL_BLEND);if(testing)glEnable(GL_DEPTH_TEST);glActiveTexture(GL_TEXTURE0);
}

void KerrWormhole::Release(){
    if(program)glDeleteProgram(program);if(vao)glDeleteVertexArrays(1,&vao);
    for(auto texture:{color,depth})if(texture)glDeleteTextures(1,&texture);
    program=vao=color=depth=0;width=height=0;
}
