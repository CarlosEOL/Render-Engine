#pragma once
#include "Dx12Dlss.h"

namespace NativeDX12 {
void ConfigureSceneBridge(bool validation, bool warp = false);
// Records the existing scene graph's render commands directly into DX12.
// No OpenGL context, driver, frame capture or interop copy is involved.
void InitializeSceneBridge(void *window);
void PresentScene();
void SetSceneSwapInterval(int interval);
void ReleaseSceneBridge();
unsigned SceneValidationErrors();
void SetDlssMode(DlssMode mode);
DlssMode GetDlssMode();
const char* DlssStatus();
// Returns the SDK's render size; unchanged scenes accumulate subpixel samples.
glm::uvec2 PrepareSceneReconstruction(unsigned width, unsigned height, bool unchanged);
bool ReconstructionNeedsFrame();
bool ResolveSceneReconstruction(unsigned color, unsigned depth, unsigned destination, float dt);
unsigned long long DlssEvaluationCount();
unsigned long long DlssResetCount();
} // namespace NativeDX12
