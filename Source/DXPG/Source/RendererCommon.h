#pragma once

#include "DXHelpers.h"

namespace dxpg
{

struct HLSL_VertexData
{
    uint32_t PositionIdx;
    uint32_t NormalIdx;
    uint32_t TexCoordIdx;
};

struct ViewData
{
    Matrix4x4 View;
    Matrix4x4 Projection;
	Matrix4x4 ViewProjection;
	Vector4 Position;
	Vector4 Direction;
};

struct FrameContext
{
    bool Ready = false;
    ComPtr<ID3D12CommandAllocator> CommandAllocator;
    UINT64                  FenceValue;
    std::unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, DescriptorHeapPage*> GPUHeapPages = {};
    std::unordered_map<DescriptorAllocation*, DescriptorAllocation> CPUViewsToGPUViews = {};

	std::vector<ComPtr<ID3D12Resource>> IntermediateResources;

    DescriptorAllocation GetGPUAllocation(DescriptorAllocation* cpuAllocation)
    {
        assert(cpuAllocation);
        auto it = CPUViewsToGPUViews.find(cpuAllocation);
        if (it == CPUViewsToGPUViews.end())
            return CPUViewsToGPUViews[cpuAllocation] = GPUHeapPages[cpuAllocation->Heap->Desc.Type]->CopyFrom(cpuAllocation);
        return it->second;
    }
};

struct Renderable
{
    std::string Name;
    D3D12_GPU_DESCRIPTOR_HANDLE MaterialInfo;
	D3D12_GPU_DESCRIPTOR_HANDLE DiffuseTextureSRV;
    D3D12_GPU_DESCRIPTOR_HANDLE VertexSRV;
    D3D12_VERTEX_BUFFER_VIEW IndicesView;
    Matrix4x4 GlobalModelMatrix;

	__forceinline uint32_t GetIndexCount() const
	{
		return IndicesView.SizeInBytes / sizeof(HLSL_VertexData);
	}
};

struct LightData
{
    union
    {
        struct
        {
            Vector3 Direction;
            float Padding;
        } Directional;
        struct
        {
            Vector3 Position;
            float Padding;
        } Point;
    };
    Vector3 Color;
    float Intensity;
    Vector3 AmbientColor;
    int Type;
};

struct SceneDataView
{
    std::vector<Renderable> RenderableList;
    LightData Light;
    ViewData LightView;
};

}