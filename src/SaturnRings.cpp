#include "SaturnRings.h"
#include "Astronomy.h"
#include "glad/glad.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

unsigned int compileShaderProgram(const char *, const char *);

const char *SaturnRings::ShaderFunctions() {
  return R"(
float saturnRingDepth(float km){
    float tau=0.0;
    if(km>=66900.0 && km<74491.0)tau=.003;
    if(km>=74658.0 && km<92000.0)tau=.12;
    if(km>=92000.0 && km<117507.0)tau=1.6;
    if(km>=117507.0 && km<122340.0)tau=.025;
    if(km>=122340.0 && km<136780.0)tau=.65;
    if(abs(km-133584.0)<162.0 || abs(km-136505.0)<21.0)tau=.0005;
    float fine=.78+.13*sin(km*.041)+.06*sin(km*.117)+.03*sin(km*.79);
    float detailFilter=1.0-smoothstep(1.0,12.0,fwidth(km));
    tau*=mix(.85,fine,detailFilter)*(.9+.1*sin(km*.003));
    float fWidth=max(25.0,fwidth(km));
    tau+=.08*exp(-pow((km-140220.0)/fWidth,2.0))*25.0/fWidth;
    return tau;
}
)";
}

bool SaturnRings::Initialize() {
  const char *vertex = R"(#version 330 core
layout(location=0)in vec3 vertex;
uniform mat4 vp;
uniform mat3 orientation;
uniform vec3 center,particleOrigin,cellAnchor;
uniform bool particles;
out vec3 localKm,relativePosition,rockNormal;
out float variation;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
void main(){
    if(particles){
        vec2 grid=cellAnchor.xz/32.0+vec2(gl_InstanceID%32-16,gl_InstanceID/32-16);
        float h=hash(grid);
        vec3 offset=vec3(float(gl_InstanceID%32-16)*32.0+(h-.5)*24.0,(hash(grid+3.7)-.5)*8.0,
            float(gl_InstanceID/32-16)*32.0+(hash(grid+7.1)-.5)*24.0);
        float size=mix(.15,3.0,pow(hash(grid+13.1),4.0));
        relativePosition=particleOrigin+orientation*((offset+vertex*size)/149597870700.0);
        localKm=(cellAnchor+offset)/1000.0;
        rockNormal=orientation*normalize(vertex);variation=h;
    }else{
        localKm=vertex;relativePosition=center+orientation*(vertex*1000.0/149597870700.0);
        rockNormal=orientation*vec3(0,1,0);variation=.5;
    }
    gl_Position=vp*vec4(relativePosition,1);
}
)";
  const std::string fragment =
      std::string("#version 330 core\n") + ShaderFunctions() + R"(
in vec3 localKm,relativePosition,rockNormal;
in float variation;
uniform mat3 orientation;
uniform vec3 sunlight;
uniform float planetRadiusKm;
uniform bool particles;
out vec4 color;
void main(){
    float km=length(localKm.xz),tau=saturnRingDepth(km);
    if(tau<.0001)discard;
    vec3 light=normalize(transpose(orientation)*sunlight);
    float along=dot(localKm,light);
    float shadow=along<0.0 && dot(localKm,localKm)-along*along<planetRadiusKm*planetRadiusKm?.025:1.0;
    vec3 tint=mix(vec3(.40,.32,.22),vec3(.87,.82,.69),smoothstep(85000.0,123000.0,km));
    vec3 view=normalize(-relativePosition);
    if(particles){
        if(length(relativePosition)*149597870700.0>650.0 || variation>min(1.0,tau))discard;
        float diffuse=.1+.9*max(0.0,dot(normalize(rockNormal),sunlight));
        color=vec4(tint*(.7+.5*variation)*diffuse*shadow,1);
    }else{
        float angle=max(.015,abs(dot(normalize(rockNormal),view)));
        float opacity=1.0-exp(-tau/angle);
        float front=abs(dot(normalize(rockNormal),sunlight));
        float back=pow(max(0.0,dot(view,-sunlight)),6.0)*.3;
        color=vec4(tint*(.12+.88*sqrt(front)+back)*shadow,opacity);
    }
    gl_FragDepth=log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12);
}
)";
  program = compileShaderProgram(vertex, fragment.c_str());
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    Release();
    return false;
  }
  const auto upload = [](unsigned int &vao, unsigned int &buffer,
                         const std::vector<glm::vec3> &vertices) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &buffer);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3),
                 vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);
  };
  std::vector<glm::vec3> vertices;
  const auto point = [](int i, float radius) {
    double a = glm::two_pi<double>() * i / 1024;
    return glm::vec3(radius * std::cos(a), 0, radius * std::sin(a));
  };
  for (int i = 0; i < 1024; ++i) {
    auto a = point(i, 66900), b = point(i, 140500), c = point(i + 1, 140500),
         d = point(i + 1, 66900);
    vertices.insert(vertices.end(), {a, b, c, a, c, d});
  }
  count = static_cast<int>(vertices.size());
  upload(vao, buffer, vertices);
  vertices.clear();
  for (int y : {-1, 1})
    for (int i = 0; i < 4; ++i) {
      const auto a = point(i * 256, 1), b = point((i + 1) * 256, 1);
      vertices.insert(vertices.end(), {glm::vec3(0, y, 0), a, b});
    }
  rockCount = static_cast<int>(vertices.size());
  upload(rockVao, rockBuffer, vertices);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

void SaturnRings::Render(const glm::mat4 &vp, const glm::dvec3 &center,
                         const glm::dmat3 &frame, const glm::dvec3 &sunlight,
                         double radius, double gravity, double seconds) const {
  if (!program)
    return;
  const auto clip = vp * glm::vec4(glm::vec3(center), 1);
  if (clip.w <= 0 && glm::length(center) > 140500000 / Astronomy::metresPerAu)
    return;
  GLint oldProgram, oldVao, src, dst;
  GLboolean depthMask;
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetIntegerv(GL_BLEND_SRC_RGB, &src);
  glGetIntegerv(GL_BLEND_DST_RGB, &dst);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
  const bool blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
  glUseProgram(program);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUniformMatrix4fv(glGetUniformLocation(program, "vp"), 1, GL_FALSE,
                     glm::value_ptr(vp));
  auto orientation = glm::mat3(frame);
  glUniformMatrix3fv(glGetUniformLocation(program, "orientation"), 1, GL_FALSE,
                     glm::value_ptr(orientation));
  glUniform3fv(glGetUniformLocation(program, "center"), 1,
               glm::value_ptr(glm::vec3(center)));
  glUniform3fv(glGetUniformLocation(program, "sunlight"), 1,
               glm::value_ptr(glm::vec3(glm::normalize(sunlight))));
  glUniform1f(glGetUniformLocation(program, "planetRadiusKm"),
              static_cast<float>(radius * Astronomy::metresPerAu / 1000));
  glUniform1i(glGetUniformLocation(program, "particles"), false);
  glDepthMask(GL_FALSE);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, count);
  const auto localCamera =
      glm::transpose(frame) * (-center) * Astronomy::metresPerAu;
  const double r = glm::length(glm::dvec2(localCamera.x, localCamera.z));
  if (std::abs(localCamera.y) < 650 && r > 66900000 && r < 140500000) {
    // A bounded co-moving visual patch; all 1024 ice pieces are placed on GPU.
    const double phase = std::remainder(
        std::sqrt(gravity / std::pow(r / Astronomy::metresPerAu, 3)) * seconds,
        glm::two_pi<double>());
    const auto moving = frame * glm::dmat3(glm::rotate(glm::dmat4(1), phase,
                                                       glm::dvec3(0, 1, 0)));
    const auto cameraLocal =
        glm::transpose(moving) * (-center) * Astronomy::metresPerAu;
    const glm::dvec3 anchor(std::floor(cameraLocal.x / 32) * 32, 0,
                            std::floor(cameraLocal.z / 32) * 32);
    const auto origin =
        glm::vec3(center + moving * (anchor / Astronomy::metresPerAu));
    orientation = glm::mat3(moving);
    glUniformMatrix3fv(glGetUniformLocation(program, "orientation"), 1,
                       GL_FALSE, glm::value_ptr(orientation));
    glUniform3fv(glGetUniformLocation(program, "particleOrigin"), 1,
                 glm::value_ptr(origin));
    glUniform3fv(glGetUniformLocation(program, "cellAnchor"), 1,
                 glm::value_ptr(glm::vec3(anchor)));
    glUniform1i(glGetUniformLocation(program, "particles"), true);
    glDepthMask(GL_TRUE);
    glBindVertexArray(rockVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, rockCount, 1024);
  }
  glDepthMask(depthMask);
  glBlendFunc(src, dst);
  if (!blend)
    glDisable(GL_BLEND);
  if (cull)
    glEnable(GL_CULL_FACE);
  glBindVertexArray(oldVao);
  glUseProgram(oldProgram);
}
void SaturnRings::Release() {
  for (auto value : {vao, rockVao})
    if (value)
      glDeleteVertexArrays(1, &value);
  for (auto value : {buffer, rockBuffer})
    if (value)
      glDeleteBuffers(1, &value);
  if (program)
    glDeleteProgram(program);
  program = vao = buffer = rockVao = rockBuffer = 0;
  count = rockCount = 0;
}
