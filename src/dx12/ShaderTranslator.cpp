#include "ShaderTranslator.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
char *Copy(const std::string &text) {
  auto *output = new char[text.size() + 1];
  std::memcpy(output, text.c_str(), text.size() + 1);
  return output;
}
void Parse(glslang::TShader &shader, const char *text) {
  shader.setStrings(&text, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, shader.getStage(),
                     glslang::EShClientOpenGL, 330);
  shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  shader.setAutoMapBindings(true);
  shader.setAutoMapLocations(true);
  if (!shader.parse(GetDefaultResources(), 330, false, EShMsgSpvRules))
    throw std::runtime_error(shader.getInfoLog());
}
std::string Hlsl(glslang::TIntermediate &stage) {
  std::vector<unsigned int> code;
  glslang::SpvOptions options;
  options.disableOptimizer = true;
  glslang::GlslangToSpv(stage, code, &options);
  spirv_cross::CompilerHLSL compiler(std::move(code));
  const auto resources = compiler.get_shader_resources();
  for (const auto &uniform : resources.gl_plain_uniforms)
    compiler.set_name(uniform.id, "u_" + uniform.name);
  unsigned binding = 0;
  for (const auto &image : resources.sampled_images) {
    compiler.set_name(image.id, "tex_" + image.name);
    compiler.set_decoration(image.id, spv::DecorationBinding, binding++);
  }
  auto common = compiler.get_common_options();
  common.vertex.fixup_clipspace = true;
  common.vertex.flip_vert_y = true;
  compiler.set_common_options(common);
  auto hlsl = compiler.get_hlsl_options();
  hlsl.shader_model = 51;
  hlsl.point_size_compat = true;
  hlsl.point_coord_compat = true;
  compiler.set_hlsl_options(hlsl);
  auto source = compiler.compile();
  if (compiler.get_execution_model() == spv::ExecutionModelFragment) {
    // D3D12 matches signature registers as well as semantics. The GLSL
    // translator emits FragCoord before user inputs, whereas the vertex
    // signature emits Position after its interpolants. Keep their order.
    const auto begin = source.find("struct SPIRV_Cross_Input"),
               end = source.find("};", begin);
    if (begin != std::string::npos && end != std::string::npos) {
      const auto semantic = source.find(": SV_Position", begin);
      if (semantic < end) {
        const auto line = source.rfind('\n', semantic) + 1,
                   finish = source.find('\n', semantic) + 1;
        const auto declaration = source.substr(line, finish - line);
        source.erase(line, finish - line);
        source.insert(source.find("};", begin), declaration);
      }
    }
  }
  return source;
}
} // namespace
int TranslateSceneShaders(const char *vertex, const char *fragment,
                          TranslatedProgram *result) {
  if (!result)
    return 0;
  *result = {};
  try {
    static std::once_flag initialize;
    std::call_once(initialize, [] {
      if (!glslang::InitializeProcess())
        throw std::runtime_error("Cannot initialize GLSL compiler");
    });
    // glslang's process state is shared by its workers; compilation is
    // startup/cache work, so keep this API explicitly serialized.
    static std::mutex compileMutex;
    std::lock_guard lock(compileMutex);
    glslang::TShader vs(EShLangVertex), ps(EShLangFragment);
    Parse(vs, vertex);
    Parse(ps, fragment);
    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&ps);
    if (!program.link(EShMsgSpvRules) || !program.mapIO())
      throw std::runtime_error(program.getInfoLog());
    result->vertex = Copy(Hlsl(*program.getIntermediate(EShLangVertex)));
    result->fragment = Copy(Hlsl(*program.getIntermediate(EShLangFragment)));
    return 1;
  } catch (const std::exception &error) {
    result->error = Copy(error.what());
    return 0;
  }
}
void FreeTranslatedProgram(TranslatedProgram *result) {
  if (!result)
    return;
  delete[] result->vertex;
  delete[] result->fragment;
  delete[] result->error;
  *result = {};
}
