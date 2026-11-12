#include "Dx12SceneBridge.h"
#include "Dx12Renderer.h"
#include "ShaderTranslator.h"
#include "glad/glad.h"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <d3d11shader.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace NativeDX12 {
namespace {
bool sceneValidation = false, sceneWarp = false;
template <class T> using Com = Microsoft::WRL::ComPtr<T>;
void Check(HRESULT result, const char *action) {
  if (FAILED(result)) {
    std::ostringstream text;
    text << action << " (0x" << std::hex << static_cast<unsigned long>(result)
         << ")";
    throw std::runtime_error(text.str());
  }
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
  D3D12_HEAP_PROPERTIES h{};
  h.Type = type;
  h.CreationNodeMask = h.VisibleNodeMask = 1;
  return h;
}
D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = std::max<UINT64>(16, bytes);
  d.Height = 1;
  d.DepthOrArraySize = d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return d;
}
struct Texture {
  Com<ID3D12Resource> resource;
  UINT width = 0, height = 0, levels = 1, layers = 1;
  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  bool depth = false, mips = false;
  GLint minFilter = GL_LINEAR, magFilter = GL_LINEAR, wrapS = GL_REPEAT,
        wrapT = GL_REPEAT;
  std::vector<D3D12_RESOURCE_STATES> states;
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs, dsvs;
};
struct Buffer {
  Com<ID3D12Resource> resource;
  UINT bytes = 0;
};
struct Attribute {
  GLuint buffer = 0;
  GLint components = 4;
  GLsizei stride = 0;
  size_t offset = 0;
  bool enabled = false;
};
struct VertexArray {
  std::array<Attribute, 16> attributes;
};
struct Attachment {
  GLuint texture = 0;
  UINT level = 0, layer = 0;
};
struct Framebuffer {
  Attachment color;
  GLuint depth = 0;
};
struct Shader {
  GLenum type = 0;
  std::string source, log;
  bool compiled = true;
};
struct UniformMember {
  std::string name;
  UINT offset = 0, bytes = 0, rows = 1, columns = 1, elements = 0;
  D3D_SHADER_VARIABLE_CLASS type = D3D_SVC_SCALAR;
};
struct ShaderStage {
  Com<ID3DBlob> code;
  std::vector<UniformMember> uniforms;
  UINT constants = 16;
};
struct SamplerBinding {
  std::string name;
  UINT binding = 0;
  D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_TEXTURE2D;
};
struct UniformValue {
  std::array<unsigned char, 256> data{};
  UINT bytes = 0;
};
struct Program {
  std::vector<GLuint> shaders;
  std::string log, vertexHlsl, fragmentHlsl;
  bool linked = false, points = false;
  ShaderStage vs, ps;
  Com<ID3DBlob> gs;
  std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
  std::vector<SamplerBinding> samplers;
  std::vector<std::string> uniformNames;
  std::map<std::string, GLint> locations;
  std::map<std::string, UniformValue> values;
  std::unordered_map<std::string, Com<ID3D12PipelineState>> pipelines;
};
struct TextureUnit {
  GLuint image = 0, cube = 0;
};
UINT Mips(UINT w, UINT h) {
  UINT count = 1;
  while (w > 1 || h > 1) {
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
    ++count;
  }
  return count;
}
unsigned short Half(float value) {
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  const unsigned sign = bits >> 16 & 0x8000;
  const int exponent = static_cast<int>(bits >> 23 & 255) - 127 + 15;
  const unsigned fraction = bits & 0x7fffff;
  if (exponent >= 31)
    return static_cast<unsigned short>(sign | 0x7c00 | (fraction ? 0x200 : 0));
  if (exponent <= 0) {
    if (exponent < -10)
      return static_cast<unsigned short>(sign);
    return static_cast<unsigned short>(
        sign | (((fraction | 0x800000) >> (1 - exponent)) + 0x1000) >> 13);
  }
  return static_cast<unsigned short>(
      sign | static_cast<unsigned>(exponent) << 10 | (fraction + 0x1000) >> 13);
}
float Unhalf(unsigned short half) {
  const unsigned sign = (half & 0x8000u) << 16;
  unsigned exponent = (half >> 10) & 31, mantissa = half & 1023;
  if (exponent == 0) {
    if (!mantissa)
      return std::bit_cast<float>(sign);
    int shift = 0;
    while (!(mantissa & 1024)) {
      mantissa <<= 1;
      ++shift;
    }
    return std::bit_cast<float>(sign |
                                static_cast<unsigned>(127 - 14 - shift) << 23 |
                                (mantissa & 1023) << 13);
  }
  return std::bit_cast<float>(
      sign | (exponent == 31 ? 255 : exponent + 112) << 23 | mantissa << 13);
}
UINT PixelBytes(DXGI_FORMAT f) {
  return f == DXGI_FORMAT_R16G16B16A16_FLOAT   ? 8
         : f == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16
                                               : 4;
}
Com<ID3DBlob> Compile(const std::string &source, const char *profile) {
  Com<ID3DBlob> code, error;
  const auto result = D3DCompile(
      source.data(), source.size(), "shared scene", nullptr, nullptr, "main",
      profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
      &code, &error);
  if (FAILED(result))
    throw std::runtime_error(
        error ? std::string(static_cast<char *>(error->GetBufferPointer()),
                            error->GetBufferSize())
              : "Cannot compile shared HLSL");
  return code;
}
void Reflect(ShaderStage &stage) {
  Com<ID3D11ShaderReflection> reflection;
  Check(D3DReflect(stage.code->GetBufferPointer(), stage.code->GetBufferSize(),
                   IID_PPV_ARGS(&reflection)),
        "Reflect shader constants");
  auto *constants = reflection->GetConstantBufferByName("$Globals");
  D3D11_SHADER_BUFFER_DESC cb{};
  if (SUCCEEDED(constants->GetDesc(&cb))) {
    stage.constants = std::max(16u, cb.Size);
    for (UINT i = 0; i < cb.Variables; ++i) {
      auto *variable = constants->GetVariableByIndex(i);
      D3D11_SHADER_VARIABLE_DESC v{};
      variable->GetDesc(&v);
      D3D11_SHADER_TYPE_DESC type{};
      variable->GetType()->GetDesc(&type);
      std::string name = v.Name;
      if (name.starts_with("u_"))
        name.erase(0, 2);
      stage.uniforms.push_back({name, v.StartOffset, v.Size, type.Rows,
                                type.Columns, type.Elements, type.Class});
    }
  }
}
void AlignStageInputs(const std::string &vertex, std::string &pixel) {
  const auto vs = vertex.find("struct SPIRV_Cross_Output"),
             ps = pixel.find("struct SPIRV_Cross_Input");
  if (vs == std::string::npos || ps == std::string::npos)
    return;
  const auto vb = vertex.find('{', vs), ve = vertex.find("};", vb),
             pb = pixel.find('{', ps), pe = pixel.find("};", pb);
  auto fields = [](const std::string &text) {
    std::vector<std::pair<std::string, std::string>> result;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
      const auto colon = line.find(':'), end = line.find(';');
      if (colon == std::string::npos || end == std::string::npos)
        continue;
      auto semantic = line.substr(colon + 1, end - colon - 1);
      semantic.erase(
          std::remove_if(semantic.begin(), semantic.end(),
                         [](unsigned char c) { return std::isspace(c) != 0; }),
          semantic.end());
      result.emplace_back(semantic, line);
    }
    return result;
  };
  const auto outputs = fields(vertex.substr(vb + 1, ve - vb - 1)),
             inputs = fields(pixel.substr(pb + 1, pe - pb - 1));
  std::ostringstream body;
  body << "\n";
  std::set<std::string> used;
  for (const auto &[semantic, declaration] : outputs) {
    const auto found =
        std::find_if(inputs.begin(), inputs.end(), [&](const auto &input) {
          return input.first == semantic;
        });
    if (found != inputs.end())
      body << found->second << "\n";
    else {
      const auto colon = declaration.find(':'),
                 nameEnd = declaration.find_last_not_of(" \t", colon - 1),
                 nameStart = declaration.find_last_of(" \t", nameEnd);
      body << declaration.substr(0, nameStart + 1) << "dxUnused_" << semantic
           << declaration.substr(nameEnd + 1) << "\n";
    }
    used.insert(semantic);
  }
  for (const auto &[semantic, declaration] : inputs)
    if (!used.count(semantic))
      body << declaration << "\n";
  pixel.replace(pb + 1, pe - pb - 1, body.str());
}
} // namespace

class SceneBridge {
public:
  struct UploadAllocation {
    unsigned char *first;
    D3D12_GPU_VIRTUAL_ADDRESS second;
    ID3D12Resource *resource;
    UINT64 offset;
  };
  Renderer gpu;
  HWND window = nullptr;
  bool recording = false, vsync = true;
  GLuint nextId = 1, boundBuffer = 0, boundVao = 0, boundFbo = 0,
         boundRenderbuffer = 0, boundProgram = 0;
  UINT activeUnit = 0, rtvCursor = 0, dsvCursor = 0, srvCursor = 0,
       samplerCursor = 0;
  GLenum error = GL_NO_ERROR, readBuffer = GL_BACK, polygon = GL_FILL;
  GLint pack = 4, unpack = 4;
  GLfloat pointSize = 1;
  std::array<float, 4> clearColor{0, 0, 0, 0};
  std::array<GLint, 4> viewport{0, 0, 1280, 720}, scissor{0, 0, 1280, 720};
  bool depthTest = false, depthMask = true, blend = false, scissorTest = false,
       cull = false;
  GLenum sourceRgb = GL_ONE, destRgb = GL_ZERO, sourceAlpha = GL_ONE,
         destAlpha = GL_ZERO;
  std::unordered_map<GLuint, Buffer> buffers;
  std::unordered_map<GLuint, VertexArray> arrays;
  std::unordered_map<GLuint, Texture> textures, renderbuffers;
  std::unordered_map<GLuint, Framebuffer> framebuffers;
  std::unordered_map<GLuint, Shader> shaders;
  std::unordered_map<GLuint, Program> programs;
  std::array<TextureUnit, 32> units;
  Texture defaultColor, defaultDepth;
  Com<ID3D12DescriptorHeap> rtvs, dsvs, srvs, samplers;
  Com<ID3D12RootSignature> root, utilityRoot;
  std::map<DXGI_FORMAT, Com<ID3D12PipelineState>> utilityPipelines;
  Com<ID3D12Resource> zeroBuffer;
  std::array<std::vector<Com<ID3D12Resource>>, 2> retired;
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> freeRtvs, freeDsvs;
  std::array<std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>, 2> retiredRtvs,
      retiredDsvs;
  std::array<Com<ID3D12Resource>, 2> upload;
  std::array<unsigned char *, 2> mapped{};
  UINT64 uploadCursor = 0;
  static constexpr UINT64 UploadBytes = 16 * 1024 * 1024;
  std::map<std::string, D3D12_GPU_DESCRIPTOR_HANDLE> samplerTables;
  UINT rtvStride = 0, dsvStride = 0, srvStride = 0, samplerStride = 0;
  ~SceneBridge() {
    if (recording)
      Flush();
    gpu.WaitIdle();
    for (UINT i = 0; i < 2; ++i)
      if (upload[i] && mapped[i])
        upload[i]->Unmap(0, nullptr);
  }
  void Initialize(HWND hwnd);
  void Begin();
  void Flush();
  void Present();
  void Allocate(Texture &texture, UINT w, UINT h, DXGI_FORMAT format,
                bool depth, UINT layers = 1);
  void State(Texture &texture, D3D12_RESOURCE_STATES state, UINT level = 0,
             UINT layer = 0);
  UploadAllocation Upload(UINT64 bytes, UINT64 alignment = 256);
  D3D12_CPU_DESCRIPTOR_HANDLE Rtv() {
    if (!freeRtvs.empty()) {
      auto h = freeRtvs.back();
      freeRtvs.pop_back();
      return h;
    }
    auto h = rtvs->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(rtvCursor++) * rtvStride;
    if (rtvCursor >= 16384)
      throw std::runtime_error("RTV capacity exceeded");
    return h;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE Dsv() {
    if (!freeDsvs.empty()) {
      auto h = freeDsvs.back();
      freeDsvs.pop_back();
      return h;
    }
    auto h = dsvs->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(dsvCursor++) * dsvStride;
    if (dsvCursor >= 2048)
      throw std::runtime_error("DSV capacity exceeded");
    return h;
  }
  void RecycleViews(Texture &texture) {
    freeRtvs.insert(freeRtvs.end(), texture.rtvs.begin(), texture.rtvs.end());
    freeDsvs.insert(freeDsvs.end(), texture.dsvs.begin(), texture.dsvs.end());
    texture.rtvs.clear();
    texture.dsvs.clear();
  }
  D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu(UINT i) {
    auto h = srvs->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(i) * srvStride;
    return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE Srv(UINT i) {
    auto h = srvs->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(i) * srvStride;
    return h;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE SamplerCpu(UINT i) {
    auto h = samplers->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(i) * samplerStride;
    return h;
  }
  Texture &BoundTexture(GLenum target) {
    const bool cube = target == GL_TEXTURE_CUBE_MAP ||
                      (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                       target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6);
    return textures[cube ? units[activeUnit].cube : units[activeUnit].image];
  }
  Texture &Color() {
    return boundFbo ? textures[framebuffers[boundFbo].color.texture]
                    : defaultColor;
  }
  Texture *Depth() {
    if (!boundFbo)
      return &defaultDepth;
    const auto id = framebuffers[boundFbo].depth;
    return id ? &renderbuffers[id] : nullptr;
  }
  Attachment Target() {
    return boundFbo ? framebuffers[boundFbo].color : Attachment{};
  }
  void BindTargets();
  void BufferData(GLsizeiptr size, const void *data);
  void TexImage(GLenum target, GLint level, GLint internal, GLsizei w,
                GLsizei h, GLenum format, GLenum type, const void *data);
  void CopyTexture(GLenum target, GLint level, GLint dx, GLint dy, GLint sx,
                   GLint sy, GLsizei w, GLsizei h);
  void GenerateMips(Texture &texture);
  void Clear(GLbitfield bits);
  void Link(Program &program);
  void SetUniform(GLint location, const void *value, UINT bytes);
  void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances);
  D3D12_GPU_VIRTUAL_ADDRESS Constants(const ShaderStage &stage,
                                      const Program &program);
  D3D12_GPU_DESCRIPTOR_HANDLE BindTextures(const Program &program);
  Com<ID3D12PipelineState> Pipeline(Program &program, GLenum mode);
  std::vector<unsigned char> Read(Texture &texture, UINT level, UINT layer,
                                  UINT &pitch);
  void GetTexture(GLenum target, GLint level, GLenum format, GLenum type,
                  void *data);
  void ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                  GLenum type, void *data);
  void Blit(Texture &texture, UINT sourceLevel,
            D3D12_CPU_DESCRIPTOR_HANDLE target, DXGI_FORMAT format, UINT w,
            UINT h, bool flip);
};
namespace {
std::unique_ptr<SceneBridge> bridge;
SceneBridge &C() {
  if (!bridge)
    throw std::runtime_error("DX12 scene backend is not initialized");
  return *bridge;
}
} // namespace

void SceneBridge::Initialize(HWND hwnd) {
  window = hwnd;
  RECT size{};
  GetClientRect(window, &size);
  gpu.Initialize(window, size.right, size.bottom, sceneWarp, false,
                 sceneValidation);
  auto makeHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible,
                      Com<ID3D12DescriptorHeap> &heap) {
    D3D12_DESCRIPTOR_HEAP_DESC d{};
    d.Type = type;
    d.NumDescriptors = count;
    d.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                      : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Check(gpu.device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&heap)),
          "Create scene descriptors");
    return gpu.device->GetDescriptorHandleIncrementSize(type);
  };
  rtvStride = makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16384, false, rtvs);
  dsvStride = makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2048, false, dsvs);
  srvStride =
      makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, true, srvs);
  samplerStride =
      makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true, samplers);
  D3D12_ROOT_PARAMETER p[5]{};
  for (UINT i = 0; i < 3; ++i) {
    p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[i].Descriptor.ShaderRegister = i == 2 ? 3 : 0;
    p[i].ShaderVisibility = i == 0   ? D3D12_SHADER_VISIBILITY_VERTEX
                            : i == 1 ? D3D12_SHADER_VISIBILITY_PIXEL
                                     : D3D12_SHADER_VISIBILITY_GEOMETRY;
  }
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 8;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  ranges[1].NumDescriptors = 8;
  for (UINT i = 0; i < 2; ++i) {
    p[3 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[3 + i].DescriptorTable = {1, &ranges[i]};
    p[3 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters = 5;
  desc.pParameters = p;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  Com<ID3DBlob> signature, error;
  Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                    &signature, &error),
        "Serialize scene signature");
  Check(gpu.device->CreateRootSignature(0, signature->GetBufferPointer(),
                                        signature->GetBufferSize(),
                                        IID_PPV_ARGS(&root)),
        "Create scene root");
  for (UINT i = 0; i < 2; ++i) {
    auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
    auto d = BufferDesc(UploadBytes);
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                              D3D12_RESOURCE_STATE_GENERIC_READ,
                                              nullptr,
                                              IID_PPV_ARGS(&upload[i])),
          "Create scene upload ring");
    D3D12_RANGE empty{0, 0};
    Check(upload[i]->Map(0, &empty, reinterpret_cast<void **>(&mapped[i])),
          "Map scene ring");
  }
  auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
  auto d = BufferDesc(16);
  Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                            D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr, IID_PPV_ARGS(&zeroBuffer)),
        "Create default attributes");
  void *zeros = nullptr;
  D3D12_RANGE empty{0, 0};
  Check(zeroBuffer->Map(0, &empty, &zeros), "Map default attributes");
  std::memset(zeros, 0, 16);
  static_cast<float *>(zeros)[3] = 1;
  zeroBuffer->Unmap(0, nullptr);
  Allocate(defaultColor, gpu.width, gpu.height, DXGI_FORMAT_R8G8B8A8_UNORM,
           false);
  Allocate(defaultDepth, gpu.width, gpu.height, DXGI_FORMAT_R32_TYPELESS, true);
}
void SceneBridge::Begin() {
  if (recording)
    return;
  RECT size{};
  GetClientRect(window, &size);
  const UINT w = std::max<LONG>(1, size.right),
             h = std::max<LONG>(1, size.bottom);
  if (w != gpu.width || h != gpu.height) {
    gpu.Resize(w, h);
    Allocate(defaultColor, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    Allocate(defaultDepth, w, h, DXGI_FORMAT_R32_TYPELESS, true);
  }
  gpu.Begin();
  retired[gpu.frameIndex].clear();
  freeRtvs.insert(freeRtvs.end(), retiredRtvs[gpu.frameIndex].begin(),
                  retiredRtvs[gpu.frameIndex].end());
  retiredRtvs[gpu.frameIndex].clear();
  freeDsvs.insert(freeDsvs.end(), retiredDsvs[gpu.frameIndex].begin(),
                  retiredDsvs[gpu.frameIndex].end());
  retiredDsvs[gpu.frameIndex].clear();
  uploadCursor = 0;
  srvCursor = gpu.frameIndex * 32768;
  recording = true;
}
void SceneBridge::Flush() {
  if (!recording)
    return;
  gpu.Submit(false, false);
  recording = false;
  gpu.WaitIdle();
}
SceneBridge::UploadAllocation SceneBridge::Upload(UINT64 bytes,
                                                  UINT64 alignment) {
  Begin();
  uploadCursor = (uploadCursor + alignment - 1) & ~(alignment - 1);
  if (uploadCursor + bytes > UploadBytes) {
    // Large imports and startup textures must not require a permanently large
    // per-frame ring or force a flush in the middle of binding a draw call.
    Com<ID3D12Resource> large;
    auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = BufferDesc(bytes);
    Check(gpu.device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &desc,
              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&large)),
          "Allocate large scene upload");
    unsigned char *data = nullptr;
    D3D12_RANGE empty{0, 0};
    Check(large->Map(0, &empty, reinterpret_cast<void **>(&data)),
          "Map large scene upload");
    retired[gpu.frameIndex].push_back(large);
    return {data, large->GetGPUVirtualAddress(), large.Get(), 0};
  }
  const auto offset = uploadCursor;
  uploadCursor += bytes;
  return {mapped[gpu.frameIndex] + offset,
          upload[gpu.frameIndex]->GetGPUVirtualAddress() + offset,
          upload[gpu.frameIndex].Get(), offset};
}
void SceneBridge::Allocate(Texture &t, UINT w, UINT h, DXGI_FORMAT f,
                           bool depth, UINT layers) {
  if (t.resource) {
    if (recording) {
      retired[gpu.frameIndex].push_back(t.resource);
      retiredRtvs[gpu.frameIndex].insert(retiredRtvs[gpu.frameIndex].end(),
                                         t.rtvs.begin(), t.rtvs.end());
      retiredDsvs[gpu.frameIndex].insert(retiredDsvs[gpu.frameIndex].end(),
                                         t.dsvs.begin(), t.dsvs.end());
    } else {
      gpu.WaitIdle();
      RecycleViews(t);
    }
  }
  t.width = std::max(1u, w);
  t.height = std::max(1u, h);
  t.depth = depth;
  t.format = f;
  t.layers = layers;
  t.levels = depth ? 1 : Mips(t.width, t.height);
  t.mips = false;
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  d.Width = t.width;
  d.Height = t.height;
  d.DepthOrArraySize = static_cast<UINT16>(layers);
  d.MipLevels = static_cast<UINT16>(t.levels);
  d.Format = f;
  d.SampleDesc.Count = 1;
  d.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                  : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE clear{};
  clear.Format = depth ? DXGI_FORMAT_D32_FLOAT : f;
  if (depth)
    clear.DepthStencil.Depth = 1;
  auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
  Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                            D3D12_RESOURCE_STATE_COMMON, &clear,
                                            IID_PPV_ARGS(&t.resource)),
        "Allocate scene image");
  t.states.assign(static_cast<size_t>(layers) * t.levels,
                  D3D12_RESOURCE_STATE_COMMON);
  t.rtvs.clear();
  t.dsvs.clear();
  for (UINT layer = 0; layer < layers; ++layer)
    for (UINT level = 0; level < t.levels; ++level) {
      if (depth) {
        auto handle = Dsv();
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_D32_FLOAT;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        gpu.device->CreateDepthStencilView(t.resource.Get(), &view, handle);
        t.dsvs.push_back(handle);
      } else {
        auto handle = Rtv();
        D3D12_RENDER_TARGET_VIEW_DESC view{};
        view.Format = f;
        view.ViewDimension = layers > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY
                                        : D3D12_RTV_DIMENSION_TEXTURE2D;
        if (layers > 1) {
          view.Texture2DArray.MipSlice = level;
          view.Texture2DArray.FirstArraySlice = layer;
          view.Texture2DArray.ArraySize = 1;
        } else
          view.Texture2D.MipSlice = level;
        gpu.device->CreateRenderTargetView(t.resource.Get(), &view, handle);
        t.rtvs.push_back(handle);
      }
    }
}
void SceneBridge::State(Texture &t, D3D12_RESOURCE_STATES next, UINT level,
                        UINT layer) {
  Begin();
  const UINT sub = level + layer * t.levels;
  if (t.states[sub] == next)
    return;
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {t.resource.Get(), sub, t.states[sub], next};
  gpu.commands->ResourceBarrier(1, &barrier);
  t.states[sub] = next;
}
void SceneBridge::BindTargets() {
  if (!boundFbo) {
    RECT size{};
    GetClientRect(window, &size);
    if (static_cast<UINT>(size.right) != gpu.width ||
        static_cast<UINT>(size.bottom) != gpu.height) {
      Flush();
      Begin();
    }
  }
  auto &color = Color();
  const auto target = Target();
  State(color, D3D12_RESOURCE_STATE_RENDER_TARGET, target.level, target.layer);
  const auto rtv = color.rtvs[target.level + target.layer * color.levels];
  auto *depth = Depth();
  if (depth && depth->resource) {
    State(*depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    const auto dsv = depth->dsvs[0];
    gpu.commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  } else
    gpu.commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}

void SceneBridge::BufferData(GLsizeiptr size, const void *data) {
  if (size < 0 || static_cast<UINT64>(size) > UINT_MAX)
    throw std::runtime_error("Mesh exceeds the DX12 vertex-buffer view limit");
  Begin();
  auto &buffer = buffers[boundBuffer];
  if (buffer.resource)
    retired[gpu.frameIndex].push_back(buffer.resource);
  buffer.bytes = static_cast<UINT>(std::max<GLsizeiptr>(16, size));
  auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
  auto desc = BufferDesc(buffer.bytes);
  Check(gpu.device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&buffer.resource)),
        "Allocate mesh buffer");
  auto memory = Upload(buffer.bytes);
  if (data)
    std::memcpy(memory.first, data, static_cast<size_t>(size));
  else
    std::memset(memory.first, 0, buffer.bytes);
  gpu.commands->CopyBufferRegion(buffer.resource.Get(), 0, memory.resource,
                                 memory.offset, buffer.bytes);
  gpu.Transition(buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
}
void SceneBridge::TexImage(GLenum target, GLint level, GLint internal,
                           GLsizei w, GLsizei h, GLenum format, GLenum type,
                           const void *data) {
  if (w <= 0 || h <= 0 || level < 0) {
    error = GL_INVALID_VALUE;
    return;
  }
  Begin();
  auto &texture = BoundTexture(target);
  const bool depth = internal == GL_DEPTH_COMPONENT24 ||
                     internal == GL_DEPTH_COMPONENT ||
                     internal == GL_DEPTH_COMPONENT32F;
  const auto native = depth                    ? DXGI_FORMAT_R32_TYPELESS
                      : internal == GL_RGBA32F ? DXGI_FORMAT_R32G32B32A32_FLOAT
                      : internal == GL_RGBA16F ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                               : DXGI_FORMAT_R8G8B8A8_UNORM;
  const bool cube = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                    target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6;
  if (!texture.resource || texture.width != static_cast<UINT>(w << level) ||
      texture.height != static_cast<UINT>(h << level) ||
      texture.format != native || texture.layers != (cube ? 6u : 1u))
    Allocate(texture, w << level, h << level, native, depth, cube ? 6 : 1);
  if (!data)
    return;
  const UINT layer = cube ? target - GL_TEXTURE_CUBE_MAP_POSITIVE_X : 0,
             sub = level + layer * texture.levels;
  auto desc = texture.resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
  UINT64 total = 0;
  gpu.device->GetCopyableFootprints(&desc, sub, 1, 0, &layout, nullptr, nullptr,
                                    &total);
  auto uploadMemory = Upload(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
  layout.Offset = uploadMemory.offset;
  const UINT channels = format == GL_RGB               ? 3
                        : format == GL_DEPTH_COMPONENT ? 1
                                                       : 4;
  const UINT sourceComponent = type == GL_FLOAT ? 4 : 1;
  const UINT sourcePitch =
      (w * channels * sourceComponent + unpack - 1) / unpack * unpack;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float values[4] = {0, 0, 0, 1};
      for (UINT c = 0; c < channels; ++c) {
        const auto *p = static_cast<const unsigned char *>(data) +
                        static_cast<size_t>(y) * sourcePitch +
                        (x * channels + c) * sourceComponent;
        if (type == GL_FLOAT)
          std::memcpy(&values[c], p, 4);
        else
          values[c] = *p / 255.f;
      }
      auto *destination = uploadMemory.first +
                          static_cast<size_t>(y) * layout.Footprint.RowPitch +
                          x * PixelBytes(native);
      if (native == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        for (UINT c = 0; c < 4; ++c) {
          const auto value = Half(values[c]);
          std::memcpy(destination + c * 2, &value, 2);
        }
      } else if (native == DXGI_FORMAT_R32G32B32A32_FLOAT)
        std::memcpy(destination, values, 16);
      else if (depth)
        std::memcpy(destination, values, 4);
      else
        for (UINT c = 0; c < 4; ++c)
          destination[c] = static_cast<unsigned char>(
              std::clamp(values[c], 0.f, 1.f) * 255 + .5f);
    }
  State(texture, D3D12_RESOURCE_STATE_COPY_DEST, level, layer);
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = uploadMemory.resource;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = layout;
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = texture.resource.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = sub;
  gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  State(texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, level, layer);
}
void SceneBridge::CopyTexture(GLenum target, GLint level, GLint dx, GLint dy,
                              GLint sx, GLint sy, GLsizei w, GLsizei h) {
  Begin();
  auto &destination = BoundTexture(target);
  auto &source = destination.depth ? *Depth() : Color();
  const auto attachment = Target();
  const UINT sourceLevel = destination.depth ? 0 : attachment.level,
             sourceLayer = destination.depth ? 0 : attachment.layer;
  const UINT destinationLayer =
      target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
              target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6
          ? target - GL_TEXTURE_CUBE_MAP_POSITIVE_X
          : 0;
  State(source, D3D12_RESOURCE_STATE_COPY_SOURCE, sourceLevel, sourceLayer);
  State(destination, D3D12_RESOURCE_STATE_COPY_DEST, level, destinationLayer);
  D3D12_TEXTURE_COPY_LOCATION from{}, to{};
  from.pResource = source.resource.Get();
  from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  from.SubresourceIndex = sourceLevel + sourceLayer * source.levels;
  to.pResource = destination.resource.Get();
  to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  to.SubresourceIndex = level + destinationLayer * destination.levels;
  const D3D12_BOX box{static_cast<UINT>(sx),     static_cast<UINT>(sy),     0,
                      static_cast<UINT>(sx + w), static_cast<UINT>(sy + h), 1};
  // Depth copies must cover the entire subresource under D3D12 rules.
  if (source.depth) {
    gpu.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  } else
    gpu.commands->CopyTextureRegion(&to, dx, dy, 0, &from, &box);
  State(destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, level,
        destinationLayer);
}
void SceneBridge::Clear(GLbitfield bits) {
  // Native menus/panels can change the client extent after state uploads
  // have started. Retire those uploads before resizing the first scene frame.
  if (!boundFbo && recording) {
    RECT size{};
    GetClientRect(window, &size);
    if (static_cast<UINT>(size.right) != gpu.width ||
        static_cast<UINT>(size.bottom) != gpu.height)
      Flush();
  }
  Begin();
  auto &color = Color();
  const auto target = Target();
  const auto rtv = color.rtvs[target.level + target.layer * color.levels];
  D3D12_RECT rect{scissor[0], scissor[1], scissor[0] + scissor[2],
                  scissor[1] + scissor[3]};
  if (bits & GL_COLOR_BUFFER_BIT) {
    State(color, D3D12_RESOURCE_STATE_RENDER_TARGET, target.level,
          target.layer);
    gpu.commands->ClearRenderTargetView(rtv, clearColor.data(),
                                        scissorTest ? 1 : 0,
                                        scissorTest ? &rect : nullptr);
  }
  auto *depth = Depth();
  if ((bits & GL_DEPTH_BUFFER_BIT) && depthMask && depth && depth->resource) {
    State(*depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    gpu.commands->ClearDepthStencilView(depth->dsvs[0], D3D12_CLEAR_FLAG_DEPTH,
                                        1, 0, scissorTest ? 1 : 0,
                                        scissorTest ? &rect : nullptr);
  }
}
void SceneBridge::Link(Program &program) {
  try {
    std::string vs, ps;
    for (auto id : program.shaders) {
      const auto &shader = shaders[id];
      if (shader.type == GL_VERTEX_SHADER)
        vs = shader.source;
      else
        ps = shader.source;
    }
    program.points = ps.find("gl_PointCoord") != std::string::npos;
    if (program.points) {
      for (size_t pos = 0;
           (pos = ps.find("gl_PointCoord", pos)) != std::string::npos;
           pos += 13)
        ps.replace(pos, 13, "dxPointCoord");
      const auto insert = ps.find('\n');
      ps.insert(insert + 1, "in vec2 dxPointCoord;\n");
      vs.insert(vs.find('\n') + 1, "out vec2 dxPointCoord;\n");
      const auto main = vs.find("void main"), brace = vs.find('{', main);
      if (brace == std::string::npos)
        throw std::runtime_error("Cannot find vertex entry point");
      vs.insert(brace + 1, "dxPointCoord=vec2(.5);\n");
    }
    TranslatedProgram translated{};
    if (!TranslateSceneShaders(vs.c_str(), ps.c_str(), &translated)) {
      const std::string message = translated.error
                                      ? translated.error
                                      : "Cannot translate scene shaders";
      FreeTranslatedProgram(&translated);
      throw std::runtime_error(message);
    }
    program.vertexHlsl = translated.vertex;
    program.fragmentHlsl = translated.fragment;
    FreeTranslatedProgram(&translated);
    AlignStageInputs(program.vertexHlsl, program.fragmentHlsl);
    program.vs.code = Compile(program.vertexHlsl, "vs_5_1");
    program.ps.code = Compile(program.fragmentHlsl, "ps_5_1");
    Reflect(program.vs);
    Reflect(program.ps);
    Com<ID3D11ShaderReflection> reflection;
    Check(D3DReflect(program.vs.code->GetBufferPointer(),
                     program.vs.code->GetBufferSize(),
                     IID_PPV_ARGS(&reflection)),
          "Reflect vertex layout");
    D3D11_SHADER_DESC desc{};
    reflection->GetDesc(&desc);
    for (UINT i = 0; i < desc.InputParameters; ++i) {
      D3D11_SIGNATURE_PARAMETER_DESC parameter{};
      reflection->GetInputParameterDesc(i, &parameter);
      if (parameter.SystemValueType != D3D_NAME_UNDEFINED)
        continue;
      const UINT components =
          std::popcount(static_cast<unsigned>(parameter.Mask));
      const DXGI_FORMAT formats[] = {
          DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT,
          DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
      program.inputs.push_back({"TEXCOORD", parameter.SemanticIndex,
                                formats[components], parameter.SemanticIndex, 0,
                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    }
    Check(D3DReflect(program.ps.code->GetBufferPointer(),
                     program.ps.code->GetBufferSize(),
                     IID_PPV_ARGS(&reflection)),
          "Reflect scene textures");
    reflection->GetDesc(&desc);
    for (UINT i = 0; i < desc.BoundResources; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC binding{};
      reflection->GetResourceBindingDesc(i, &binding);
      if (binding.Type != D3D_SIT_TEXTURE)
        continue;
      std::string name = binding.Name;
      if (name.starts_with("tex_"))
        name.erase(0, 4);
      program.samplers.push_back({name, binding.BindPoint, binding.Dimension});
    }
    if (program.points) {
      const auto begin = program.vertexHlsl.find("struct SPIRV_Cross_Output"),
                 end = program.vertexHlsl.find("};", begin);
      const auto declaration =
          program.vertexHlsl.substr(begin, end + 2 - begin);
      std::ostringstream gs;
      gs << declaration
         << "\ncbuffer PointParameters:register(b3){float2 viewport;float "
            "pointSize;float unused;}\n"
         << "[maxvertexcount(4)] void main(point SPIRV_Cross_Output "
            "input[1],inout TriangleStream<SPIRV_Cross_Output> stream){"
         << "float2 "
            "corners[4]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,1)};"
         << "float size=max(1,ceil(pointSize-.5));float2 "
            "window=(input[0].gl_Position.xy/"
            "input[0].gl_Position.w*float2(.5,-.5)+.5)*viewport;"
         << "float2 "
            "center=(fmod(size,2)>0?floor(window)+.5:floor(window+.5))/"
            "viewport;"
         << "[unroll]for(int i=0;i<4;++i){SPIRV_Cross_Output "
            "output=input[0];output.gl_Position.xy=((center-.5)*float2(2,-2)+"
            "corners[i]*size/"
            "viewport)*output.gl_Position.w;output.dxPointCoord=corners[i]*.5+."
            "5;stream.Append(output);}stream.RestartStrip();}";
      program.gs = Compile(gs.str(), "gs_5_1");
    }
    program.linked = true;
  } catch (const std::exception &failure) {
    program.log = failure.what();
    program.linked = false;
    std::cerr << "DX12 scene shader: " << program.log << "\n";
  }
}
void SceneBridge::SetUniform(GLint location, const void *value, UINT bytes) {
  if (location < 0)
    return;
  auto &program = programs[boundProgram];
  if (static_cast<size_t>(location) >= program.uniformNames.size())
    return;
  auto &uniform = program.values[program.uniformNames[location]];
  uniform.bytes = std::min<UINT>(bytes, static_cast<UINT>(uniform.data.size()));
  std::memcpy(uniform.data.data(), value, uniform.bytes);
}
D3D12_GPU_VIRTUAL_ADDRESS SceneBridge::Constants(const ShaderStage &stage,
                                                 const Program &program) {
  auto memory = Upload((stage.constants + 255) & ~255u);
  std::memset(memory.first, 0, (stage.constants + 255) & ~255u);
  for (const auto &variable : stage.uniforms) {
    const auto found = program.values.find(variable.name);
    if (found == program.values.end())
      continue;
    const auto &value = found->second;
    if (variable.type == D3D_SVC_MATRIX_COLUMNS ||
        variable.type == D3D_SVC_MATRIX_ROWS) {
      // SPIRV-Cross represents GLSL matrices transposed in HLSL's row-vector
      // algebra.
      for (UINT r = 0; r < variable.rows; ++r)
        for (UINT c = 0; c < variable.columns; ++c) {
          const UINT source = (r * variable.columns + c) * 4;
          const UINT destination = variable.type == D3D_SVC_MATRIX_COLUMNS
                                       ? c * 16 + r * 4
                                       : r * 16 + c * 4;
          if (source + 4 <= value.bytes)
            std::memcpy(memory.first + variable.offset + destination,
                        value.data.data() + source, 4);
        }
    } else
      std::memcpy(memory.first + variable.offset, value.data.data(),
                  std::min(variable.bytes, value.bytes));
  }
  return memory.second;
}

D3D12_GPU_DESCRIPTOR_HANDLE SceneBridge::BindTextures(const Program &program) {
  if (srvCursor + 8 > (gpu.frameIndex + 1) * 32768)
    throw std::runtime_error("Scene descriptor budget exceeded");
  const UINT first = srvCursor;
  srvCursor += 8;
  std::array<D3D12_SAMPLER_DESC, 8> sampling{};
  for (UINT slot = 0; slot < 8; ++slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    Texture *texture = nullptr;
    for (const auto &binding : program.samplers)
      if (binding.binding == slot) {
        int unit = 0;
        const auto value = program.values.find(binding.name);
        if (value != program.values.end())
          std::memcpy(&unit, value->second.data.data(), 4);
        unit = std::clamp(unit, 0, 31);
        const GLuint id = binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBE
                              ? units[unit].cube
                              : units[unit].image;
        auto found = textures.find(id);
        if (found != textures.end() && found->second.resource)
          texture = &found->second;
        if (binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBE) {
          view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
          view.TextureCube.MipLevels = 1;
        }
      }
    auto &sampler = sampling[slot];
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxAnisotropy = 1;
    if (texture) {
      view.Format = texture->depth ? DXGI_FORMAT_R32_FLOAT : texture->format;
      if (view.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
        view.TextureCube.MipLevels = texture->mips ? texture->levels : 1;
      else
        view.Texture2D.MipLevels = texture->mips ? texture->levels : 1;
      for (UINT layer = 0; layer < texture->layers; ++layer)
        for (UINT level = 0; level < (texture->mips ? texture->levels : 1);
             ++level)
          State(*texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, level,
                layer);
      sampler.Filter =
          texture->minFilter == GL_NEAREST && texture->magFilter == GL_NEAREST
              ? D3D12_FILTER_MIN_MAG_MIP_POINT
              : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
      sampler.AddressU = texture->wrapS == GL_REPEAT
                             ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                             : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      sampler.AddressV = texture->wrapT == GL_REPEAT
                             ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                             : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      if (texture->minFilter == GL_LINEAR_MIPMAP_LINEAR ||
          texture->minFilter == GL_NEAREST_MIPMAP_LINEAR ||
          texture->minFilter == GL_LINEAR_MIPMAP_NEAREST)
        sampler.MaxLOD =
            texture->mips ? static_cast<float>(texture->levels - 1) : 0;
    }
    gpu.device->CreateShaderResourceView(texture ? texture->resource.Get()
                                                 : nullptr,
                                         &view, SrvCpu(first + slot));
  }
  std::string key(reinterpret_cast<const char *>(sampling.data()),
                  sizeof(sampling));
  auto table = samplerTables.find(key);
  if (table == samplerTables.end()) {
    if (samplerCursor + 8 > 2048)
      throw std::runtime_error("Sampler combinations exceeded capacity");
    const UINT index = samplerCursor;
    samplerCursor += 8;
    for (UINT i = 0; i < 8; ++i)
      gpu.device->CreateSampler(&sampling[i], SamplerCpu(index + i));
    auto handle = samplers->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * samplerStride;
    table = samplerTables.emplace(std::move(key), handle).first;
  }
  gpu.commands->SetGraphicsRootDescriptorTable(4, table->second);
  return Srv(first);
}
namespace {
D3D12_BLEND Blend(GLenum value) {
  switch (value) {
  case GL_ZERO:
    return D3D12_BLEND_ZERO;
  case GL_ONE:
    return D3D12_BLEND_ONE;
  case GL_SRC_ALPHA:
    return D3D12_BLEND_SRC_ALPHA;
  case GL_ONE_MINUS_SRC_ALPHA:
    return D3D12_BLEND_INV_SRC_ALPHA;
  case GL_SRC_COLOR:
    return D3D12_BLEND_SRC_COLOR;
  case GL_ONE_MINUS_SRC_COLOR:
    return D3D12_BLEND_INV_SRC_COLOR;
  case GL_DST_ALPHA:
    return D3D12_BLEND_DEST_ALPHA;
  case GL_ONE_MINUS_DST_ALPHA:
    return D3D12_BLEND_INV_DEST_ALPHA;
  default:
    throw std::runtime_error("Unsupported scene blend factor");
  }
}
} // namespace
Com<ID3D12PipelineState> SceneBridge::Pipeline(Program &program, GLenum mode) {
  std::ostringstream key;
  key << mode << '/' << Color().format << '/' << (Depth() != nullptr) << '/'
      << depthTest << depthMask << blend << cull << polygon << sourceRgb
      << destRgb << sourceAlpha << destAlpha;
  auto &vao = arrays[boundVao];
  auto inputs = program.inputs;
  for (auto &input : inputs) {
    const auto &attribute = vao.attributes[input.InputSlot];
    input.AlignedByteOffset = static_cast<UINT>(attribute.offset);
    key << '/' << attribute.offset;
  }
  const auto existing = program.pipelines.find(key.str());
  if (existing != program.pipelines.end())
    return existing->second;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
  p.pRootSignature = root.Get();
  p.VS = {program.vs.code->GetBufferPointer(),
          program.vs.code->GetBufferSize()};
  p.PS = {program.ps.code->GetBufferPointer(),
          program.ps.code->GetBufferSize()};
  if (mode == GL_POINTS && program.gs)
    p.GS = {program.gs->GetBufferPointer(), program.gs->GetBufferSize()};
  p.InputLayout = {inputs.data(), static_cast<UINT>(inputs.size())};
  p.RasterizerState.FillMode =
      polygon == GL_LINE ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  p.RasterizerState.CullMode =
      cull ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
  p.RasterizerState.DepthClipEnable = TRUE;
  p.DepthStencilState.DepthEnable = depthTest && Depth() != nullptr;
  p.DepthStencilState.DepthWriteMask =
      depthMask ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  p.DSVFormat = Depth() ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
  auto &b = p.BlendState.RenderTarget[0];
  b.BlendEnable = blend;
  b.SrcBlend = Blend(sourceRgb);
  b.DestBlend = Blend(destRgb);
  b.BlendOp = D3D12_BLEND_OP_ADD;
  b.SrcBlendAlpha = Blend(sourceAlpha);
  b.DestBlendAlpha = Blend(destAlpha);
  b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  p.NumRenderTargets = 1;
  p.RTVFormats[0] = Color().format;
  p.SampleMask = UINT_MAX;
  p.SampleDesc.Count = 1;
  p.PrimitiveTopologyType =
      mode == GL_POINTS ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
      : mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP
          ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
          : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  Com<ID3D12PipelineState> pipeline;
  const auto result =
      gpu.device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline));
  if (FAILED(result)) {
    gpu.ErrorCount();
    std::cerr << "Failed scene program=" << boundProgram
              << " format=" << p.RTVFormats[0] << "\n";
  }
  Check(result, "Create shared scene pipeline");
  program.pipelines.emplace(key.str(), pipeline);
  return pipeline;
}
void SceneBridge::Draw(GLenum mode, GLint first, GLsizei count,
                       GLsizei instances) {
  if (count <= 0 || instances <= 0)
    return;
  Begin();
  auto &program = programs[boundProgram];
  if (!program.linked)
    throw std::runtime_error("Drawing an unlinked scene shader: " +
                             program.log);
  BindTargets();
  ID3D12DescriptorHeap *heaps[] = {srvs.Get(), samplers.Get()};
  gpu.commands->SetDescriptorHeaps(2, heaps);
  gpu.commands->SetGraphicsRootSignature(root.Get());
  gpu.commands->SetGraphicsRootConstantBufferView(
      0, Constants(program.vs, program));
  gpu.commands->SetGraphicsRootConstantBufferView(
      1, Constants(program.ps, program));
  auto point = Upload(256);
  const float pointValues[] = {static_cast<float>(viewport[2]),
                               static_cast<float>(viewport[3]), pointSize, 0};
  std::memcpy(point.first, pointValues, sizeof(pointValues));
  gpu.commands->SetGraphicsRootConstantBufferView(2, point.second);
  gpu.commands->SetGraphicsRootDescriptorTable(3, BindTextures(program));
  gpu.commands->SetPipelineState(Pipeline(program, mode).Get());
  const D3D12_VIEWPORT vp{static_cast<float>(viewport[0]),
                          static_cast<float>(viewport[1]),
                          static_cast<float>(viewport[2]),
                          static_cast<float>(viewport[3]),
                          0,
                          1};
  D3D12_RECT rect{viewport[0], viewport[1], viewport[0] + viewport[2],
                  viewport[1] + viewport[3]};
  if (scissorTest) {
    rect.left = std::max<LONG>(rect.left, scissor[0]);
    rect.top = std::max<LONG>(rect.top, scissor[1]);
    rect.right = std::min<LONG>(rect.right, scissor[0] + scissor[2]);
    rect.bottom = std::min<LONG>(rect.bottom, scissor[1] + scissor[3]);
  }
  gpu.commands->RSSetViewports(1, &vp);
  gpu.commands->RSSetScissorRects(1, &rect);
  auto &vao = arrays[boundVao];
  for (const auto &input : program.inputs) {
    const auto &attribute = vao.attributes[input.InputSlot];
    D3D12_VERTEX_BUFFER_VIEW view{};
    if (attribute.enabled && buffers[attribute.buffer].resource) {
      const auto &buffer = buffers[attribute.buffer];
      view.BufferLocation = buffer.resource->GetGPUVirtualAddress();
      view.SizeInBytes = buffer.bytes;
      view.StrideInBytes =
          attribute.stride ? attribute.stride : attribute.components * 4;
    } else {
      view.BufferLocation = zeroBuffer->GetGPUVirtualAddress();
      view.SizeInBytes = 16;
      view.StrideInBytes = 0;
    }
    gpu.commands->IASetVertexBuffers(input.InputSlot, 1, &view);
  }
  const auto topology = mode == GL_POINTS  ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
                        : mode == GL_LINES ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
                        : mode == GL_LINE_STRIP || mode == GL_LINE_LOOP
                            ? D3D_PRIMITIVE_TOPOLOGY_LINESTRIP
                        : mode == GL_TRIANGLE_STRIP
                            ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
                            : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  gpu.commands->IASetPrimitiveTopology(topology);
  gpu.commands->DrawInstanced(count, instances, first, 0);
}

void SceneBridge::Blit(Texture &texture, UINT sourceLevel,
                       D3D12_CPU_DESCRIPTOR_HANDLE target, DXGI_FORMAT format,
                       UINT w, UINT h, bool flip) {
  Begin();
  if (!utilityRoot) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 1;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    Com<ID3DBlob> code, error;
    Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &code, &error),
          "Serialize image utility root");
    Check(gpu.device->CreateRootSignature(0, code->GetBufferPointer(),
                                          code->GetBufferSize(),
                                          IID_PPV_ARGS(&utilityRoot)),
          "Create image utility root");
  }
  auto pipeline = utilityPipelines.find(format);
  if (pipeline == utilityPipelines.end()) {
    const std::string source =
        R"(Texture2D<float4> image:register(t0);SamplerState sampling:register(s0);cbuffer Options:register(b0){float flip;};
struct Pixel{float4 position:SV_Position;float2 uv:TEXCOORD0;};
Pixel main(uint id:SV_VertexID){Pixel p;p.uv=float2((id<<1)&2,id&2);p.position=float4(p.uv*float2(2,-2)+float2(-1,1),0,1);return p;})";
    const std::string pixel =
        R"(Texture2D<float4> image:register(t0);SamplerState sampling:register(s0);cbuffer Options:register(b0){float flip;};
struct Pixel{float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 main(Pixel p):SV_Target{return image.SampleLevel(sampling,float2(p.uv.x,flip>0?1-p.uv.y:p.uv.y),0);})";
    const auto vs = Compile(source, "vs_5_1"), ps = Compile(pixel, "ps_5_1");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = utilityRoot.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    p.SampleMask = UINT_MAX;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = format;
    p.SampleDesc.Count = 1;
    Com<ID3D12PipelineState> created;
    Check(gpu.device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&created)),
          "Create image utility pipeline");
    pipeline = utilityPipelines.emplace(format, created).first;
  }
  State(texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, sourceLevel);
  const UINT descriptor = srvCursor++;
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = texture.format;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MostDetailedMip = sourceLevel;
  view.Texture2D.MipLevels = 1;
  gpu.device->CreateShaderResourceView(texture.resource.Get(), &view,
                                       SrvCpu(descriptor));
  ID3D12DescriptorHeap *heaps[] = {srvs.Get()};
  gpu.commands->SetDescriptorHeaps(1, heaps);
  gpu.commands->SetGraphicsRootSignature(utilityRoot.Get());
  const float value = flip ? 1.f : 0.f;
  gpu.commands->SetGraphicsRoot32BitConstants(0, 1, &value, 0);
  gpu.commands->SetGraphicsRootDescriptorTable(1, Srv(descriptor));
  gpu.commands->SetPipelineState(pipeline->second.Get());
  gpu.commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
  const D3D12_VIEWPORT vp{0, 0, static_cast<float>(w), static_cast<float>(h),
                          0, 1};
  const D3D12_RECT rect{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
  gpu.commands->RSSetViewports(1, &vp);
  gpu.commands->RSSetScissorRects(1, &rect);
  gpu.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  gpu.commands->DrawInstanced(3, 1, 0, 0);
}
void SceneBridge::GenerateMips(Texture &texture) {
  for (UINT level = 1; level < texture.levels; ++level) {
    State(texture, D3D12_RESOURCE_STATE_RENDER_TARGET, level);
    Blit(texture, level - 1, texture.rtvs[level], texture.format,
         std::max(1u, texture.width >> level),
         std::max(1u, texture.height >> level), false);
    State(texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, level);
  }
  texture.mips = true;
}
void SceneBridge::Present() {
  Begin();
  gpu.Transition(gpu.buffers[gpu.frameIndex].Get(),
                 D3D12_RESOURCE_STATE_PRESENT,
                 D3D12_RESOURCE_STATE_RENDER_TARGET);
  Blit(defaultColor, 0, gpu.Rtv(gpu.frameIndex), DXGI_FORMAT_R8G8B8A8_UNORM,
       gpu.width, gpu.height, true);
  gpu.Transition(gpu.buffers[gpu.frameIndex].Get(),
                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                 D3D12_RESOURCE_STATE_PRESENT);
  gpu.Submit(true, vsync);
  recording = false;
}
std::vector<unsigned char> SceneBridge::Read(Texture &texture, UINT level,
                                             UINT layer, UINT &pitch) {
  Begin();
  const auto description = texture.resource->GetDesc();
  const UINT sub = level + layer * texture.levels;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
  UINT64 total = 0;
  gpu.device->GetCopyableFootprints(&description, sub, 1, 0, &layout, nullptr,
                                    nullptr, &total);
  pitch = layout.Footprint.RowPitch;
  Com<ID3D12Resource> readback;
  auto heap = Heap(D3D12_HEAP_TYPE_READBACK);
  auto desc = BufferDesc(total);
  Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_COPY_DEST,
                                            nullptr, IID_PPV_ARGS(&readback)),
        "Create scene test readback");
  const auto prior = texture.states[sub];
  State(texture, D3D12_RESOURCE_STATE_COPY_SOURCE, level, layer);
  D3D12_TEXTURE_COPY_LOCATION from{}, to{};
  from.pResource = texture.resource.Get();
  from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  from.SubresourceIndex = sub;
  to.pResource = readback.Get();
  to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  to.PlacedFootprint = layout;
  gpu.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  State(texture, prior, level, layer);
  Flush();
  void *data = nullptr;
  D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
  Check(readback->Map(0, &range, &data), "Map scene test readback");
  std::vector<unsigned char> bytes(static_cast<size_t>(total));
  std::memcpy(bytes.data(), data, bytes.size());
  D3D12_RANGE empty{0, 0};
  readback->Unmap(0, &empty);
  return bytes;
}
namespace {
void Pixel(const unsigned char *source, DXGI_FORMAT format, float *result) {
  result[0] = result[1] = result[2] = 0;
  result[3] = 1;
  if (format == DXGI_FORMAT_R16G16B16A16_FLOAT)
    for (UINT c = 0; c < 4; ++c) {
      unsigned short value;
      std::memcpy(&value, source + c * 2, 2);
      result[c] = Unhalf(value);
    }
  else if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
    std::memcpy(result, source, 16);
  else if (format == DXGI_FORMAT_R32_TYPELESS)
    std::memcpy(result, source, 4);
  else
    for (UINT c = 0; c < 4; ++c)
      result[c] = source[c] / 255.f;
}
void StorePixel(unsigned char *destination, const float *values, UINT channels,
                GLenum type) {
  for (UINT c = 0; c < channels; ++c)
    if (type == GL_FLOAT)
      std::memcpy(destination + c * 4, values + c, 4);
    else
      destination[c] = static_cast<unsigned char>(
          std::clamp(values[c], 0.f, 1.f) * 255 + .5f);
}
} // namespace
void SceneBridge::GetTexture(GLenum target, GLint level, GLenum format,
                             GLenum type, void *output) {
  auto &texture = BoundTexture(target);
  UINT pitch = 0;
  const UINT layer = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                             target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6
                         ? target - GL_TEXTURE_CUBE_MAP_POSITIVE_X
                         : 0;
  const auto bytes = Read(texture, level, layer, pitch);
  const UINT channels = format == GL_DEPTH_COMPONENT ? 1
                        : format == GL_RGB           ? 3
                                                     : 4,
             component = type == GL_FLOAT ? 4 : 1,
             w = std::max(1u, texture.width >> level),
             h = std::max(1u, texture.height >> level),
             destinationPitch =
                 (w * channels * component + pack - 1) / pack * pack;
  for (UINT y = 0; y < h; ++y)
    for (UINT x = 0; x < w; ++x) {
      float values[4];
      Pixel(bytes.data() + static_cast<size_t>(y) * pitch +
                x * PixelBytes(texture.format),
            texture.format, values);
      StorePixel(static_cast<unsigned char *>(output) +
                     static_cast<size_t>(y) * destinationPitch +
                     x * channels * component,
                 values, channels, type);
    }
}
void SceneBridge::ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                             GLenum format, GLenum type, void *output) {
  auto &texture = format == GL_DEPTH_COMPONENT ? *Depth() : Color();
  const auto target = Target();
  UINT pitch = 0;
  const auto bytes =
      Read(texture, format == GL_DEPTH_COMPONENT ? 0 : target.level,
           format == GL_DEPTH_COMPONENT ? 0 : target.layer, pitch);
  const UINT channels = format == GL_DEPTH_COMPONENT ? 1
                        : format == GL_RGB           ? 3
                                                     : 4,
             component = type == GL_FLOAT ? 4 : 1,
             destinationPitch =
                 (w * channels * component + pack - 1) / pack * pack;
  for (int row = 0; row < h; ++row)
    for (int column = 0; column < w; ++column) {
      float values[4];
      Pixel(bytes.data() + static_cast<size_t>(row + y) * pitch +
                (column + x) * PixelBytes(texture.format),
            texture.format, values);
      StorePixel(static_cast<unsigned char *>(output) +
                     static_cast<size_t>(row) * destinationPitch +
                     column * channels * component,
                 values, channels, type);
    }
}

namespace {
void APIENTRY dglGenBuffers(GLsizei n, GLuint *ids) {
  for (int i = 0; i < n; ++i) {
    ids[i] = C().nextId++;
    C().buffers[ids[i]] = {};
  }
}
void APIENTRY dglBindBuffer(GLenum target, GLuint id) {
  if (target != GL_ARRAY_BUFFER)
    throw std::runtime_error("Unsupported scene buffer binding");
  C().boundBuffer = id;
}
void APIENTRY dglBufferData(GLenum, GLsizeiptr size, const void *data, GLenum) {
  C().BufferData(size, data);
}
void APIENTRY dglDeleteBuffers(GLsizei n, const GLuint *ids) {
  C().Flush();
  C().gpu.WaitIdle();
  for (int i = 0; i < n; ++i)
    C().buffers.erase(ids[i]);
}
void APIENTRY dglGenVertexArrays(GLsizei n, GLuint *ids) {
  for (int i = 0; i < n; ++i) {
    ids[i] = C().nextId++;
    C().arrays[ids[i]] = {};
  }
}
void APIENTRY dglBindVertexArray(GLuint id) { C().boundVao = id; }
void APIENTRY dglDeleteVertexArrays(GLsizei n, const GLuint *ids) {
  for (int i = 0; i < n; ++i)
    C().arrays.erase(ids[i]);
}
void APIENTRY dglEnableVertexAttribArray(GLuint index) {
  C().arrays[C().boundVao].attributes.at(index).enabled = true;
}
void APIENTRY dglVertexAttribPointer(GLuint index, GLint components,
                                     GLenum type, GLboolean, GLsizei stride,
                                     const void *offset) {
  if (type != GL_FLOAT)
    throw std::runtime_error("Unsupported scene attribute type");
  auto &a = C().arrays[C().boundVao].attributes.at(index);
  a.buffer = C().boundBuffer;
  a.components = components;
  a.stride = stride;
  a.offset = reinterpret_cast<size_t>(offset);
}
GLuint APIENTRY dglCreateShader(GLenum type) {
  const auto id = C().nextId++;
  C().shaders[id].type = type;
  return id;
}
void APIENTRY dglShaderSource(GLuint id, GLsizei count,
                              const GLchar *const *strings,
                              const GLint *lengths) {
  auto &source = C().shaders[id].source;
  source.clear();
  for (int i = 0; i < count; ++i)
    source.append(strings[i], lengths && lengths[i] >= 0
                                  ? lengths[i]
                                  : std::strlen(strings[i]));
}
void APIENTRY dglCompileShader(GLuint id) { C().shaders[id].compiled = true; }
void APIENTRY dglGetShaderiv(GLuint id, GLenum query, GLint *value) {
  *value = query == GL_COMPILE_STATUS
               ? C().shaders[id].compiled
               : static_cast<GLint>(C().shaders[id].log.size() + 1);
}
void Log(const std::string &text, GLsizei size, GLsizei *length,
         GLchar *output) {
  const auto count = std::min<size_t>(text.size(), size > 0 ? size - 1 : 0);
  if (size > 0) {
    std::memcpy(output, text.data(), count);
    output[count] = 0;
  }
  if (length)
    *length = static_cast<GLsizei>(count);
}
void APIENTRY dglGetShaderInfoLog(GLuint id, GLsizei size, GLsizei *length,
                                  GLchar *output) {
  Log(C().shaders[id].log, size, length, output);
}
void APIENTRY dglDeleteShader(GLuint id) { C().shaders.erase(id); }
GLuint APIENTRY dglCreateProgram() {
  const auto id = C().nextId++;
  C().programs[id] = {};
  return id;
}
void APIENTRY dglAttachShader(GLuint program, GLuint shader) {
  C().programs[program].shaders.push_back(shader);
}
void APIENTRY dglLinkProgram(GLuint program) {
  C().Link(C().programs[program]);
}
void APIENTRY dglGetProgramiv(GLuint id, GLenum query, GLint *value) {
  *value = query == GL_LINK_STATUS
               ? C().programs[id].linked
               : static_cast<GLint>(C().programs[id].log.size() + 1);
}
void APIENTRY dglGetProgramInfoLog(GLuint id, GLsizei size, GLsizei *length,
                                   GLchar *output) {
  Log(C().programs[id].log, size, length, output);
}
void APIENTRY dglDeleteProgram(GLuint id) {
  C().Flush();
  C().gpu.WaitIdle();
  C().programs.erase(id);
}
void APIENTRY dglUseProgram(GLuint id) { C().boundProgram = id; }
GLint APIENTRY dglGetUniformLocation(GLuint id, const GLchar *name) {
  auto &p = C().programs[id];
  const auto found = p.locations.find(name);
  if (found != p.locations.end())
    return found->second;
  const auto location = static_cast<GLint>(p.uniformNames.size());
  p.uniformNames.push_back(name);
  p.locations[name] = location;
  return location;
}
void APIENTRY dglUniform1i(GLint location, GLint x) {
  C().SetUniform(location, &x, 4);
}
void APIENTRY dglUniform1f(GLint location, GLfloat x) {
  C().SetUniform(location, &x, 4);
}
void APIENTRY dglUniform2f(GLint location, GLfloat x, GLfloat y) {
  const float v[] = {x, y};
  C().SetUniform(location, v, 8);
}
void APIENTRY dglUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
  const float v[] = {x, y, z};
  C().SetUniform(location, v, 12);
}
void APIENTRY dglUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z,
                           GLfloat w) {
  const float v[] = {x, y, z, w};
  C().SetUniform(location, v, 16);
}
void APIENTRY dglUniform3fv(GLint location, GLsizei count,
                            const GLfloat *value) {
  C().SetUniform(location, value, count * 12);
}
void APIENTRY dglUniform4fv(GLint location, GLsizei count,
                            const GLfloat *value) {
  C().SetUniform(location, value, count * 16);
}
void APIENTRY dglUniformMatrix3fv(GLint location, GLsizei count,
                                  GLboolean transpose, const GLfloat *value) {
  if (transpose)
    throw std::runtime_error("Unexpected transposed scene matrix");
  C().SetUniform(location, value, count * 36);
}
void APIENTRY dglUniformMatrix4fv(GLint location, GLsizei count,
                                  GLboolean transpose, const GLfloat *value) {
  if (transpose)
    throw std::runtime_error("Unexpected transposed scene matrix");
  C().SetUniform(location, value, count * 64);
}
void APIENTRY dglGenTextures(GLsizei n, GLuint *ids) {
  for (int i = 0; i < n; ++i) {
    ids[i] = C().nextId++;
    C().textures[ids[i]] = {};
  }
}
void APIENTRY dglActiveTexture(GLenum unit) {
  C().activeUnit = unit - GL_TEXTURE0;
  if (C().activeUnit >= C().units.size())
    throw std::runtime_error("Texture unit exceeded scene budget");
}
void APIENTRY dglBindTexture(GLenum target, GLuint id) {
  if (target == GL_TEXTURE_CUBE_MAP)
    C().units[C().activeUnit].cube = id;
  else
    C().units[C().activeUnit].image = id;
}
void APIENTRY dglTexImage2D(GLenum target, GLint level, GLint internal,
                            GLsizei w, GLsizei h, GLint, GLenum format,
                            GLenum type, const void *data) {
  C().TexImage(target, level, internal, w, h, format, type, data);
}
void APIENTRY dglTexParameteri(GLenum target, GLenum key, GLint value) {
  auto &t = C().BoundTexture(target);
  if (key == GL_TEXTURE_MIN_FILTER)
    t.minFilter = value;
  else if (key == GL_TEXTURE_MAG_FILTER)
    t.magFilter = value;
  else if (key == GL_TEXTURE_WRAP_S)
    t.wrapS = value;
  else if (key == GL_TEXTURE_WRAP_T)
    t.wrapT = value;
}
void APIENTRY dglGenerateMipmap(GLenum target) {
  C().GenerateMips(C().BoundTexture(target));
}
void APIENTRY dglCopyTexSubImage2D(GLenum target, GLint level, GLint dx,
                                   GLint dy, GLint sx, GLint sy, GLsizei w,
                                   GLsizei h) {
  C().CopyTexture(target, level, dx, dy, sx, sy, w, h);
}
void APIENTRY dglGetTexImage(GLenum target, GLint level, GLenum format,
                             GLenum type, void *data) {
  C().GetTexture(target, level, format, type, data);
}
void APIENTRY dglGetTexLevelParameteriv(GLenum target, GLint level,
                                        GLenum query, GLint *value) {
  const auto &t = C().BoundTexture(target);
  *value = query == GL_TEXTURE_WIDTH    ? std::max(1u, t.width >> level)
           : query == GL_TEXTURE_HEIGHT ? std::max(1u, t.height >> level)
                                        : 0;
}
void APIENTRY dglDeleteTextures(GLsizei n, const GLuint *ids) {
  C().Flush();
  C().gpu.WaitIdle();
  for (int i = 0; i < n; ++i) {
    auto found = C().textures.find(ids[i]);
    if (found != C().textures.end()) {
      C().RecycleViews(found->second);
      C().textures.erase(found);
    }
  }
}
void APIENTRY dglGenFramebuffers(GLsizei n, GLuint *ids) {
  for (int i = 0; i < n; ++i) {
    ids[i] = C().nextId++;
    C().framebuffers[ids[i]] = {};
  }
}
void APIENTRY dglBindFramebuffer(GLenum, GLuint id) { C().boundFbo = id; }
void APIENTRY dglFramebufferTexture2D(GLenum, GLenum, GLenum target, GLuint id,
                                      GLint level) {
  C().framebuffers[C().boundFbo].color = {
      id, static_cast<UINT>(level),
      target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
              target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6
          ? target - GL_TEXTURE_CUBE_MAP_POSITIVE_X
          : 0};
}
GLenum APIENTRY dglCheckFramebufferStatus(GLenum) {
  return C().boundFbo && (!C().Color().resource)
             ? GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
             : GL_FRAMEBUFFER_COMPLETE;
}
void APIENTRY dglDeleteFramebuffers(GLsizei n, const GLuint *ids) {
  for (int i = 0; i < n; ++i)
    C().framebuffers.erase(ids[i]);
}
void APIENTRY dglGenRenderbuffers(GLsizei n, GLuint *ids) {
  for (int i = 0; i < n; ++i) {
    ids[i] = C().nextId++;
    C().renderbuffers[ids[i]] = {};
  }
}
void APIENTRY dglBindRenderbuffer(GLenum, GLuint id) {
  C().boundRenderbuffer = id;
}
void APIENTRY dglRenderbufferStorage(GLenum, GLenum, GLsizei w, GLsizei h) {
  C().Allocate(C().renderbuffers[C().boundRenderbuffer], w, h,
               DXGI_FORMAT_R32_TYPELESS, true);
}
void APIENTRY dglFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint id) {
  C().framebuffers[C().boundFbo].depth = id;
}
void APIENTRY dglDeleteRenderbuffers(GLsizei n, const GLuint *ids) {
  C().Flush();
  C().gpu.WaitIdle();
  for (int i = 0; i < n; ++i) {
    auto found = C().renderbuffers.find(ids[i]);
    if (found != C().renderbuffers.end()) {
      C().RecycleViews(found->second);
      C().renderbuffers.erase(found);
    }
  }
}
void APIENTRY dglViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
  C().viewport = {x, y, w, h};
}
void APIENTRY dglScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
  C().scissor = {x, y, w, h};
}
void APIENTRY dglClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  C().clearColor = {r, g, b, a};
}
void APIENTRY dglClear(GLbitfield bits) { C().Clear(bits); }
void Enable(GLenum capability, bool enabled) {
  if (capability == GL_DEPTH_TEST)
    C().depthTest = enabled;
  else if (capability == GL_BLEND)
    C().blend = enabled;
  else if (capability == GL_CULL_FACE)
    C().cull = enabled;
  else if (capability == GL_SCISSOR_TEST)
    C().scissorTest = enabled;
  else
    throw std::runtime_error("Unsupported scene state");
}
void APIENTRY dglEnable(GLenum capability) { Enable(capability, true); }
void APIENTRY dglDisable(GLenum capability) { Enable(capability, false); }
GLboolean APIENTRY dglIsEnabled(GLenum capability) {
  return capability == GL_DEPTH_TEST     ? C().depthTest
         : capability == GL_BLEND        ? C().blend
         : capability == GL_CULL_FACE    ? C().cull
         : capability == GL_SCISSOR_TEST ? C().scissorTest
                                         : false;
}
void APIENTRY dglDepthMask(GLboolean value) { C().depthMask = value != 0; }
void APIENTRY dglBlendFunc(GLenum source, GLenum destination) {
  C().sourceRgb = C().sourceAlpha = source;
  C().destRgb = C().destAlpha = destination;
}
void APIENTRY dglBlendFuncSeparate(GLenum source, GLenum destination,
                                   GLenum sourceAlpha,
                                   GLenum destinationAlpha) {
  C().sourceRgb = source;
  C().destRgb = destination;
  C().sourceAlpha = sourceAlpha;
  C().destAlpha = destinationAlpha;
}
void APIENTRY dglPolygonMode(GLenum, GLenum mode) { C().polygon = mode; }
void APIENTRY dglPointSize(GLfloat size) { C().pointSize = size; }
void APIENTRY dglDrawArrays(GLenum mode, GLint first, GLsizei count) {
  C().Draw(mode, first, count, 1);
}
void APIENTRY dglDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                     GLsizei instances) {
  C().Draw(mode, first, count, instances);
}
void APIENTRY dglPixelStorei(GLenum key, GLint value) {
  if (key == GL_PACK_ALIGNMENT)
    C().pack = value;
  else if (key == GL_UNPACK_ALIGNMENT)
    C().unpack = value;
}
void APIENTRY dglReadBuffer(GLenum value) { C().readBuffer = value; }
void APIENTRY dglReadPixels(GLint x, GLint y, GLsizei w, GLsizei h,
                            GLenum format, GLenum type, void *output) {
  C().ReadPixels(x, y, w, h, format, type, output);
}
void APIENTRY dglFinish() {
  C().Flush();
  C().gpu.WaitIdle();
}
void APIENTRY dglGetIntegerv(GLenum query, GLint *value) {
  auto &c = C();
  switch (query) {
  case GL_VIEWPORT:
    std::copy(c.viewport.begin(), c.viewport.end(), value);
    break;
  case GL_ACTIVE_TEXTURE:
    *value = GL_TEXTURE0 + c.activeUnit;
    break;
  case GL_CURRENT_PROGRAM:
    *value = c.boundProgram;
    break;
  case GL_VERTEX_ARRAY_BINDING:
    *value = c.boundVao;
    break;
  case GL_FRAMEBUFFER_BINDING:
    *value = c.boundFbo;
    break;
  case GL_POLYGON_MODE:
    value[0] = value[1] = c.polygon;
    break;
  case GL_TEXTURE_BINDING_2D:
    *value = c.units[c.activeUnit].image;
    break;
  case GL_READ_BUFFER:
    *value = c.readBuffer;
    break;
  case GL_BLEND_SRC_RGB:
    *value = c.sourceRgb;
    break;
  case GL_BLEND_DST_RGB:
    *value = c.destRgb;
    break;
  case GL_BLEND_SRC_ALPHA:
    *value = c.sourceAlpha;
    break;
  case GL_BLEND_DST_ALPHA:
    *value = c.destAlpha;
    break;
  default:
    *value = 0;
    throw std::runtime_error("Unsupported scene state query " +
                             std::to_string(query));
  }
}
void APIENTRY dglGetBooleanv(GLenum query, GLboolean *value) {
  if (query == GL_DEPTH_WRITEMASK)
    *value = C().depthMask;
  else
    *value = dglIsEnabled(query);
}
void APIENTRY dglGetFloatv(GLenum query, GLfloat *value) {
  if (query == GL_COLOR_CLEAR_VALUE)
    std::copy(C().clearColor.begin(), C().clearColor.end(), value);
  else
    *value = 0;
}
GLenum APIENTRY dglGetError() {
  const auto error = C().error;
  C().error = GL_NO_ERROR;
  return error;
}
} // namespace
void ConfigureSceneBridge(bool validation, bool warp) {
  sceneValidation = validation;
  sceneWarp = warp;
}
void InitializeSceneBridge(void *window) {
  bridge = std::make_unique<SceneBridge>();
  bridge->Initialize(static_cast<HWND>(window));
#define INSTALL(name) glad_##name = &d##name
  INSTALL(glGenBuffers);
  INSTALL(glBindBuffer);
  INSTALL(glBufferData);
  INSTALL(glDeleteBuffers);
  INSTALL(glGenVertexArrays);
  INSTALL(glBindVertexArray);
  INSTALL(glDeleteVertexArrays);
  INSTALL(glEnableVertexAttribArray);
  INSTALL(glVertexAttribPointer);
  INSTALL(glCreateShader);
  INSTALL(glShaderSource);
  INSTALL(glCompileShader);
  INSTALL(glGetShaderiv);
  INSTALL(glGetShaderInfoLog);
  INSTALL(glDeleteShader);
  INSTALL(glCreateProgram);
  INSTALL(glAttachShader);
  INSTALL(glLinkProgram);
  INSTALL(glGetProgramiv);
  INSTALL(glGetProgramInfoLog);
  INSTALL(glDeleteProgram);
  INSTALL(glUseProgram);
  INSTALL(glGetUniformLocation);
  INSTALL(glUniform1i);
  INSTALL(glUniform1f);
  INSTALL(glUniform2f);
  INSTALL(glUniform3f);
  INSTALL(glUniform4f);
  INSTALL(glUniform3fv);
  INSTALL(glUniform4fv);
  INSTALL(glUniformMatrix3fv);
  INSTALL(glUniformMatrix4fv);
  INSTALL(glGenTextures);
  INSTALL(glActiveTexture);
  INSTALL(glBindTexture);
  INSTALL(glTexImage2D);
  INSTALL(glTexParameteri);
  INSTALL(glGenerateMipmap);
  INSTALL(glCopyTexSubImage2D);
  INSTALL(glGetTexImage);
  INSTALL(glGetTexLevelParameteriv);
  INSTALL(glDeleteTextures);
  INSTALL(glGenFramebuffers);
  INSTALL(glBindFramebuffer);
  INSTALL(glFramebufferTexture2D);
  INSTALL(glCheckFramebufferStatus);
  INSTALL(glDeleteFramebuffers);
  INSTALL(glGenRenderbuffers);
  INSTALL(glBindRenderbuffer);
  INSTALL(glRenderbufferStorage);
  INSTALL(glFramebufferRenderbuffer);
  INSTALL(glDeleteRenderbuffers);
  INSTALL(glViewport);
  INSTALL(glScissor);
  INSTALL(glClearColor);
  INSTALL(glClear);
  INSTALL(glEnable);
  INSTALL(glDisable);
  INSTALL(glIsEnabled);
  INSTALL(glDepthMask);
  INSTALL(glBlendFunc);
  INSTALL(glBlendFuncSeparate);
  INSTALL(glPolygonMode);
  INSTALL(glPointSize);
  INSTALL(glDrawArrays);
  INSTALL(glDrawArraysInstanced);
  INSTALL(glPixelStorei);
  INSTALL(glReadBuffer);
  INSTALL(glReadPixels);
  INSTALL(glFinish);
  INSTALL(glGetIntegerv);
  INSTALL(glGetBooleanv);
  INSTALL(glGetFloatv);
  INSTALL(glGetError);
#undef INSTALL
  std::cout
      << "Native DX12 shared-scene backend initialized; no OpenGL context\n";
}
void PresentScene() { C().Present(); }
void SetSceneSwapInterval(int interval) { C().vsync = interval != 0; }
unsigned SceneValidationErrors() {
  C().Flush();
  C().gpu.WaitIdle();
  return C().gpu.ErrorCount();
}
void ReleaseSceneBridge() { bridge.reset(); }
} // namespace NativeDX12
