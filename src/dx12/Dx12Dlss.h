#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;

namespace NativeDX12 {
enum class DlssMode { Off, Quality, Balanced, Performance, DLAA };
// NGX owns its feature/history; the caller owns resources and GPU fences.
class Dlss {
public:
  Dlss();
  ~Dlss();
  void Initialize(ID3D12Device* device);
  bool Prepare(ID3D12GraphicsCommandList* commands, DlssMode mode,
               unsigned width, unsigned height);
  bool Evaluate(ID3D12GraphicsCommandList* commands, ID3D12Resource* color,
                ID3D12Resource* depth, ID3D12Resource* motion,
                ID3D12Resource* output, glm::vec2 jitter, bool reset, float dt);
  void ReleaseFeature(); // Call only after the caller's queue is idle.
  bool Available() const;
  glm::uvec2 InputSize() const;
  const std::string& Status() const;
  unsigned long long Evaluations() const;
  unsigned long long Resets() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace NativeDX12
