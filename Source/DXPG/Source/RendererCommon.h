#pragma once

#include "DXHelpers.h"
#include "RenderResources.hlsli"
#include "ConstantBuffers.hlsli"

namespace dxpg
{
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
    uint32_t MaterialBufferIndex;
	uint32_t DiffuseTextureIndex;
	uint32_t NormalMapTextureIndex;
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
    D3D12_INDEX_BUFFER_VIEW IndexBufferView;
    Matrix4x4 GlobalModelMatrix;

	__forceinline uint32_t GetIndexCount() const
	{
		return IndexBufferView.SizeInBytes / sizeof(uint32_t);
	}
};

struct SceneDataView
{
    std::vector<Renderable> RenderableList;
    dxpg::hlsl::LightDataBuffer Light;
    ViewData LightView;
};

}