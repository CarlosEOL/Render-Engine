#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Dx12RayTracing.h"
#include "SurfaceRays.generated.h"
#include "../Astronomy.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

namespace NativeDX12 {
namespace {
constexpr UINT capacity = 2250000;
constexpr unsigned primary=1, reflection=2, transmission=4, shadow=8, indirect=16;

unsigned Channels(RayKind kind) {
    switch (kind) {
    case RayKind::Surface: return primary | reflection | shadow | indirect;
    case RayKind::Bottom: return transmission;
    case RayKind::Solid: return primary | reflection | transmission | shadow | indirect;
    case RayKind::Submerged: return primary | transmission | shadow | indirect;
    case RayKind::Glass: return primary | reflection;
    default: return 0;
    }
}

unsigned Channels(const RayMaterial& material) {
    // A fully liquid surface has no opaque secondary hits.
    if(material.kind==RayKind::Surface && material.capturesWaterNormal && !material.visibleInReflections)
        return primary;
    return Channels(material.kind) & (material.visibleInReflections ? ~0u : ~reflection);
}

bool EnsureBuffer(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
                  UINT64 bytes, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
    if (buffer && buffer->GetDesc().Width >= bytes) return true;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<UINT64>(256, (bytes + 255) & ~UINT64(255));
    desc.Height = 1;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        state, nullptr, IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf())));
}

struct GpuMaterial {
    glm::vec3 color;
    float reflectivity;
    unsigned firstVertex;
    float unitScale;
    unsigned kind;
};
static_assert(sizeof(GpuMaterial) == 28);

struct GpuView {
    glm::mat4 projection, inverseProjection;
    glm::vec4 eyeScale, lightRange;
    glm::uvec4 viewport, options;
    glm::vec4 sky;
};
static_assert(sizeof(GpuView) == 208);

void UseAs(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
           D3D12_RESOURCE_STATES& before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
    before = after;
}

void CompleteBuild(ID3D12GraphicsCommandList* commands, ID3D12Resource* destination, ID3D12Resource* scratch) {
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = destination;
    barriers[1].UAV.pResource = scratch;
    commands->ResourceBarrier(2, barriers);
}
}

bool SurfaceRayTracing::Initialize(ID3D12Device* adapter, ID3D12GraphicsCommandList* list) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 features{};
    D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_5};
    if (FAILED(adapter->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features))) ||
        features.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
        FAILED(adapter->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader, sizeof(shader))) ||
        shader.HighestShaderModel < D3D_SHADER_MODEL_6_5 ||
        FAILED(adapter->QueryInterface(IID_PPV_ARGS(&device))) ||
        FAILED(list->QueryInterface(IID_PPV_ARGS(&commands)))) return false;

    D3D12_ROOT_PARAMETER parameters[6]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    for (UINT i = 0; i < 3; ++i) {
        parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[i + 1].Descriptor.ShaderRegister = i;
    }
    D3D12_DESCRIPTOR_RANGE ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 3, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 4}
    };
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[4].DescriptorTable = {2, ranges};
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[5].Constants = {1,0,1};
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 6;
    desc.pParameters = parameters;
    D3D12_STATIC_SAMPLER_DESC skySampler{};
    skySampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    skySampler.AddressU = skySampler.AddressV = skySampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    skySampler.MaxAnisotropy = 1;
    skySampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &skySampler;
    Com<ID3DBlob> serialized;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr)) ||
        FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC definition{};
    definition.pRootSignature = root.Get();
    definition.CS = {SurfaceRayShader, sizeof(SurfaceRayShader)};
    return SUCCEEDED(device->CreateComputePipelineState(&definition, IID_PPV_ARGS(&pipeline)));
}

bool SurfaceRayTracing::Start(UINT frameIndex, const RayView& settings, const Upload& zero) {
    if (!Available() || capturing) return false;
    frame = &frames[frameIndex];
    if (!EnsureBuffer(device.Get(), frame->stream, counterBytes + UINT64(capacity) * vertexStride,
        frame->streamState, D3D12_RESOURCE_FLAG_NONE)) return false;
    UseAs(commands.Get(), frame->stream.Get(), frame->streamState, D3D12_RESOURCE_STATE_COPY_DEST);
    std::memset(zero.data, 0, counterBytes);
    commands->CopyBufferRegion(frame->stream.Get(), 0, zero.resource, zero.offset, counterBytes);
    UseAs(commands.Get(), frame->stream.Get(), frame->streamState, D3D12_RESOURCE_STATE_STREAM_OUT);
    geometries.clear();
    vertexCount = 0;
    view = settings;
    capturing = true;
    return true;
}

D3D12_STREAM_OUTPUT_BUFFER_VIEW SurfaceRayTracing::Append(UINT vertices, const RayMaterial& material, bool liquidMask) {
    if (!capturing || !vertices || vertices > capacity - vertexCount || geometries.size() >= maxGeometry)
        return {};
    const UINT64 start = frame->stream->GetGPUVirtualAddress();
    const D3D12_STREAM_OUTPUT_BUFFER_VIEW output{start + counterBytes + UINT64(vertexCount) * vertexStride,
        UINT64(vertices) * vertexStride, start + geometries.size() * sizeof(UINT64)};
    geometries.push_back({vertices, vertexCount, material, liquidMask});
    vertexCount += vertices;
    return output;
}

bool SurfaceRayTracing::Resolve(const std::function<Upload(UINT64)>& allocateUpload,
    ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* sky, ID3D12Resource* normals, ID3D12Resource* output,
    ID3D12Resource* samples, ID3D12Resource* guides,
    ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE descriptors,
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptors, UINT descriptorStride, glm::uvec4 viewport) {
    if (!capturing) return false;
    capturing = false;
    if (geometries.empty() || viewport.z == 0 || viewport.w == 0) return false;

    // Route ray types through the hardware instance mask. Dry geometry can
    // use fixed-function traversal while mixed land/water is checked at the hit.
    std::sort(geometries.begin(),geometries.end(),[](const Geometry& a,const Geometry& b) {
        return Channels(a.material)<Channels(b.material);
    });

    auto matrices = allocateUpload(geometries.size() * sizeof(float) * 12);
    auto materialData = allocateUpload(geometries.size() * sizeof(GpuMaterial));
    auto* transform = static_cast<float*>(matrices.data);
    auto* material = static_cast<GpuMaterial*>(materialData.data);
    const auto vertexAddress = frame->stream->GetGPUVirtualAddress() + counterBytes;
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> triangles(geometries.size());
    tracedKinds.fill(0);
    for (UINT i = 0; i < triangles.size(); ++i) {
        const auto& g = geometries[i];
        const float scale = g.material.space == RaySpace::RelativeAu ? static_cast<float>(Astronomy::metresPerAu) : 1.f;
        const auto translation = g.material.space == RaySpace::World ? -view.eye : glm::vec3(0);
        const std::array<float, 12> matrix{scale,0,0,translation.x, 0,scale,0,translation.y, 0,0,scale,translation.z};
        std::memcpy(transform + i * 12, matrix.data(), sizeof(matrix));
        const auto kind = static_cast<unsigned>(g.material.kind);
        material[i] = {g.material.color, g.material.reflectivity, g.firstVertex, scale, kind};
        ++tracedKinds[kind];
        auto& geo = triangles[i];
        geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geo.Flags = g.liquidMask ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geo.Triangles.Transform3x4 = matrices.address + i * sizeof(matrix);
        geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geo.Triangles.VertexCount = g.vertices;
        geo.Triangles.VertexBuffer = {vertexAddress + UINT64(g.firstVertex) * vertexStride, vertexStride};
    }
    struct BottomGroup {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        UINT first, mask;
        UINT64 offset;
    };
    std::vector<BottomGroup> groups;
    UINT64 bottomSize=0, scratchSize=0;
    constexpr UINT64 alignment=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    for (UINT first=0;first<geometries.size();) {
        const UINT mask=Channels(geometries[first].material);
        UINT next=first+1;
        while(next<geometries.size() && Channels(geometries[next].material)==mask)++next;
        BottomGroup group{{},first,mask,bottomSize};
        auto& inputs=group.build.Inputs;
        inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs=next-first;
        inputs.pGeometryDescs=triangles.data()+first;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bytes{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,&bytes);
        if(!bytes.ResultDataMaxSizeInBytes)return false;
        bottomSize+=(bytes.ResultDataMaxSizeInBytes+alignment-1)&~(alignment-1);
        scratchSize=std::max(scratchSize,bytes.ScratchDataSizeInBytes);
        groups.push_back(group);
        first=next;
    }
    auto instances=allocateUpload(groups.size()*sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    auto* models=static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(instances.data);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{};
    top.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    top.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Inputs.NumDescs = static_cast<UINT>(groups.size());
    top.Inputs.InstanceDescs = instances.address;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topBytes{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&top.Inputs, &topBytes);
    const auto uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto acceleration = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if (!topBytes.ResultDataMaxSizeInBytes ||
        !EnsureBuffer(device.Get(), frame->bottom, bottomSize, acceleration, uav) ||
        !EnsureBuffer(device.Get(), frame->top, topBytes.ResultDataMaxSizeInBytes, acceleration, uav) ||
        !EnsureBuffer(device.Get(), frame->scratch,
            std::max(scratchSize, topBytes.ScratchDataSizeInBytes),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uav)) return false;
    top.DestAccelerationStructureData = frame->top->GetGPUVirtualAddress();
    top.ScratchAccelerationStructureData = frame->scratch->GetGPUVirtualAddress();
    UseAs(commands.Get(), frame->stream.Get(), frame->streamState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (size_t i=0;i<groups.size();++i) {
        auto& group=groups[i];
        auto& model=models[i];
        model={};
        model.Transform[0][0]=model.Transform[1][1]=model.Transform[2][2]=1;
        model.InstanceID=group.first;
        model.InstanceMask=group.mask;
        model.AccelerationStructure=frame->bottom->GetGPUVirtualAddress()+group.offset;
        group.build.DestAccelerationStructureData=model.AccelerationStructure;
        group.build.ScratchAccelerationStructureData=top.ScratchAccelerationStructureData;
        commands->BuildRaytracingAccelerationStructure(&group.build,0,nullptr);
        CompleteBuild(commands.Get(),frame->bottom.Get(),frame->scratch.Get());
    }
    commands->BuildRaytracingAccelerationStructure(&top, 0, nullptr);
    CompleteBuild(commands.Get(), frame->top.Get(), frame->scratch.Get());

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = color->GetDesc().Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(color, &srv, descriptors);
    descriptors.ptr += descriptorStride;
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    device->CreateShaderResourceView(depth, &srv, descriptors);
    descriptors.ptr += descriptorStride;
    srv.Format = sky ? sky->GetDesc().Format : DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv.TextureCube.MipLevels = 1;
    device->CreateShaderResourceView(sky, &srv, descriptors);
    descriptors.ptr += descriptorStride;
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(normals, &srv, descriptors);
    descriptors.ptr += descriptorStride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC target{};
    target.Format = output->GetDesc().Format;
    target.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(output, nullptr, &target, descriptors);
    descriptors.ptr += descriptorStride;
    device->CreateUnorderedAccessView(samples, nullptr, &target, descriptors);
    descriptors.ptr += descriptorStride;
    device->CreateUnorderedAccessView(guides, nullptr, &target, descriptors);

    const float scale = view.logarithmic ? static_cast<float>(Astronomy::metresPerAu) : 1.f;
    const GpuView config{view.viewProjection, glm::inverse(view.viewProjection), glm::vec4(view.eye, scale),
        glm::vec4(view.light, view.range), viewport,
        glm::uvec4((view.logarithmic ? 1u : 0u) | (sky ? 2u : 0u), view.underwater, view.reflection, view.refraction),view.sky};
    const auto constants = allocateUpload(sizeof(config));
    std::memcpy(constants.data, &config, sizeof(config));
    commands->SetDescriptorHeaps(1, &heap);
    commands->SetPipelineState(pipeline.Get());
    commands->SetComputeRootSignature(root.Get());
    commands->SetComputeRootConstantBufferView(0, constants.address);
    commands->SetComputeRootShaderResourceView(1, top.DestAccelerationStructureData);
    commands->SetComputeRootShaderResourceView(2, vertexAddress);
    commands->SetComputeRootShaderResourceView(3, materialData.address);
    commands->SetComputeRootDescriptorTable(4, gpuDescriptors);
    commands->SetComputeRoot32BitConstant(5, 0, 0);
    commands->Dispatch((viewport.z+7)/8, (viewport.w+7)/8, 1);
    D3D12_RESOURCE_BARRIER ready[3]{};
    ID3D12Resource* produced[] = {output,samples,guides};
    for (unsigned i=0;i<3;++i) {
        ready[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        ready[i].UAV.pResource = produced[i];
    }
    commands->ResourceBarrier(3,ready);
    commands->SetComputeRoot32BitConstant(5, 1, 0);
    commands->Dispatch((viewport.z+7)/8, (viewport.w+7)/8, 1);
    ++passes;
    return true;
}

} // namespace NativeDX12
