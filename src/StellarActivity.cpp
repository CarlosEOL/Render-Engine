#include "StellarActivity.h"
#include "AssetManager.h"
#include "OrbitalMechanics.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "glad/glad.h"
#include <cmath>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

unsigned int compileShaderProgram(const char *, const char *);

std::string StellarActivity::ShaderFunctions() {
  return R"(
uniform bool stellarEnabled;
uniform bool stellarWarmColor;
uniform float stellarTime;
uniform float stellarSeed;
uniform float stellarPulse;
float stellarHash(float n) { return fract(sin(n*127.1+stellarSeed*31.7)*43758.5453); }
vec3 stellarAnchor(float n) {
    // One limb-facing arcade makes the activity legible on the initial solar approach.
    if(n<.5) return normalize(vec3(.92,.12,.35));
    float z=stellarHash(n*3.0)*1.8-.9;
    float a=stellarHash(n*3.0+1.0)*6.2831853;
    return vec3(sqrt(1.0-z*z)*cos(a),z,sqrt(1.0-z*z)*sin(a));
}
float stellarEnvelope(float n) {
    float phase=fract(stellarTime/3600.0+stellarHash(n+17.0));
    float cycle=pow(max(sin(phase*3.14159265),0.0),3.0);
    return max(cycle,n<.5?stellarPulse:0.0);
}
float stellarCellHash(vec3 p) {
    uvec3 cell=uvec3(ivec3(p));
    uint h=cell.x*1597334677u ^ cell.y*3812015801u ^ cell.z*2798796415u ^ uint(stellarSeed)*1013904223u;
    h^=h>>16u;h*=2246822519u;h^=h>>13u;h*=3266489917u;h^=h>>16u;
    return float(h&16777215u)/16777215.0;
}
float stellarNoise(vec3 p) {
    vec3 cell=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(stellarCellHash(cell),stellarCellHash(cell+vec3(1,0,0)),f.x),
                   mix(stellarCellHash(cell+vec3(0,1,0)),stellarCellHash(cell+vec3(1,1,0)),f.x),f.y),
               mix(mix(stellarCellHash(cell+vec3(0,0,1)),stellarCellHash(cell+vec3(1,0,1)),f.x),
                   mix(stellarCellHash(cell+vec3(0,1,1)),stellarCellHash(cell+vec3(1,1,1)),f.x),f.y),f.z);
}
vec3 stellarSurface(vec3 radial, vec3 tint, float mu, float temperature) {
    // Optional artistic display grade; the physical temperature stays unchanged.
    float warmth=smoothstep(3800.0,5000.0,temperature)*(1.0-smoothstep(6500.0,8500.0,temperature));
    if(stellarWarmColor){
        // Artistic display calibration: cool stellar photospheres remain visibly
        // emissive instead of resembling dim, lit planetary rock at default EV.
        tint*=max(1.0,2.0/max(max(tint.r,tint.g),max(tint.b,.0001)));
        tint=mix(tint,max(tint.r,max(tint.g,tint.b))*vec3(1.0,.40,.055),warmth*.90);
    }
    if(!stellarEnabled) return tint;
    float phase=stellarTime*6.2831853/3600.0;
    vec3 drift=vec3(sin(phase),cos(phase),sin(phase*2.0));
    float grain=stellarNoise(radial*140.0+drift*2.0);
    float cells=stellarNoise(radial*35.0+drift*.5);
    vec3 result=tint*(.78+.18*grain+.04*cells)*(.3+.7*sqrt(max(mu,0.0)));
    for(int i=0;i<6;++i) {
        float d=1.0-dot(radial,stellarAnchor(float(i)));
        float event=stellarEnvelope(float(i));
        result*=1.0-.9*exp(-d*900.0)*event;
        result+=tint*.8*exp(-pow((d-.003)*500.0,2.0))*event;
    }
    return result;
}
)";
}

bool StellarActivity::Initialize() {
  const auto vertex =
      std::string("#version 330 core\n") + ShaderFunctions() + R"(
layout(location=0) in vec2 parameter;
uniform mat4 model;
uniform mat4 viewProjection;
uniform float temperature;
out vec3 relativePosition;
out vec3 plasmaNormal;
out float brightness;
out float strand;
out float halo;
vec3 curve(float u, vec3 anchor, vec3 tangent, float height, float span) {
    return normalize(anchor+tangent*((u*2.0-1.0)*span))
         *(1.002+height*sin(3.14159265*u)*(1.0+.025*sin(u*37.0)));
}
void main() {
    float n=floor(float(gl_InstanceID)/6.0);
    float filament=mod(float(gl_InstanceID),3.0)-1.0;
    halo=mod(float(gl_InstanceID),6.0)>2.5?1.0:0.0;
    vec3 anchor=stellarAnchor(n);
    vec3 reference=abs(anchor.y)<.9?vec3(0,1,0):vec3(1,0,0);
    vec3 tangent=normalize(cross(reference,anchor));
    vec3 binormal=cross(anchor,tangent);
    float angle=stellarHash(n+41.0)*6.2831853;
    tangent=tangent*cos(angle)+binormal*sin(angle);
    binormal=normalize(cross(anchor,tangent));
    float event=stellarEnvelope(n);
    float activity=mix(1.35,.65,smoothstep(3500.0,12000.0,temperature));
    float height=(.035+.12*stellarHash(n+8.0))*activity*(.45+event)
                +(n<.5?stellarPulse*.12:0.0)+filament*.007;
    float span=.06+.14*stellarHash(n+9.0)+filament*.006;
    float u=parameter.x;
    vec3 center=curve(u,anchor,tangent,height,span);
    vec3 along=normalize(curve(min(u+.001,1.001),anchor,tangent,height,span)
                       -curve(max(u-.001,-.001),anchor,tangent,height,span));
    vec3 side=normalize(cross(along,binormal));
    vec3 normal=side*cos(parameter.y)+binormal*sin(parameter.y);
    center+=binormal*filament*.006*sin(u*3.14159265);
    float width=(.002+.002*stellarHash(n+5.0))*(.5+event)*mix(1.0,4.0,halo);
    relativePosition=(model*vec4(center+normal*width,1)).xyz;
    plasmaNormal=normalize(mat3(model)*normal);
    brightness=(.12+event*1.4)*activity;
    strand=u;
    gl_Position=viewProjection*vec4(relativePosition,1);
}
)";
  const auto fragment = std::string("#version 330 core\n") + R"(
in vec3 relativePosition;
in vec3 plasmaNormal;
in float brightness;
in float strand;
in float halo;
uniform float temperature;
uniform float stellarTime;
out vec4 color;
void main() {
    gl_FragDepth=log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12);
    float facing=abs(dot(normalize(plasmaNormal),normalize(-relativePosition)));
    float threads=.6+.4*sin(strand*190.0-stellarTime*6.2831853*46.0/3600.0);
    // Enhanced emission-line-inspired palette, not a broadband blackbody spectrum.
    vec3 tint=mix(vec3(1.0,.12,.018),vec3(.28,.48,1.0),smoothstep(7500.0,18000.0,temperature));
    vec3 core=mix(tint,vec3(1.0,.75,.35),pow(facing,5.0)*.6);
    color=vec4(core*brightness*threads*3.0,mix(.20+.4*facing,.035+.04*facing,halo));
}
)";
  program = compileShaderProgram(vertex.c_str(), fragment.c_str());
  int linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    Release();
    return false;
  }
  std::vector<glm::vec2> mesh;
  for (int u = 0; u < 64; ++u)
    for (int v = 0; v < 8; ++v) {
      const glm::vec2 a(u / 64.f, v * 6.2831853f / 8),
          b((u + 1) / 64.f, v * 6.2831853f / 8);
      const glm::vec2 c((u + 1) / 64.f, (v + 1) * 6.2831853f / 8),
          d(u / 64.f, (v + 1) * 6.2831853f / 8);
      mesh.insert(mesh.end(), {a, b, c, a, c, d});
    }
  vertices = static_cast<int>(mesh.size());
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &buffer);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(glm::vec2), mesh.data(),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  const char* coronaVertex=R"(#version 330 core
uniform mat4 viewProjection;
uniform vec3 center;
uniform float radius;
out vec2 uv;
out vec3 relativePosition;
void main(){
 vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
 vec2 p=corners[gl_VertexID];uv=p*.5+.5;
 vec3 right=normalize(vec3(viewProjection[0][0],viewProjection[1][0],viewProjection[2][0]));
 vec3 up=normalize(vec3(viewProjection[0][1],viewProjection[1][1],viewProjection[2][1]));
 relativePosition=center+(right*p.x+up*p.y)*radius*5.12;
 gl_Position=viewProjection*vec4(relativePosition,1);
})";
  const auto coronaFragment=std::string("#version 330 core\nconst float horizonRadius=0.0; const vec3 observerBeta=vec3(0);\n")
      +OrbitalMechanics::RelativityShaderFunctions()+R"(
in vec2 uv;in vec3 relativePosition;out vec4 color;
uniform sampler2D stellarCorona;
uniform float temperature,seed;uniform bool warmColor;
void main(){
 vec2 p=uv*2.0-1.0;
 float angle=seed*2.39996323,c=cos(angle),s=sin(angle);
 vec2 q=mat2(c,-s,s,c)*p;
 float r=length(p)*5.12;
 if(r<1.005 || r>4.9)discard;
 vec4 image=texture(stellarCorona,q*.5+.5);
 float emission=dot(image.rgb,vec3(.2126,.7152,.0722))*image.a;
 emission*=smoothstep(1.005,1.10,r)*(1.0-smoothstep(3.8,4.9,r));
 if(emission<.0005)discard;
 vec3 tint=blackbody(temperature);tint/=max(max(tint.r,tint.g),max(tint.b,.0001));
 if(warmColor)tint=mix(tint,vec3(1.0,.78,.46),smoothstep(3800.0,5000.0,temperature)*(1.0-smoothstep(6500.0,8500.0,temperature)));
 // Coronal emission is decorative radiance, never a second lens-flare source.
 color=vec4(tint*emission*.32,0);
 gl_FragDepth=log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12);
})";
  coronaProgram=compileShaderProgram(coronaVertex,coronaFragment.c_str());
  glGetProgramiv(coronaProgram,GL_LINK_STATUS,&linked);
  std::filesystem::path path="assets/stellar/corona.png";
#ifdef _WIN32
  wchar_t executable[32768];const auto length=GetModuleFileNameW(nullptr,executable,32768);
  if(length && length<32768)path=std::filesystem::path(executable).parent_path()/path;
#endif
  coronaTexture=AssetManager::LoadTexture(path.string().c_str(),true);
  if(!linked || !coronaTexture){Release();return false;}
  glBindTexture(GL_TEXTURE_2D,coronaTexture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glGenVertexArrays(1,&coronaVao);
  return true;
}

void StellarActivity::BindCorona(unsigned target,int unit) const{
  glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,coronaTexture);
  glUniform1i(glGetUniformLocation(target,"stellarCorona"),unit);
}

void StellarActivity::RenderCorona(const glm::mat4& vp,const glm::mat4& model,float temperature,int seed) const{
  if(!coronaProgram || !coronaTexture)return;
  GLint oldProgram,oldVao,active,texture,src,dst,srcA,dstA;GLboolean mask;
  glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
  glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
  glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);
  glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);
  glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);
  const bool blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
  glUseProgram(coronaProgram);BindCorona(coronaProgram,0);
  glUniformMatrix4fv(glGetUniformLocation(coronaProgram,"viewProjection"),1,GL_FALSE,glm::value_ptr(vp));
  glUniform3fv(glGetUniformLocation(coronaProgram,"center"),1,glm::value_ptr(glm::vec3(model[3])));
  glUniform1f(glGetUniformLocation(coronaProgram,"radius"),glm::length(glm::vec3(model[0])));
  glUniform1f(glGetUniformLocation(coronaProgram,"temperature"),temperature);
  glUniform1i(glGetUniformLocation(coronaProgram,"warmColor"),warmColor);
  glUniform1f(glGetUniformLocation(coronaProgram,"seed"),static_cast<float>(seed%10000));
  glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ONE);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
  glBindVertexArray(coronaVao);glDrawArrays(GL_TRIANGLES,0,6);
  glDepthMask(mask);glBlendFuncSeparate(src,dst,srcA,dstA);if(!blend)glDisable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);
  glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(active);glBindVertexArray(oldVao);glUseProgram(oldProgram);
}

void StellarActivity::BindTime(unsigned int target, int seed,
                               double seconds) const {
  glUniform1i(glGetUniformLocation(target, "stellarEnabled"), enabled);
  glUniform1i(glGetUniformLocation(target, "stellarWarmColor"), warmColor);
  glUniform1f(glGetUniformLocation(target, "stellarSeed"),
              static_cast<float>(seed % 10000));
  glUniform1f(glGetUniformLocation(target, "stellarTime"),
              static_cast<float>(seconds - std::floor(seconds / 3600) * 3600));
  const double age = seconds - eruptionStart;
  glUniform1f(glGetUniformLocation(target, "stellarPulse"),
              age >= 0 ? static_cast<float>(std::exp(-age / 240)) : 0);
}

void StellarActivity::BindSurface(unsigned int target, int seed,
                                  double seconds) const {
  BindTime(target, seed, seconds);
}

void StellarActivity::Render(const glm::mat4 &vp, const glm::mat4 &model,
                             float temperature, int seed,
                             double seconds) const {
  if (!program)
    return;
  const float radius = glm::length(glm::vec3(model[0]));
  const auto center = glm::vec3(model[3]);
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  const float projectionScale =
      glm::length(glm::vec3(vp[0][1], vp[1][1], vp[2][1]));
  if (radius * projectionScale * viewport[3] /
          glm::max(glm::length(center), radius) <
      8)
    return;
  const auto clip = vp * glm::vec4(center, 1);
  if (clip.w < -radius * 1.7f)
    return;
  if(coronaEnabled)RenderCorona(vp,model,temperature,seed);
  if(!enabled)return;
  GLint oldProgram, oldVao, srcRgb, dstRgb, srcAlpha, dstAlpha;
  GLboolean depthMask;
  const bool blend = glIsEnabled(GL_BLEND) != 0,
             cull = glIsEnabled(GL_CULL_FACE) != 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  glUseProgram(program);
  BindTime(program, seed, seconds);
  glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1,
                     GL_FALSE, glm::value_ptr(vp));
  glUniformMatrix4fv(glGetUniformLocation(program, "model"), 1, GL_FALSE,
                     glm::value_ptr(model));
  glUniform1f(glGetUniformLocation(program, "temperature"), temperature);
  glEnable(GL_BLEND);
  // Alpha is optical-source eligibility in the HDR scene. Plasma adds visible
  // radiance but clears eligibility so bright strands cannot spawn lens flares.
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ZERO);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glBindVertexArray(vao);
  glDrawArraysInstanced(GL_TRIANGLES, 0, vertices, 72);
  glDepthMask(depthMask);
  glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
  if (!blend)
    glDisable(GL_BLEND);
  if (cull)
    glEnable(GL_CULL_FACE);
  glBindVertexArray(oldVao);
  glUseProgram(oldProgram);
}

void StellarActivity::Release() {
  glDeleteProgram(coronaProgram);glDeleteVertexArrays(1,&coronaVao);glDeleteTextures(1,&coronaTexture);
  coronaProgram=coronaVao=coronaTexture=0;
  glDeleteBuffers(1, &buffer);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  buffer = vao = program = 0;
  vertices = 0;
}
