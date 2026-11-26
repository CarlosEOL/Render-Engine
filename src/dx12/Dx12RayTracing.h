#pragma once

#include "Dx12SceneBridge.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <functional>
#include <vector>

namespace NativeDX12 {

class SurfaceRayTracing {
public:
    struct Upload {
        void* data;
        ID3D12Resource* resource;
        UINT64 offset;
        D3D12_GPU_VIRTUAL_ADDRESS address;
    };
    static constexpr UINT maxGeometry = 96;
    static constexpr UINT counterBytes = maxGeometry * sizeof(UINT64);
    bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commands);
    bool Available() const { return pipeline != nullptr; }
    bool Start(UINT frameIndex, const RayView& view, const Upload& zero);
    D3D12_STREAM_OUTPUT_BUFFER_VIEW Append(UINT vertices, const RayMaterial& material);
    bool Resolve(const std::function<Upload(UINT64)>& allocateUpload,
                 ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* sky, ID3D12Resource* normals, ID3D12Resource* output,
                 ID3D12Resource* samples, ID3D12Resource* guides,
                 ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE descriptors,
                 D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptors, UINT descriptorStride,
                 glm::uvec4 viewport);
    void Cancel() { capturing = false; }
    bool Capturing() const { return capturing; }
    bool HasGeometry() const { return !geometries.empty(); }
    unsigned long long PassCount() const { return passes; }
    unsigned GeometryCount(RayKind kind) const { return tracedKinds[static_cast<unsigned>(kind)]; }

private:
    template<class T> using Com = Microsoft::WRL::ComPtr<T>;
    struct Geometry {
        UINT vertices, firstVertex;
        RayMaterial material;
    };
    struct Frame {
        Com<ID3D12Resource> stream, scratch, bottom, top;
        D3D12_RESOURCE_STATES streamState = D3D12_RESOURCE_STATE_COPY_DEST;
    };
    std::array<Frame, 2> frames;
    Frame* frame = nullptr;
    Com<ID3D12Device5> device;
    Com<ID3D12GraphicsCommandList4> commands;
    Com<ID3D12RootSignature> root;
    Com<ID3D12PipelineState> pipeline;
    RayView view;
    std::vector<Geometry> geometries;
    std::array<unsigned, 6> tracedKinds{};
    UINT vertexCount = 0;
    unsigned long long passes = 0;
    bool capturing = false;
};

} // namespace NativeDX12
