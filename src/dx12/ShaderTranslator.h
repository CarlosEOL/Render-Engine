#pragma once

// C ABI isolates the compiler's Release STL/runtime from the engine's Debug
// configuration. All returned memory is owned/freed by the translator DLL.
struct TranslatedProgram {
  char *vertex = nullptr;
  char *fragment = nullptr;
  char *error = nullptr;
};
#ifdef SHADER_TRANSLATOR_EXPORTS
#define SHADER_TRANSLATOR_API __declspec(dllexport)
#else
#define SHADER_TRANSLATOR_API __declspec(dllimport)
#endif
extern "C" SHADER_TRANSLATOR_API int
TranslateSceneShaders(const char *vertex, const char *fragment,
                      TranslatedProgram *result);
extern "C" SHADER_TRANSLATOR_API void
FreeTranslatedProgram(TranslatedProgram *result);
