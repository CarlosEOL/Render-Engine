#include "VehicleInterior.h"
#include "glad/glad.h"
#include <array>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

unsigned int compileShaderProgram(const char *, const char *);

bool VehicleInterior::Initialize() {
  const char *vs = R"(#version 330 core
layout(location=0)in vec3 position;
layout(location=1)in vec3 normal;
layout(location=2)in vec4 material;
layout(location=3)in vec2 uv;
uniform mat4 vp;uniform mat3 cabin;
out vec3 relativePosition,n;out vec4 paint;out vec2 tex;
void main(){relativePosition=cabin*position/149597870700.0;n=cabin*normal;paint=material;tex=uv;
    gl_Position=vp*vec4(relativePosition,1);}
)";
  const char *fs = R"(#version 330 core
in vec3 relativePosition,n;in vec4 paint;in vec2 tex;
uniform float fuel;
out vec4 color;
void main(){
    vec3 value=paint.rgb*(.35+.65*max(0.0,dot(normalize(n),normalize(vec3(.3,.8,.5)))));
    if(paint.a>.5){
        vec2 grid=fract(tex*vec2(14,7));
        float lines=step(.92,grid.x)+step(.93,grid.y);
        float bar=step(tex.x,fuel)*step(.72,tex.y)*step(tex.y,.84);
        float plot=exp(-abs(tex.y-(.40+.11*sin(tex.x*23.0)))*130.0)*step(tex.y,.7);
        value=vec3(.006,.018,.022)+vec3(.012,.065,.07)*lines+vec3(.02,.35,.25)*bar+vec3(.04,.23,.30)*plot;
    }
    color=vec4(value,0); // Cabin surfaces and displays do not seed camera star flares.
    gl_FragDepth=log2(1.0+length(relativePosition)*1e12)/log2(1.0+20000.0*1e12);
}
)";
  program = compileShaderProgram(vs, fs);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    Release();
    return false;
  }
  struct Vertex {
    glm::vec3 p, n;
    glm::vec4 material;
    glm::vec2 uv;
  };
  for (int type = 0; type < 2; ++type) {
    std::vector<Vertex> vertices;
    const auto box = [&](glm::vec3 center, glm::vec3 size, glm::vec3 color,
                         bool screen = false) {
      const glm::vec3 corners[] = {
          {-.5, -.5, -.5}, {.5, -.5, -.5}, {.5, .5, -.5}, {-.5, .5, -.5},
          {-.5, -.5, .5},  {.5, -.5, .5},  {.5, .5, .5},  {-.5, .5, .5}};
      const int faces[][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3},
                              {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
      const glm::vec2 uvs[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
      for (const auto &face : faces) {
        const auto normal =
            glm::normalize(glm::cross(corners[face[1]] - corners[face[0]],
                                      corners[face[2]] - corners[face[0]]));
        for (int index : {0, 1, 2, 0, 2, 3})
          vertices.push_back({center + corners[face[index]] * size, normal,
                              glm::vec4(color, screen ? 1 : 0), uvs[index]});
      }
    };
    const float half = type == 1 ? .95f : 1.6f;
    const glm::vec3 wall(.12, .14, .16), trim(.21, .23, .24),
        dark(.035, .045, .05);
    box({0, -1.05f, .4f}, {half * 2, .12f, 4}, wall);
    box({0, 1.25f, .5f}, {half * 2, .15f, 3.8f}, wall);
    box({0, 0, 2.25f}, {half * 2, 2.6f, .15f}, wall);
    for (int side : {-1, 1}) {
      box({side * half, 0, .9f}, {.10f, 2.5f, 2.8f}, wall);
      box({side * (half - .04f), -.65f, -.45f}, {.24f, .6f, 1.1f}, trim);
      box({side * (half - .12f), -.31f, -.55f}, {.25f, .025f, .45f}, dark);
    }
    box({0, -.7f, -1.30f}, {half * 2, .45f, .45f}, trim);
    box({0, -.45f, -1.53f}, {half * 2, .10f, .12f}, dark);
    for (int screen = 0; screen < 3; ++screen) {
      const float x = (screen - 1) * (type == 1 ? .54f : .86f);
      box({x, -.57f, -1.05f}, {type == 1 ? .45f : .72f, .30f, .035f}, dark,
          true);
      for (int button = 0; button < 4; ++button)
        box({x - .15f + button * .1f, -.79f, -1.03f}, {.035f, .025f, .03f},
            glm::vec3(.16, .25, .24));
    }
    // Open windows: thin mullions leave the real astronomical view visible.
    for (float x : {-1.f, 1.f})
      box({x * half * .96f, .43f, -1.5f}, {.055f, 1.55f, .09f}, trim);
    if (type == 1)
      box({0, .47f, -1.52f}, {.04f, 1.4f, .06f}, trim);
    else
      for (int side : {-1, 1})
        box({side * .70f, .48f, -1.52f}, {.04f, 1.4f, .06f}, trim);
    box({0, 1.18f, -1.5f}, {half * 2, .07f, .10f}, trim);
    box({0, -.22f, -1.5f}, {half * 2, .055f, .10f}, trim);
    box({0, -.25f, .85f}, {.65f, 1.1f, .3f}, dark);
    box({0, -.75f, .45f}, {.65f, .16f, .65f}, dark);
    count[type] = static_cast<int>(vertices.size());
    glGenVertexArrays(1, &vao[type]);
    glGenBuffers(1, &buffer[type]);
    glBindVertexArray(vao[type]);
    glBindBuffer(GL_ARRAY_BUFFER, buffer[type]);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                 vertices.data(), GL_STATIC_DRAW);
    const int sizes[] = {3, 3, 4, 2};
    const size_t offsets[] = {offsetof(Vertex, p), offsetof(Vertex, n),
                              offsetof(Vertex, material), offsetof(Vertex, uv)};
    for (int i = 0; i < 4; ++i) {
      glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void *>(offsets[i]));
      glEnableVertexAttribArray(i);
    }
  }
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}
void VehicleInterior::Render(const glm::mat4 &vp, const glm::mat3 &frame,
                             bool ranger, float fuel) const {
  if (!program)
    return;
  GLint oldProgram, oldVao;
  GLboolean oldDepthMask;
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
  const bool blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
             depth = glIsEnabled(GL_DEPTH_TEST);
  glUseProgram(program);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glUniformMatrix4fv(glGetUniformLocation(program, "vp"), 1, GL_FALSE,
                     glm::value_ptr(vp));
  glUniformMatrix3fv(glGetUniformLocation(program, "cabin"), 1, GL_FALSE,
                     glm::value_ptr(frame));
  glUniform1f(glGetUniformLocation(program, "fuel"), fuel);
  glBindVertexArray(vao[ranger ? 1 : 0]);
  glDrawArrays(GL_TRIANGLES, 0, count[ranger ? 1 : 0]);
  glBindVertexArray(oldVao);
  glUseProgram(oldProgram);
  glDepthMask(oldDepthMask);
  if (blend)
    glEnable(GL_BLEND);
  if (cull)
    glEnable(GL_CULL_FACE);
  if (!depth)
    glDisable(GL_DEPTH_TEST);
}
void VehicleInterior::Release() {
  for (auto &value : vao) {
    if (value)
      glDeleteVertexArrays(1, &value);
    value = 0;
  }
  for (auto &value : buffer) {
    if (value)
      glDeleteBuffers(1, &value);
    value = 0;
  }
  if (program)
    glDeleteProgram(program);
  program = 0;
}
