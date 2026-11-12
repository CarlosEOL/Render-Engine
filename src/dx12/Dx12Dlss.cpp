#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include "Dx12Dlss.h"
#include <filesystem>
#include <iostream>
#include <sstream>
#ifdef RENDER_ENGINE_DLSS
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>
#endif

namespace NativeDX12 {
struct Dlss::Impl {
  ID3D12Device* device = nullptr;
  bool initialized = false, available = false;
  DlssMode mode = DlssMode::Off;
  glm::uvec2 input{}, output{};
  std::string status = "DLSS not built";
  unsigned long long evaluations = 0, resets = 0;
#ifdef RENDER_ENGINE_DLSS
  NVSDK_NGX_Parameter* parameters = nullptr;
  NVSDK_NGX_Handle* feature = nullptr;
  void Fail(const char* operation, NVSDK_NGX_Result result) {
    std::ostringstream text;
    text << "DLSS " << operation << " failed (0x" << std::hex << unsigned(result) << ")";
    status = text.str();
    std::cerr << status << "; using native rendering\n";
    available = false;
  }
#endif
};
Dlss::Dlss() : impl(std::make_unique<Impl>()) {}
Dlss::~Dlss() {
  ReleaseFeature();
#ifdef RENDER_ENGINE_DLSS
  if (impl->parameters) NVSDK_NGX_D3D12_DestroyParameters(impl->parameters);
  if (impl->initialized) NVSDK_NGX_D3D12_Shutdown1(impl->device);
#endif
}
void Dlss::Initialize(ID3D12Device* device) {
  impl->device = device;
#ifdef RENDER_ENGINE_DLSS
  wchar_t executable[MAX_PATH]{};
  GetModuleFileNameW(nullptr, executable, MAX_PATH);
  const auto directory = std::filesystem::path(executable).parent_path();
  if (!std::filesystem::exists(directory / "nvngx_dlss.dll")) {
    impl->status = "DLSS unavailable: nvngx_dlss.dll missing";
    return;
  }
  // A custom engine GUID is supported by NGX; this is not a borrowed NVIDIA app ID.
  const auto cache = directory / "ngx-cache";
  std::error_code error;
  std::filesystem::create_directories(cache, error);
  const wchar_t* paths[] = { directory.c_str() };
  NVSDK_NGX_FeatureCommonInfo info{};
  info.PathListInfo.Path = paths;
  info.PathListInfo.Length = 1;
  auto result = NVSDK_NGX_D3D12_Init_with_ProjectID(
      "406e86ea-bb6e-471a-9312-166737eafa94", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
      "Render-Engine 1", cache.c_str(), device, &info);
  if (NVSDK_NGX_FAILED(result)) { impl->Fail("initialization", result); return; }
  impl->initialized = true;
  result = NVSDK_NGX_D3D12_GetCapabilityParameters(&impl->parameters);
  if (NVSDK_NGX_FAILED(result)) { impl->Fail("capability query", result); return; }
  int supported = 0;
  result = impl->parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &supported);
  impl->available = NVSDK_NGX_SUCCEED(result) && supported != 0;
  impl->status = impl->available ? "DLSS ready" : "DLSS unavailable on this adapter/driver";
  std::cout << impl->status << '\n';
#endif
}
void Dlss::ReleaseFeature() {
#ifdef RENDER_ENGINE_DLSS
  if (impl->feature) NVSDK_NGX_D3D12_ReleaseFeature(impl->feature);
  impl->feature = nullptr;
#endif
  impl->mode = DlssMode::Off;
}
bool Dlss::Prepare(ID3D12GraphicsCommandList* commands, DlssMode mode,
                   unsigned width, unsigned height) {
#ifdef RENDER_ENGINE_DLSS
  if (!impl->available || mode == DlssMode::Off) return false;
  if (impl->feature && impl->mode == mode && impl->output == glm::uvec2(width,height)) return true;
  // The bridge retires previous GPU work before requesting a new feature.
  ReleaseFeature();
  const auto quality = mode == DlssMode::Quality ? NVSDK_NGX_PerfQuality_Value_MaxQuality
      : mode == DlssMode::Balanced ? NVSDK_NGX_PerfQuality_Value_Balanced
      : mode == DlssMode::Performance ? NVSDK_NGX_PerfQuality_Value_MaxPerf
      : NVSDK_NGX_PerfQuality_Value_DLAA;
  unsigned w=0,h=0,maxW=0,maxH=0,minW=0,minH=0;
  float sharpness=0;
  auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(impl->parameters,width,height,quality,
      &w,&h,&maxW,&maxH,&minW,&minH,&sharpness);
  if (NVSDK_NGX_FAILED(result) || !w || !h) {
    impl->Fail("optimal-size query", result); return false;
  }
  NVSDK_NGX_DLSS_Create_Params create{};
  create.Feature = {w,h,width,height,quality};
  create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
      NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
  result = NGX_D3D12_CREATE_DLSS_EXT(commands,1,1,&impl->feature,impl->parameters,&create);
  if (NVSDK_NGX_FAILED(result)) { impl->Fail("feature creation", result); return false; }
  impl->mode=mode; impl->input={w,h}; impl->output={width,height};
  impl->status = "DLSS active " + std::to_string(w) + "x" + std::to_string(h) + " -> " +
      std::to_string(width) + "x" + std::to_string(height);
  std::cout << impl->status << '\n';
  return true;
#else
  return false;
#endif
}
bool Dlss::Evaluate(ID3D12GraphicsCommandList* commands, ID3D12Resource* color,
                    ID3D12Resource* depth, ID3D12Resource* motion,
                    ID3D12Resource* output, glm::vec2 jitter, bool reset, float dt) {
#ifdef RENDER_ENGINE_DLSS
  if (!impl->feature || !impl->available) return false;
  NVSDK_NGX_D3D12_DLSS_Eval_Params evaluate{};
  evaluate.Feature.pInColor=color; evaluate.Feature.pInOutput=output;
  evaluate.pInDepth=depth; evaluate.pInMotionVectors=motion;
  evaluate.InJitterOffsetX=jitter.x; evaluate.InJitterOffsetY=jitter.y;
  evaluate.InRenderSubrectDimensions={impl->input.x,impl->input.y};
  evaluate.InMVScaleX=evaluate.InMVScaleY=1;
  evaluate.InPreExposure=evaluate.InExposureScale=1;
  evaluate.InFrameTimeDeltaInMsec=dt*1000;
  evaluate.InReset=reset;
  const auto result=NGX_D3D12_EVALUATE_DLSS_EXT(commands,impl->feature,impl->parameters,&evaluate);
  if (NVSDK_NGX_FAILED(result)) { impl->Fail("evaluation", result); return false; }
  ++impl->evaluations; if(reset)++impl->resets;
  return true;
#else
  return false;
#endif
}
bool Dlss::Available() const { return impl->available; }
glm::uvec2 Dlss::InputSize() const { return impl->input; }
const std::string& Dlss::Status() const { return impl->status; }
unsigned long long Dlss::Evaluations() const { return impl->evaluations; }
unsigned long long Dlss::Resets() const { return impl->resets; }
} // namespace NativeDX12
