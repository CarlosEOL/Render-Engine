#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace NativeDX12 {
// Each float4 occupies one HLSL constant register. No GL projection
// conventions.
struct Frame {
  glm::vec4 camera{0, -28, 6, 0};
  glm::vec4 forward{0, 1, 0, 0};
  glm::vec4 right{1, 0, 0, 0};
  glm::vec4 up{0, 0, 1, 0};
  glm::vec4 metric{1.974, .8, 1.34, 8.5}; // reference Rs, spin, inner, outer
  glm::vec4 optics{
      .4663, 0, 0,
      0}; // tan(fov/2), manual EV, mode(0 HDR/1 PureAuto/2 PureManual), seconds
  glm::vec4 viewport{1280, 720, 1.0f / 60,
                     1};           // full dimensions, real dt, reset exposure
  glm::vec4 display{1, 160, 0, 0}; // disk visibility, ray budget, unused
  glm::vec4 padding[8]{}; // explicit 256-byte D3D12 constant-buffer stride
};
static_assert(sizeof(Frame) == 256);

class Renderer {
public:
  ~Renderer();
  void Initialize(HWND window, UINT width, UINT height, bool warp = false,
                  bool previewScene = true, bool validation = false);
  void Resize(UINT width, UINT height);
  void Draw(const Frame &frame, bool sceneDirty = true, bool vsync = true);
  void WaitIdle();
  std::vector<unsigned char> Capture(const std::filesystem::path &path);
  std::array<float, 4> ReadExposure();
  std::array<float, 4> ProbeKerr(const Frame &frame);
  bool SceneFinite();
  UINT64 SceneDrawCount() const { return sceneDraws; }
  unsigned ErrorCount() const;
  const std::string &AdapterName() const { return adapterName; }
  bool DebugLayer() const { return debugLayer; }
  UINT Width() const { return width; }
  UINT Height() const { return height; }

private:
  friend class SceneBridge;
  template <class T> using Com = Microsoft::WRL::ComPtr<T>;
  static constexpr UINT Count = 2;
  struct Slot {
    Com<ID3D12CommandAllocator> allocator;
    UINT64 fence = 0;
  };
  void CreateTargets();
  void CreatePipelines();
  void Begin();
  void Submit(bool present, bool vsync);
  void Wait(UINT64 value);
  D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index) const;
  D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu(UINT index) const;
  D3D12_GPU_DESCRIPTOR_HANDLE Srv(UINT index) const;
  void Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES from,
                  D3D12_RESOURCE_STATES to);
  std::vector<unsigned char> ReadTexture(ID3D12Resource *resource,
                                         D3D12_RESOURCE_STATES state,
                                         UINT &rowPitch);
  HWND window = nullptr;
  UINT width = 0, height = 0, frameIndex = 0, lastBuffer = 0, history = 0,
       rtvStride = 0, srvStride = 0;
  UINT64 nextFence = 1, sceneDraws = 0;
  HANDLE fenceEvent = nullptr;
  bool debugLayer = false, historyValid = false, sceneValid = false,
       previewEnabled = true;
  std::string adapterName;
  Com<ID3D12Device> device;
  Com<ID3D12InfoQueue> info;
  Com<ID3D12CommandQueue> queue;
  Com<IDXGISwapChain3> swapchain;
  Com<ID3D12GraphicsCommandList> commands;
  Com<ID3D12Fence> fence;
  std::array<Slot, Count> slots;
  std::array<Com<ID3D12Resource>, Count> buffers, exposure;
  Com<ID3D12Resource> scene, constants;
  unsigned char *constantData = nullptr;
  Com<ID3D12DescriptorHeap> rtvs, srvs;
  Com<ID3D12RootSignature> root;
  Com<ID3D12PipelineState> scenePipeline, meterPipeline, finalPipeline,
      probePipeline;
};
} // namespace NativeDX12
