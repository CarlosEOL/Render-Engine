#include "ExposureMeter.h"
#include "glad/glad.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char *, const char *);

bool ExposureMeter::Initialize() {
  const char *vertex = R"(#version 330 core
void main(){
    vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    gl_Position=vec4(p*2.0-1.0,0,1);
})";
  const char *fragment = R"(#version 330 core
uniform sampler2D scene;
uniform vec3 stellarHint;
uniform vec3 blackHoleHint;
uniform sampler2D history;
uniform bool initialize;
uniform float realSeconds;
layout(location=0) out vec4 state;
void main(){
    float logSum=0,weightSum=0,highlight=0;
    int hotCells=0;
    int diskHistogram[32];for(int i=0;i<32;++i)diskHistogram[i]=0;
    int diskSamples=0;
    vec2 dimensions=vec2(textureSize(scene,0));
    float level=max(0.0,ceil(log2(max(dimensions.x/64.0,dimensions.y/36.0))));
    ivec2 size=textureSize(scene,int(level));
    // Every coarse texel is metered once, with uniform spatial weighting.
    // Lens-flare sources, acceptance, radius and strength never enter this pass.
    for(int y=0;y<36;++y)for(int x=0;x<64;++x){
        if(x>=size.x || y>=size.y)continue;
        vec2 uv=(vec2(x,y)+.5)/vec2(size);
        vec3 c=textureLod(scene,uv,level).rgb;
        float luminance=dot(c,vec3(.2126,.7152,.0722));
        if(luminance>.0001){
            logSum+=log2(max(luminance,.0001));
            weightSum+=1.0;
        }
        float peak=max(c.r,max(c.g,c.b));
        highlight=max(highlight,peak);
        if(peak>8.0)++hotCells;
        float diskDistance=length((uv-blackHoleHint.xy)*vec2(dimensions.x/dimensions.y,1));
        if(blackHoleHint.z>0 && diskDistance<blackHoleHint.z && peak>.5 && c.g<max(c.r,c.b)*1.15){
            int bin=clamp(int(floor(log2(peak)))+16,0,31);++diskHistogram[bin];++diskSamples;
        }
    }
    float average=weightSum>0?exp2(logSum/weightSum):.0007;
    float target=log2(.22/max(average,.0001));
    // A few isolated hot cells may clip; a resolved luminous area is protected.
    if(hotCells<4)highlight=min(highlight,8.0);
    if(highlight>0)target=min(target,log2(.90/highlight));
    // A beamed disk can span many stops. Meter its resolved emitting area,
    // allowing a small hot core to clip instead of hiding the entire disk.
    if(diskSamples>=8){
        int accumulated=0;float percentile=.5;
        for(int i=0;i<32;++i){
            accumulated+=diskHistogram[i];
            if(accumulated*4>=diskSamples*3){percentile=exp2(float(i)-15.5);break;}
        }
        target=log2(.55/percentile);
    }
    if(stellarHint.z>0){
        float samples[16];int count=0;
        float aspect=dimensions.x/dimensions.y;
        for(int i=0;i<16;++i){
            float angle=(float(i)+.37)*6.2831853/16.0;
            float radius=stellarHint.z*(i%2==0?.48:.66);
            vec2 uv=stellarHint.xy+vec2(cos(angle)/aspect,sin(angle))*radius;
            if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))continue;
            vec3 c=textureLod(scene,uv,0).rgb;
            float peak=max(c.r,c.b);
            // Ignore green selection graphics and empty samples around the limb.
            if(peak>.02 && c.g<peak*1.15)samples[count++]=peak;
        }
        if(count>=6){
            // Trim hot active regions and dark spots; camera panning should not
            // change exposure because a detector picked a different pixel.
            for(int i=1;i<count;++i){
                float value=samples[i];int j=i-1;
                while(j>=0 && samples[j]>value){samples[j+1]=samples[j];--j;}
                samples[j+1]=value;
            }
            int trim=count/4;float surface=0;
            for(int i=trim;i<count-trim;++i)surface+=samples[i];
            surface/=float(count-2*trim);
            target=log2(.60/surface);
        }
    }
    target=clamp(target,-18.0,8.0);
    float previous=texelFetch(history,ivec2(0),0).r;
    float speed=target<previous?4.0:1.1;
    float adapted=initialize?target:mix(previous,target,1.0-exp(-realSeconds*speed));
    if(abs(adapted-target)<.001)adapted=target;
    state=vec4(adapted,target,highlight,average);
})";
  program = compileShaderProgram(vertex, fragment);
  int linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    Release();
    return false;
  }
  glGenVertexArrays(1, &vao);
  glGenFramebuffers(1, &framebuffer);
  glGenTextures(2, textures);
  const float zero[4] = {};
  for (const auto texture : textures) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT,
                 zero);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      Release();
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  valid = false;
  current = 0;
  return true;
}

void ExposureMeter::Update(unsigned int scene, const glm::vec3 &stellarHint,
                           float seconds,const glm::vec3& blackHoleHint) const {
  if (!program)
    return;
  int viewport[4], oldFramebuffer, oldProgram, oldVao, activeTexture,
      polygon[2] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFramebuffer);
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
  glGetIntegerv(GL_POLYGON_MODE, polygon);
  const bool depth = glIsEnabled(GL_DEPTH_TEST) != 0,
             blend = glIsEnabled(GL_BLEND) != 0;
  GLint oldTextures[3];
  for (int unit = 0; unit < 3; ++unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTextures[unit]);
  }
  const int next = 1 - current;
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         textures[next], 0);
  glViewport(0, 0, 1, 1);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glUseProgram(program);
  glUniform3fv(glGetUniformLocation(program,"blackHoleHint"),1,glm::value_ptr(blackHoleHint));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, scene);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, textures[current]);
  glUniform1i(glGetUniformLocation(program, "scene"), 0);
  glUniform3fv(glGetUniformLocation(program, "stellarHint"), 1,
               glm::value_ptr(stellarHint));
  glUniform1i(glGetUniformLocation(program, "history"), 2);
  glUniform1i(glGetUniformLocation(program, "initialize"), !valid);
  glUniform1f(glGetUniformLocation(program, "realSeconds"),
              std::clamp(seconds, 0.f, .25f));
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  current = next;
  valid = true;
  for (int unit = 0; unit < 3; ++unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, oldTextures[unit]);
  }
  glActiveTexture(activeTexture);
  glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
  glUseProgram(oldProgram);
  glBindVertexArray(oldVao);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
  if (depth)
    glEnable(GL_DEPTH_TEST);
  if (blend)
    glEnable(GL_BLEND);
}

void ExposureMeter::Release() {
  glDeleteProgram(program);
  glDeleteVertexArrays(1, &vao);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(2, textures);
  program = vao = framebuffer = textures[0] = textures[1] = 0;
  valid = false;
  current = 0;
}
