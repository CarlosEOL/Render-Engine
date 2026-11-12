cmake_minimum_required(VERSION 3.22)
project(RenderEngineShaderTranslator LANGUAGES CXX)
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(EXTERNAL_ROOT "${ENGINE_ROOT}/../External_Lib" ABSOLUTE)
add_library(ShaderTranslator SHARED "${ENGINE_ROOT}/src/dx12/ShaderTranslator.cpp")
target_compile_features(ShaderTranslator PRIVATE cxx_std_20)
target_compile_definitions(ShaderTranslator PRIVATE SHADER_TRANSLATOR_EXPORTS)
target_include_directories(ShaderTranslator PRIVATE "${EXTERNAL_ROOT}/src/glslang" "${EXTERNAL_ROOT}/src/SPIRV-Cross")
target_link_libraries(ShaderTranslator PRIVATE
    "${EXTERNAL_ROOT}/build/glslang/glslang/Release/glslang.lib"
    "${EXTERNAL_ROOT}/build/glslang/glslang/Release/glslang-default-resource-limits.lib"
    "${EXTERNAL_ROOT}/build/SPIRV-Cross/Release/spirv-cross-hlsl.lib"
    "${EXTERNAL_ROOT}/build/SPIRV-Cross/Release/spirv-cross-glsl.lib"
    "${EXTERNAL_ROOT}/build/SPIRV-Cross/Release/spirv-cross-core.lib")
