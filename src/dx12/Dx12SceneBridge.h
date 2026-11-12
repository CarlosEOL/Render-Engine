#pragma once

namespace NativeDX12 {
void ConfigureSceneBridge(bool validation, bool warp = false);
// Records the existing scene graph's render commands directly into DX12.
// No OpenGL context, driver, frame capture or interop copy is involved.
void InitializeSceneBridge(void *window);
void PresentScene();
void SetSceneSwapInterval(int interval);
void ReleaseSceneBridge();
unsigned SceneValidationErrors();
} // namespace NativeDX12
