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
uniform bool cachedTarget;
uniform bool cameraMetering;
uniform float atmosphereMetering;
uniform float marineMetering;
uniform float realSeconds;
layout(location=0) out vec4 state;

float adapt(float previous,float target){
    float speed=target<previous?4.5:2.25;
    float result=mix(previous,target,1.0-exp(-realSeconds*speed));
    return abs(result-target)<.001?target:result;
}

void main(){
    vec4 previous=texelFetch(history,ivec2(0),0);
    if(cachedTarget){
        state=vec4(adapt(previous.r,previous.g),previous.gba);
        return;
    }
    float average=0.0,target=0.0;
    float hotLog=0.0,logSum=0.0,ambientLogSum=0.0;int hotCells=0;
    float marineHighlightLog=0.0,marineHighlightWeight=0.0,marineCoverage=0.0;
    int diskHistogram[32];float diskLog[32];
    for(int i=0;i<32;++i){diskHistogram[i]=0;diskLog[i]=0.0;}
    int diskSamples=0;
    ivec2 resolution=textureSize(scene,0);
    vec2 dimensions=vec2(resolution);
    if(cameraMetering){
        int level=max(0,int(ceil(log2(max(dimensions.x/64.0,dimensions.y/36.0)))));
        ivec2 size=textureSize(scene,level);
        // Meter every coarse texel once. Dark sky contributes to the same
        // average as the lit scene, independent of optical glare.
        for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x){
            vec3 color=texelFetch(scene,ivec2(x,y),level).rgb;
            float luminance=max(dot(color,vec3(.2126,.7152,.0722)),.000001);
            float peak=max(color.r,max(color.g,color.b));
            logSum+=log2(luminance);
            if(atmosphereMetering>0.0)ambientLogSum+=log2(min(luminance,.65));
            if(peak>8.0){
                hotLog+=log2(peak);
                ++hotCells;
            }
            if(marineMetering>0.0){
                float highlight=smoothstep(.55,1.25,peak);
                marineHighlightLog+=highlight*log2(max(peak,.55));
                marineHighlightWeight+=highlight;
            }
        }
        float count=float(size.x*size.y);
        marineCoverage=marineHighlightWeight/count;
        average=exp2(logSum/count);
        ambientLogSum/=count;
        target=log2(.22/max(average,.0001));
        if(hotCells>0){
            // Broad highlights protect themselves from clipping. A hot pixel
            // inside a bright region cannot set the exposure for that region.
            float influence=smoothstep(.004,.016,float(hotCells)/count);
            target=mix(target,min(target,log2(.90)-hotLog/float(hotCells)),influence);
        }
    }
    float diskWeight=0.0;
    if(blackHoleHint.z>0){
        vec2 radius=max(vec2(blackHoleHint.z*dimensions.y/dimensions.x,blackHoleHint.z)*1.5,2.0/dimensions);
        vec2 lower=clamp(blackHoleHint.xy-radius,0.0,1.0);
        vec2 upper=clamp(blackHoleHint.xy+radius,0.0,1.0);
        ivec2 first=ivec2(floor(lower*dimensions)),last=ivec2(ceil(upper*dimensions));
        ivec2 range=last-first,grid=min(range,ivec2(64));
        // A small source gets one vote per covered texel. For larger sources
        // use a bounded grid over their visible area.
        for(int y=0;y<grid.y;++y)for(int x=0;x<grid.x;++x){
            ivec2 pixel=first+ivec2((vec2(x,y)+.5)*vec2(range)/vec2(grid));
            vec4 sample=texelFetch(scene,pixel,0);
            float peak=max(sample.r,max(sample.g,sample.b));
            if(sample.a>2.001 && peak>.00001){
                float value=log2(peak);
                int bin=clamp(int(floor(value))+16,0,31);
                ++diskHistogram[bin];diskLog[bin]+=value;++diskSamples;
            }
        }
        // Release the disk's control of the camera smoothly as it leaves
        // the frame. A centered, distant disk still meters at full weight.
        vec2 occupied=min(radius*2.0,vec2(1.0));
        vec2 visible=(upper-lower)/occupied;
        diskWeight=smoothstep(.025,.48,visible.x*visible.y);
        float edge=min(min(blackHoleHint.x,1.0-blackHoleHint.x)/radius.x,
                       min(blackHoleHint.y,1.0-blackHoleHint.y)/radius.y);
        diskWeight*=mix(.35,1.0,smoothstep(-1.0,.25,edge));
    }
    float diskEv=0.0;
    if(diskSamples>0){
        int accumulated=0;float upperLog=0.0,medianLog=0.0;bool foundMedian=false;
        for(int i=0;i<32;++i){
            int count=diskHistogram[i];
            if(count==0)continue;
            accumulated+=count;
            float logPeak=diskLog[i]/float(count);
            if(!foundMedian && accumulated*2>=diskSamples){medianLog=logPeak;foundMedian=true;}
            if(accumulated*4>=diskSamples*3){upperLog=logPeak;break;}
        }
        diskEv=clamp(log2(.90)-upperLog,-18.0,8.0);
        float diskTarget=mix(log2(2.4)-medianLog,diskEv,marineMetering);
        target=mix(target,diskTarget,diskWeight);
    }
    if(cameraMetering && atmosphereMetering<1.0 && stellarHint.z*dimensions.y>=2.0){
        float samples[16];int count=0;
        float aspect=dimensions.x/dimensions.y;
        for(int i=0;i<16;++i){
            float angle=(float(i)+.37)*6.2831853/16.0;
            float radius=stellarHint.z*(i%2==0?.48:.66);
            vec2 uv=stellarHint.xy+vec2(cos(angle)/aspect,sin(angle))*radius;
            if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))continue;
            vec4 source=textureLod(scene,uv,0);
            vec3 c=source.rgb;
            float peak=max(c.r,c.b);
            // Ignore green selection graphics and empty samples around the limb.
            if(peak>.02 && source.a>=.99 && c.g<peak*1.15)samples[count++]=peak;
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
            // A resolved nearby continuum has a stable radiance calibration;
            // subpixel catalogue display PSFs must not override its exposure.
            target=min(log2(.22/max(average,.0001)),log2(.60/surface));
        }
    }
    if(marineHighlightWeight>0.0){
        // Reflected light and haze count even when the disk is out of frame.
        float highlightEv=log2(.76)-marineHighlightLog/marineHighlightWeight;
        float influence=marineMetering*smoothstep(.015,.10,marineCoverage);
        target=mix(target,min(target,highlightEv),influence);
    }
    target=clamp(target,-18.0,8.0);
    if(cameraMetering && atmosphereMetering>0.0){
        // Meter the sky and landscape together, capping the Sun's influence.
        // Limit the gain range so bright daylight and dark twilight keep
        // their character when automatic exposure is enabled.
        float ambientEv=clamp(log2(.22)-ambientLogSum,-.25,1.25);
        target=mix(target,ambientEv,atmosphereMetering);
    }
    float adapted=initialize?target:adapt(previous.r,target);
    state=vec4(adapted,target,diskEv,average);
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
                           float seconds,const glm::vec3& blackHoleHint,
                           bool reuseScene,bool cameraMetering,float atmosphereMetering,float marineMetering) const {
  if (!program)
    return;
  const bool initialize=!valid || lastCameraMetering!=cameraMetering;
  const bool cachedTarget=reuseScene && !initialize && lastAtmosphereMetering==atmosphereMetering &&
      lastMarineMetering==marineMetering;
  const float elapsed=std::clamp(seconds,0.f,.25f);
  // The maximum EV span has converged to the shader cutoff by this time.
  if(cachedTarget && (!cameraMetering || settlingSeconds>=5.f || elapsed==0))return;
  settlingSeconds=initialize?5.f:cachedTarget?settlingSeconds+elapsed:0.f;
  lastCameraMetering=cameraMetering;
  lastAtmosphereMetering=atmosphereMetering;
  lastMarineMetering=marineMetering;
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
  glUniform1i(glGetUniformLocation(program, "initialize"), initialize);
  glUniform1i(glGetUniformLocation(program, "cachedTarget"), cachedTarget);
  glUniform1i(glGetUniformLocation(program, "cameraMetering"), cameraMetering);
  glUniform1f(glGetUniformLocation(program, "atmosphereMetering"), atmosphereMetering);
  glUniform1f(glGetUniformLocation(program, "marineMetering"), marineMetering);
  glUniform1f(glGetUniformLocation(program, "realSeconds"), elapsed);
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
