#include "Dx12Renderer.h"
#include "KerrSpacetime.h"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace NativeDX12 {
namespace {
void Check(HRESULT result, const char *action) {
  if (FAILED(result)) {
    std::ostringstream text;
    text << action << " (0x" << std::hex << static_cast<unsigned long>(result)
         << ")";
    throw std::runtime_error(text.str());
  }
}
std::string ReadShader(const wchar_t *name) {
  wchar_t executable[32768];
  const auto length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!length || length >= 32768)
    throw std::runtime_error("Cannot locate shader directory");
  const auto path =
      std::filesystem::path(executable).parent_path() / L"shaders" / name;
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Missing shader: " + path.string());
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}
Microsoft::WRL::ComPtr<ID3DBlob>
Compile(const std::string &text, const char *entry, const char *profile) {
  Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
  const auto result = D3DCompile(
      text.data(), text.size(), entry, nullptr, nullptr, entry, profile,
      D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
      &errors);
  if (FAILED(result))
    throw std::runtime_error(
        errors
            ? std::string(static_cast<const char *>(errors->GetBufferPointer()),
                          errors->GetBufferSize())
            : "Shader compilation failed");
  return code;
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES p{};
  p.Type = type;
  p.CreationNodeMask = p.VisibleNodeMask = 1;
  return p;
}
D3D12_RESOURCE_DESC Texture(UINT width, UINT height, DXGI_FORMAT format) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  d.Width = width;
  d.Height = height;
  d.DepthOrArraySize = d.MipLevels = 1;
  d.Format = format;
  d.SampleDesc.Count = 1;
  d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  return d;
}
D3D12_RESOURCE_DESC Buffer(UINT64 size) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = size;
  d.Height = 1;
  d.DepthOrArraySize = d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return d;
}
} // namespace
Renderer::~Renderer() {
  if (queue && fence && fenceEvent) {
    try {
      WaitIdle();
    } catch (...) {
    }
  }
  if (constants && constantData)
    constants->Unmap(0, nullptr);
  if (fenceEvent)
    CloseHandle(fenceEvent);
}
void Renderer::Initialize(HWND hwnd, UINT w, UINT h, bool warp,
                          bool previewScene, bool validation) {
  previewEnabled = previewScene;
  window = hwnd;
  width = std::max(1u, w);
  height = std::max(1u, h);
  Com<ID3D12Debug> debug;
  bool requestValidation = validation;
#ifdef _DEBUG
  requestValidation = true;
#endif
  if (requestValidation &&
      SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
    debugLayer = true;
  }
  Com<IDXGIFactory6> factory;
  Check(CreateDXGIFactory2(debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0,
                           IID_PPV_ARGS(&factory)),
        "Create DXGI factory");
  Com<IDXGIAdapter1> adapter;
  if (warp)
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),
          "Select WARP adapter");
  else {
    for (UINT i = 0;; ++i) {
      Com<IDXGIAdapter1> candidate;
      const auto result = factory->EnumAdapterByGpuPreference(
          i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
      if (result == DXGI_ERROR_NOT_FOUND)
        break;
      Check(result, "Enumerate adapter");
      DXGI_ADAPTER_DESC1 desc{};
      Check(candidate->GetDesc1(&desc), "Read adapter");
      if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
          SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                      __uuidof(ID3D12Device), nullptr))) {
        adapter = candidate;
        break;
      }
    }
    if (!adapter)
      throw std::runtime_error("No DX12 hardware adapter; use --warp "
                               "explicitly for software validation");
  }
  DXGI_ADAPTER_DESC1 desc{};
  Check(adapter->GetDesc1(&desc), "Read selected adapter");
  const int nameBytes = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                            nullptr, 0, nullptr, nullptr);
  std::vector<char> name(static_cast<size_t>(std::max(1, nameBytes)));
  if (nameBytes > 0)
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name.data(),
                        nameBytes, nullptr, nullptr);
  adapterName = name.data();
  Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                          IID_PPV_ARGS(&device)),
        "Create DX12 device");
  device.As(&info);
  D3D12_COMMAND_QUEUE_DESC q{};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "Create queue");
  DXGI_SWAP_CHAIN_DESC1 sc{};
  sc.Width = width;
  sc.Height = height;
  sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sc.SampleDesc.Count = 1;
  sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sc.BufferCount = Count;
  sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  Com<IDXGISwapChain1> chain;
  Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &sc, nullptr,
                                        nullptr, &chain),
        "Create swap chain");
  Check(chain.As(&swapchain), "Query swap chain");
  Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
        "Disable implicit fullscreen");
  for (auto &slot : slots)
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&slot.allocator)),
          "Create frame allocator");
  Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  slots[0].allocator.Get(), nullptr,
                                  IID_PPV_ARGS(&commands)),
        "Create command list");
  Check(commands->Close(), "Close initial command list");
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
        "Create frame fence");
  fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fenceEvent)
    throw std::runtime_error("Cannot create GPU fence event");
  D3D12_DESCRIPTOR_HEAP_DESC heap{};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap.NumDescriptors = 5;
  Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvs)),
        "Create RTV heap");
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap.NumDescriptors = 4;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvs)),
        "Create shader descriptor heap");
  rtvStride =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  srvStride = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (previewEnabled) {
    auto uploadHeap = Heap(D3D12_HEAP_TYPE_UPLOAD);
    auto buffer = Buffer(Count * sizeof(Frame));
    Check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
                                          &buffer,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&constants)),
          "Create per-frame constants");
    D3D12_RANGE empty{0, 0};
    Check(constants->Map(0, &empty, reinterpret_cast<void **>(&constantData)),
          "Map constants");
  }
  CreateTargets();
  if (previewEnabled)
    CreatePipelines();
  std::cout << "DX12 adapter: " << adapterName << "; debug layer=" << debugLayer
            << "\n";
}
D3D12_CPU_DESCRIPTOR_HANDLE Renderer::Rtv(UINT i) const {
  auto h = rtvs->GetCPUDescriptorHandleForHeapStart();
  h.ptr += static_cast<SIZE_T>(i) * rtvStride;
  return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE Renderer::SrvCpu(UINT i) const {
  auto h = srvs->GetCPUDescriptorHandleForHeapStart();
  h.ptr += static_cast<SIZE_T>(i) * srvStride;
  return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE Renderer::Srv(UINT i) const {
  auto h = srvs->GetGPUDescriptorHandleForHeapStart();
  h.ptr += static_cast<UINT64>(i) * srvStride;
  return h;
}
void Renderer::CreateTargets() {
  for (UINT i = 0; i < Count; ++i) {
    Check(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffers[i])),
          "Get back buffer");
    device->CreateRenderTargetView(buffers[i].Get(), nullptr, Rtv(i));
  }
  if (!previewEnabled)
    return;
  auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
  auto make = [&](Com<ID3D12Resource> &target, UINT w, UINT h,
                  DXGI_FORMAT format, UINT rtv) {
    auto description = Texture(w, h, format);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = format;
    Check(device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &description,
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
              IID_PPV_ARGS(&target)),
          "Create scene/exposure target");
    device->CreateRenderTargetView(target.Get(), nullptr, Rtv(rtv));
  };
  make(scene, (width + 1) / 2, (height + 1) / 2, DXGI_FORMAT_R16G16B16A16_FLOAT,
       2);
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Texture2D.MipLevels = 1;
  for (UINT i = 0; i < Count; ++i) {
    make(exposure[i], 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 3 + i);
    view.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateShaderResourceView(scene.Get(), &view, SrvCpu(i * 2));
    view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    device->CreateShaderResourceView(exposure[i].Get(), &view,
                                     SrvCpu(i * 2 + 1));
  }
  history = 0;
  historyValid = sceneValid = false;
}
void Renderer::CreatePipelines() {
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 2;
  range.BaseShaderRegister = 0;
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable = {1, &range};
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC signature{};
  signature.NumParameters = 2;
  signature.pParameters = parameters;
  signature.NumStaticSamplers = 1;
  signature.pStaticSamplers = &sampler;
  Com<ID3DBlob> blob, errors;
  Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
                                    &blob, &errors),
        "Serialize root signature");
  Check(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                    blob->GetBufferSize(), IID_PPV_ARGS(&root)),
        "Create root signature");
  const auto blackHole = std::string("#define vec3 float3\n") +
                         KerrSpacetime::ShaderFunctions() +
                         ReadShader(L"BlackHole.hlsl");
  const auto optics = ReadShader(L"Optics.hlsl");
  auto make = [&](const std::string &source, const char *pixel,
                  DXGI_FORMAT format, Com<ID3D12PipelineState> &pipeline) {
    auto vertex = Compile(source, "Fullscreen", "vs_5_1"),
         fragment = Compile(source, pixel, "ps_5_1");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root.Get();
    p.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    p.PS = {fragment->GetBufferPointer(), fragment->GetBufferSize()};
    p.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    p.SampleMask = UINT_MAX;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.DepthStencilState.DepthEnable = FALSE;
    p.DepthStencilState.StencilEnable = FALSE;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = format;
    p.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline)),
          "Create HLSL pipeline");
  };
  make(blackHole, "BlackHole", DXGI_FORMAT_R16G16B16A16_FLOAT, scenePipeline);
  make(blackHole, "KerrProbe", DXGI_FORMAT_R32G32B32A32_FLOAT, probePipeline);
  make(optics, "Meter", DXGI_FORMAT_R32G32B32A32_FLOAT, meterPipeline);
  make(optics, "Present", DXGI_FORMAT_R8G8B8A8_UNORM, finalPipeline);
}
void Renderer::Wait(UINT64 value) {
  if (fence->GetCompletedValue() == UINT64_MAX)
    Check(device->GetDeviceRemovedReason(), "DX12 device removed");
  if (fence->GetCompletedValue() >= value)
    return;
  Check(fence->SetEventOnCompletion(value, fenceEvent), "Arm GPU fence");
  if (WaitForSingleObject(fenceEvent, 30000) != WAIT_OBJECT_0)
    throw std::runtime_error("GPU fence timed out");
}
void Renderer::WaitIdle() {
  if (!queue)
    return;
  const UINT64 value = nextFence++;
  Check(queue->Signal(fence.Get(), value), "Signal idle fence");
  Wait(value);
}
void Renderer::Begin() {
  frameIndex = swapchain->GetCurrentBackBufferIndex();
  Wait(slots[frameIndex].fence);
  Check(slots[frameIndex].allocator->Reset(), "Reset retired allocator");
  Check(commands->Reset(slots[frameIndex].allocator.Get(), nullptr),
        "Reset command list");
}
void Renderer::Submit(bool present, bool vsync) {
  Check(commands->Close(), "Close frame commands");
  ID3D12CommandList *lists[] = {commands.Get()};
  queue->ExecuteCommandLists(1, lists);
  if (present) {
    lastBuffer = frameIndex;
    Check(swapchain->Present(vsync ? 1 : 0, 0), "Present DX12 frame");
  }
  const UINT64 value = nextFence++;
  Check(queue->Signal(fence.Get(), value), "Signal frame fence");
  slots[frameIndex].fence = value;
}
void Renderer::Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES from,
                          D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from,
                        to};
  commands->ResourceBarrier(1, &barrier);
}
void Renderer::Resize(UINT w, UINT h) {
  if (!w || !h || (w == width && h == height))
    return;
  WaitIdle();
  for (auto &b : buffers)
    b.Reset();
  scene.Reset();
  for (auto &e : exposure)
    e.Reset();
  Check(swapchain->ResizeBuffers(Count, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
        "Resize DX12 buffers");
  width = w;
  height = h;
  CreateTargets();
}
void Renderer::Draw(const Frame &input, bool dirty, bool vsync) {
  Begin();
  Frame frame = input;
  frame.viewport.x = static_cast<float>(width);
  frame.viewport.y = static_cast<float>(height);
  if (!historyValid)
    frame.viewport.w = 1;
  std::memcpy(constantData + frameIndex * sizeof(Frame), &frame, sizeof(Frame));
  ID3D12DescriptorHeap *heaps[] = {srvs.Get()};
  commands->SetDescriptorHeaps(1, heaps);
  commands->SetGraphicsRootSignature(root.Get());
  commands->SetGraphicsRootConstantBufferView(
      0, constants->GetGPUVirtualAddress() + frameIndex * sizeof(Frame));
  commands->SetGraphicsRootDescriptorTable(1, Srv(history * 2));
  commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  auto viewport = [&](UINT w, UINT h) {
    D3D12_VIEWPORT vp{0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1};
    D3D12_RECT rect{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    commands->RSSetViewports(1, &vp);
    commands->RSSetScissorRects(1, &rect);
  };
  if (dirty || !sceneValid) {
    Transition(scene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
    viewport((width + 1) / 2, (height + 1) / 2);
    const auto target = Rtv(2);
    commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
    commands->SetPipelineState(scenePipeline.Get());
    commands->DrawInstanced(3, 1, 0, 0);
    ++sceneDraws;
    Transition(scene.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    sceneValid = true;
  }
  if (!historyValid) {
    const float zero[4] = {};
    for (UINT i = 0; i < Count; ++i) {
      Transition(exposure[i].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_RENDER_TARGET);
      commands->ClearRenderTargetView(Rtv(3 + i), zero, 0, nullptr);
      Transition(exposure[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
  }
  const UINT next = 1 - history;
  Transition(exposure[next].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_RENDER_TARGET);
  viewport(1, 1);
  const auto meter = Rtv(3 + next);
  commands->OMSetRenderTargets(1, &meter, FALSE, nullptr);
  commands->SetPipelineState(meterPipeline.Get());
  commands->DrawInstanced(3, 1, 0, 0);
  Transition(exposure[next].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  history = next;
  historyValid = true;
  Transition(buffers[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
             D3D12_RESOURCE_STATE_RENDER_TARGET);
  viewport(width, height);
  const auto target = Rtv(frameIndex);
  commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
  commands->SetGraphicsRootDescriptorTable(1, Srv(history * 2));
  commands->SetPipelineState(finalPipeline.Get());
  commands->DrawInstanced(3, 1, 0, 0);
  Transition(buffers[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_PRESENT);
  Submit(true, vsync);
}
std::vector<unsigned char> Renderer::ReadTexture(ID3D12Resource *resource,
                                                 D3D12_RESOURCE_STATES state,
                                                 UINT &rowPitch) {
  WaitIdle();
  const auto desc = resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
  UINT64 size = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr,
                                &size);
  rowPitch = layout.Footprint.RowPitch;
  auto heap = Heap(D3D12_HEAP_TYPE_READBACK);
  auto descBuffer = Buffer(size);
  Com<ID3D12Resource> readback;
  Check(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &descBuffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
        "Create readback");
  Begin();
  Transition(resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = resource;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = readback.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = layout;
  commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
  Submit(false, false);
  WaitIdle();
  D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
  void *data = nullptr;
  Check(readback->Map(0, &range, &data), "Map checked readback");
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  std::memcpy(bytes.data(), data, bytes.size());
  D3D12_RANGE empty{0, 0};
  readback->Unmap(0, &empty);
  return bytes;
}
std::vector<unsigned char>
Renderer::Capture(const std::filesystem::path &path) {
  UINT pitch = 0;
  const auto bytes = ReadTexture(buffers[lastBuffer].Get(),
                                 D3D12_RESOURCE_STATE_PRESENT, pitch);
  std::vector<unsigned char> rgb(static_cast<size_t>(width) * height * 3);
  for (UINT y = 0; y < height; ++y)
    for (UINT x = 0; x < width; ++x)
      for (UINT c = 0; c < 3; ++c)
        rgb[(static_cast<size_t>(y) * width + x) * 3 + c] =
            bytes[static_cast<size_t>(y) * pitch + x * 4 + c];
  std::ofstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot write capture");
  file << "P6\n" << width << " " << height << "\n255\n";
  file.write(reinterpret_cast<const char *>(rgb.data()),
             static_cast<std::streamsize>(rgb.size()));
  return rgb;
}
std::array<float, 4> Renderer::ReadExposure() {
  UINT pitch = 0;
  const auto bytes =
      ReadTexture(exposure[history].Get(),
                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, pitch);
  std::array<float, 4> values{};
  std::memcpy(values.data(), bytes.data(), sizeof(values));
  return values;
}
std::array<float, 4> Renderer::ProbeKerr(const Frame &frame) {
  Begin();
  std::memcpy(constantData + frameIndex * sizeof(Frame), &frame, sizeof(Frame));
  commands->SetGraphicsRootSignature(root.Get());
  commands->SetGraphicsRootConstantBufferView(
      0, constants->GetGPUVirtualAddress() + frameIndex * sizeof(Frame));
  Transition(exposure[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_RENDER_TARGET);
  const D3D12_VIEWPORT viewport{0, 0, 1, 1, 0, 1};
  const D3D12_RECT rect{0, 0, 1, 1};
  const auto target = Rtv(3);
  commands->RSSetViewports(1, &viewport);
  commands->RSSetScissorRects(1, &rect);
  commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
  commands->SetPipelineState(probePipeline.Get());
  commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  commands->DrawInstanced(3, 1, 0, 0);
  Transition(exposure[0].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  Submit(false, false);
  UINT pitch = 0;
  const auto bytes = ReadTexture(
      exposure[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, pitch);
  std::array<float, 4> result{};
  std::memcpy(result.data(), bytes.data(), sizeof(result));
  historyValid = false;
  return result;
}
bool Renderer::SceneFinite() {
  UINT pitch = 0;
  const auto bytes = ReadTexture(
      scene.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, pitch);
  for (UINT y = 0; y < (height + 1) / 2; ++y)
    for (UINT x = 0; x < (width + 1) / 2; ++x)
      for (UINT c = 0; c < 4; ++c) {
        unsigned short value = 0;
        std::memcpy(
            &value,
            bytes.data() + static_cast<size_t>(y) * pitch + x * 8 + c * 2, 2);
        if ((value & 0x7c00) == 0x7c00)
          return false;
      }
  return true;
}
unsigned Renderer::ErrorCount() const {
  if (!info)
    return 0;
  unsigned errors = 0;
  for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
    SIZE_T size = 0;
    info->GetMessage(i, nullptr, &size);
    std::vector<unsigned char> data(size);
    auto *message = reinterpret_cast<D3D12_MESSAGE *>(data.data());
    if (SUCCEEDED(info->GetMessage(i, message, &size)) &&
        message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
      ++errors;
      std::cerr << "DX12 validation: " << message->pDescription << "\n";
    }
  }
  return errors;
}
} // namespace NativeDX12
