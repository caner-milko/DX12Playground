#pragma once

#include "RootSignature.h"
#include "PipelineState.h"

#include "RendererCommon.h"
#include "DXResource.h"

namespace dxpg
{
struct DeferredRenderingPipeline
{
	bool Setup(ID3D12Device2* dev);
	void OnResize(uint32_t width, uint32_t height);

	void Run(ID3D12GraphicsCommandList2* cmd, ViewData const& viewData, SceneDataView const& scene, FrameContext& frameCtx);

	DXTexture& GetOutputBuffer() { return OutputBuffer; }
	DescriptorAllocationView GetOutputBufferSRV() { return OutputBufferSRV.GetView(); }
	DXTexture& GetShadowMap() { return ShadowMap; }
	DescriptorAllocationView GetShadowMapSRV() { return ShadowMapSRV.GetView(); }
	DXTexture& GetAlbedoBuffer() { return AlbedoBuffer; }
	DescriptorAllocationView GetAlbedoBufferSRV() { return GBuffersSRV.GetView(); }
	DXTexture& GetNormalBuffer() { return NormalBuffer; }
	DescriptorAllocationView GetNormalBufferSRV() { return GBuffersSRV.GetView(1); }
private:
	bool SetupStaticMeshPipeline();
	bool SetupLightingPipeline();
	bool SetupShadowMapPipeline();

	void RunStaticMeshPipeline(ID3D12GraphicsCommandList2* cmd, ViewData const& viewData, SceneDataView const& scene);
	void RunShadowMapPipeline(ID3D12GraphicsCommandList2* cmd, SceneDataView const& scene);
	void RunLightingPipeline(ID3D12GraphicsCommandList2* cmd, ViewData const& viewData, SceneDataView const& scene, FrameContext& frameCtx);



	ID3D12Device2* Device;
	PipelineState StaticMeshPipelineState;

	DXTexture DepthBuffer;
	DXTexture AlbedoBuffer;
	DXTexture NormalBuffer;

	DescriptorAllocation DepthBufferDSV;
	DescriptorAllocation AlbedoBufferRTV;
	DescriptorAllocation NormalBufferRTV;
	DescriptorAllocation GBuffersSRV;

	PipelineState ShadowMapPipelineState;
	DXTexture ShadowMap;
	DescriptorAllocation ShadowMapDSV;
	DescriptorAllocation ShadowMapSRV;
	DescriptorAllocation ShadowMapSampler;

	DXBuffer LightBuffer;
	DescriptorAllocation LightBufferCBV;
	DXBuffer LightTransformationMatricesBuffer;
	DescriptorAllocation LightTransformationMatricesBufferCBV;

	PipelineState LightingPipelineState;
	DXTexture OutputBuffer;
	DescriptorAllocation OutputBufferRTV;
	DescriptorAllocation OutputBufferSRV;

	D3D12_VIEWPORT ShadowMapViewport;
	D3D12_VIEWPORT Viewport;
	D3D12_RECT ScissorRect;
};

}