#include "DeferredRenderingPipeline.h"

#include "ShaderManager.h"

namespace dxpg
{


struct MVP_NORMAL_CB
{
	Matrix4x4 ModelViewProjection;
	Matrix4x4 NormalMatrix;
};

struct MVP_CB
{
	Matrix4x4 ModelViewProjection;
};

namespace StaticPipelineConsts
{
	constexpr const char* ModelViewProjectionCB = "ModelViewProjectionCB";
	constexpr const char* MaterialInfo = "MaterialInfo";
	constexpr const char* DiffuseSRV = "DiffuseSRV";
	constexpr const char* NormalMapSRV = "NormalMapSRV";
}

namespace ShadowMapPipelineConsts
{
	constexpr const char* ModelViewProjectionCB = "ModelViewProjectionCB";
}

namespace LightingPipelineConsts
{
	struct TransformationMatrices
	{
		Matrix4x4 LightViewProjection;
		Matrix4x4 CamInverseView;
		Matrix4x4 CamInverseProjection;
	};
	constexpr const char* LightCB = "LightCB";
	constexpr const char* TransformationMatricesSTR = "TransformationMatrices";
	constexpr const char* GBuffers = "GBuffers";
	constexpr const char* ShadowMap = "ShadowMap";
}

bool DeferredRenderingPipeline::Setup(ID3D12Device2* dev)
{
	Device = dev;
	ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
	SetupStaticMeshPipeline();
	SetupShadowMapPipeline();
	SetupLightingPipeline();

	return true;
}

void DeferredRenderingPipeline::OnResize(uint32_t width, uint32_t height)
{
	Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
	DXTexture::TextureCreateInfo depthBufferInfo
	{
		.Width = width,
		.Height = height,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_D32_FLOAT,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
		.ClearValue = D3D12_CLEAR_VALUE{.Format = DXGI_FORMAT_D32_FLOAT, .DepthStencil = {.Depth = 1.0f}}
	};
	DepthBuffer = DXTexture::Create(Device, L"DepthBuffer", depthBufferInfo, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DXTexture::TextureCreateInfo albedoBufferInfo
	{
		.Width = width,
		.Height = height,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
		.ClearValue = D3D12_CLEAR_VALUE{.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, .Color = {0, 0, 0, 1}}
	};
	AlbedoBuffer = DXTexture::Create(Device, L"AlbedoBuffer", albedoBufferInfo);
	DXTexture::TextureCreateInfo normalBufferInfo
	{
		.Width = width,
		.Height = height,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
		.ClearValue = D3D12_CLEAR_VALUE{.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .Color = {0, 0, 0, 1}}
	};
	NormalBuffer = DXTexture::Create(Device, L"NormalBuffer", normalBufferInfo);
	DXTexture::TextureCreateInfo outputBufferInfo
	{
		.Width = width,
		.Height = height,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
		.ClearValue = D3D12_CLEAR_VALUE{.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, .Color = {0, 0, 0, 1}}
	};
	OutputBuffer = DXTexture::Create(Device, L"OutputBuffer", outputBufferInfo);

	// Create RTVs and DSVs
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	DepthBuffer.CreatePlacedDSV(DepthBufferDSV.GetView(), &dsvDesc);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	AlbedoBuffer.CreatePlacedRTV(AlbedoBufferRTV.GetView(), &rtvDesc);

	rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	NormalBuffer.CreatePlacedRTV(NormalBufferRTV.GetView(), &rtvDesc);

	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	OutputBuffer.CreatePlacedRTV(OutputBufferRTV.GetView(), &rtvDesc);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	OutputBuffer.CreatePlacedSRV(OutputBufferSRV.GetView(), &srvDesc);
	AlbedoBuffer.CreatePlacedSRV(GBuffersSRV.GetView(), &srvDesc);
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	NormalBuffer.CreatePlacedSRV(GBuffersSRV.GetView(1), &srvDesc);
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	DepthBuffer.CreatePlacedSRV(GBuffersSRV.GetView(2), &srvDesc);
}

void DeferredRenderingPipeline::Run(ID3D12GraphicsCommandList2* cmd, ViewData const& viewData, SceneDataView const& scene, FrameContext& frameCtx)
{
	RunStaticMeshPipeline(cmd, viewData, scene);
	RunShadowMapPipeline(cmd, scene);
	RunLightingPipeline(cmd, viewData, scene, frameCtx); 
}
bool DeferredRenderingPipeline::SetupStaticMeshPipeline()
{
	RootSignatureBuilder builder{};

	std::vector< CD3DX12_ROOT_PARAMETER1> rootParams;
	builder.AddConstants(StaticPipelineConsts::ModelViewProjectionCB, sizeof(MVP_NORMAL_CB) / 4, { .ShaderRegister = 0, .Visibility = D3D12_SHADER_VISIBILITY_VERTEX });
	builder.AddDescriptorTable(StaticPipelineConsts::MaterialInfo, { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1) } }, D3D12_SHADER_VISIBILITY_PIXEL);
	builder.AddDescriptorTable(StaticPipelineConsts::DiffuseSRV, { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0) } }, D3D12_SHADER_VISIBILITY_PIXEL);
	builder.AddDescriptorTable(StaticPipelineConsts::NormalMapSRV, { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1) } }, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.MaxAnisotropy = 16;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	builder.AddStaticSampler(staticSampler);

	StaticMeshRootSignature = builder.Build("StaticMeshRS", Device, rootSignatureFlags);

	struct StaticMeshPipelineStateStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
	} pipelineStateStream;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	auto* vertexShader = ShaderManager::Get().CompileShader(L"Triangle.vs", DXPG_SHADERS_DIR L"Vertex/StaticMesh.vs.hlsl", ShaderType::Vertex);
	auto* pixelShader = ShaderManager::Get().CompileShader(L"Triangle.ps", DXPG_SHADERS_DIR L"Pixel/StaticMesh.ps.hlsl", ShaderType::Pixel);

	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShader->Blob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader->Blob.Get());

	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 2;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvFormats.RTFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	StaticMeshPipelineState = PipelineState::Create("StaticMeshPipeline", Device, pipelineStateStream, &StaticMeshRootSignature);
	return true;
}

bool DeferredRenderingPipeline::SetupShadowMapPipeline()
{
	RootSignatureBuilder builder{};
	builder.AddConstants(ShadowMapPipelineConsts::ModelViewProjectionCB, sizeof(MVP_CB) / 4, { .ShaderRegister = 0, .Visibility = D3D12_SHADER_VISIBILITY_VERTEX });
	ShadowMapRootSignature = builder.Build("ShadowMapRS", Device, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	struct ShadowMapPipelineStateStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
	} pipelineStateStream;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(ShaderManager::Get().CompileShader(L"ShadowMap.vs", DXPG_SHADERS_DIR L"Vertex/ShadowMap.vs.hlsl", ShaderType::Vertex)->Blob.Get());

	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	ShadowMapPipelineState = PipelineState::Create("ShadowMapPipeline", Device, pipelineStateStream, &ShadowMapRootSignature);
	ShadowMap = DXTexture::Create(Device, L"ShadowMap", {
		.Width = 1024,
		.Height = 1024,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_D32_FLOAT,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
		.ClearValue = D3D12_CLEAR_VALUE{.Format = DXGI_FORMAT_D32_FLOAT, .DepthStencil = {.Depth = 1.0f}}
		});
	ShadowMapViewport = CD3DX12_VIEWPORT(0.0f, 0.0f, ShadowMap.Info.Width, ShadowMap.Info.Height);
	ShadowMapDSV = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	ShadowMapSRV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	ShadowMap.CreatePlacedDSV(ShadowMapDSV.GetView(), &dsvDesc);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	ShadowMap.CreatePlacedSRV(ShadowMapSRV.GetView(), &srvDesc);
	return true;
}



bool DeferredRenderingPipeline::SetupLightingPipeline()
{
	RootSignatureBuilder builder{};
	builder.AddDescriptorTable(LightingPipelineConsts::GBuffers, { {CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0)} }, D3D12_SHADER_VISIBILITY_PIXEL);
	builder.AddConstantBufferView(LightingPipelineConsts::LightCB, { .ShaderRegister = 0, .Visibility = D3D12_SHADER_VISIBILITY_PIXEL, .DescFlags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE });
	builder.AddConstantBufferView(LightingPipelineConsts::TransformationMatricesSTR, { .ShaderRegister = 1, .Visibility = D3D12_SHADER_VISIBILITY_PIXEL, .DescFlags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE });
	builder.AddDescriptorTable(LightingPipelineConsts::ShadowMap, { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3) } }, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	staticSampler.MaxAnisotropy = 0;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	builder.AddStaticSampler(staticSampler);
	CD3DX12_STATIC_SAMPLER_DESC shadowSampler(1);
	shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	builder.AddStaticSampler(shadowSampler);

	LightingRootSignature = builder.Build("LightingRS", Device, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	struct LightingPipelineStateStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
	} pipelineStateStream;

	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(ShaderManager::Get().CompileShader(L"Fullscreen.vs", DXPG_SHADERS_DIR L"Vertex/Fullscreen.vs.hlsl", ShaderType::Vertex)->Blob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ShaderManager::Get().CompileShader(L"Lighting.ps", DXPG_SHADERS_DIR L"Pixel/Lighting.ps.hlsl", ShaderType::Pixel)->Blob.Get());

	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 1;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	LightingPipelineState = PipelineState::Create("LightingPipeline", Device, pipelineStateStream, &LightingRootSignature);

	LightBuffer = DXBuffer::Create(Device, L"LightBuffer", sizeof(LightData), D3D12_HEAP_TYPE_DEFAULT);
	LightTransformationMatricesBuffer = DXBuffer::Create(Device, L"LightTransformationMatricesBuffer", sizeof(LightingPipelineConsts::TransformationMatrices), D3D12_HEAP_TYPE_DEFAULT);

	DepthBufferDSV = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	AlbedoBufferRTV = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	NormalBufferRTV = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	OutputBufferRTV = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	GBuffersSRV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
	OutputBufferSRV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	return true;
}

void DeferredRenderingPipeline::RunStaticMeshPipeline(ID3D12GraphicsCommandList2* cmd, ViewData const& viewData, SceneDataView const& scene)
{
	// Transition Resources
	cmd->RSSetViewports(1, &Viewport);
	cmd->RSSetScissorRects(1, &ScissorRect);
	{
		TransitionVec(AlbedoBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET)
			.Add(NormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET)
			.Add(DepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE)
			.Execute(cmd);

		// Clear Render Targets
		auto rtv = AlbedoBufferRTV.GetCPUHandle();
		auto rtv2 = NormalBufferRTV.GetCPUHandle();
		auto dsv = DepthBufferDSV.GetCPUHandle();
		float clearColor[] = { 0, 0, 0, 1 };
		cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		cmd->ClearRenderTargetView(rtv2, clearColor, 0, nullptr);
		//cmd->ClearRenderTargetView(OutputBufferRTV->GetCPUHandle(), clearColor, 0, nullptr);
		cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	// Set Render Targets
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv[2] = { AlbedoBufferRTV.GetCPUHandle(), NormalBufferRTV.GetCPUHandle() };
		auto dsv = DepthBufferDSV.GetCPUHandle();
		cmd->OMSetRenderTargets(2, rtv, FALSE, &dsv);
	}

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->SetPipelineState(StaticMeshPipelineState.DXPipelineState.Get());
	cmd->SetGraphicsRootSignature(StaticMeshPipelineState.RootSignature->DXSignature.Get());

	Renderable lastRenderableCfg{};
	for (auto& renderable : scene.RenderableList)
	{
		if (lastRenderableCfg.VertexBufferView.BufferLocation != renderable.VertexBufferView.BufferLocation)
		{
			lastRenderableCfg.VertexBufferView = renderable.VertexBufferView;
			cmd->IASetVertexBuffers(0, 1, &renderable.VertexBufferView);
		}

		if (lastRenderableCfg.MaterialInfo.ptr != renderable.MaterialInfo.ptr)
		{
			lastRenderableCfg.MaterialInfo = renderable.MaterialInfo;
			cmd->SetGraphicsRootDescriptorTable(StaticMeshPipelineState.RootSignature->NameToParameterIndices[StaticPipelineConsts::MaterialInfo], renderable.MaterialInfo);
		}
		if (renderable.DiffuseTextureSRV.ptr != 0)
		{
			if (lastRenderableCfg.DiffuseTextureSRV.ptr != renderable.DiffuseTextureSRV.ptr)
			{
				lastRenderableCfg.DiffuseTextureSRV = renderable.DiffuseTextureSRV;
				cmd->SetGraphicsRootDescriptorTable(StaticMeshPipelineState.RootSignature->NameToParameterIndices[StaticPipelineConsts::DiffuseSRV], renderable.DiffuseTextureSRV);
			}
		}
		if (renderable.NormalMapTextureSRV.ptr != 0)
		{
			if (lastRenderableCfg.NormalMapTextureSRV.ptr != renderable.NormalMapTextureSRV.ptr)
			{
				lastRenderableCfg.NormalMapTextureSRV = renderable.NormalMapTextureSRV;
				cmd->SetGraphicsRootDescriptorTable(StaticMeshPipelineState.RootSignature->NameToParameterIndices[StaticPipelineConsts::NormalMapSRV], renderable.NormalMapTextureSRV);
			}
		}

		if (lastRenderableCfg.IndexBufferView.BufferLocation != renderable.IndexBufferView.BufferLocation)
		{
			lastRenderableCfg.IndexBufferView = renderable.IndexBufferView;
			cmd->IASetIndexBuffer(&renderable.IndexBufferView);
		}
		MVP_NORMAL_CB mvp{};
		mvp.ModelViewProjection = XMMatrixMultiply(renderable.GlobalModelMatrix, viewData.ViewProjection);
		mvp.NormalMatrix = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, renderable.GlobalModelMatrix));
		cmd->SetGraphicsRoot32BitConstants(StaticMeshPipelineState.RootSignature->NameToParameterIndices[StaticPipelineConsts::ModelViewProjectionCB], sizeof(MVP_NORMAL_CB) / sizeof(uint32_t), &mvp, 0);
		cmd->DrawIndexedInstanced(renderable.GetIndexCount(), 1, 0, 0, 0);
	}
}
void DeferredRenderingPipeline::RunShadowMapPipeline(ID3D12GraphicsCommandList2* cmd, SceneDataView const& scene)
{
	// Transition Resources
	cmd->RSSetViewports(1, &ShadowMapViewport);
	TransitionVec(ShadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE).Execute(cmd);

	// Clear Render Targets
	{
		auto dsv = ShadowMapDSV.GetCPUHandle();
		cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	// Set Render Targets
	{
		auto dsv = ShadowMapDSV.GetCPUHandle();
		cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
	}

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->SetPipelineState(ShadowMapPipelineState.DXPipelineState.Get());
	cmd->SetGraphicsRootSignature(ShadowMapPipelineState.RootSignature->DXSignature.Get());

	Renderable lastRenderableCfg{};
	for (auto& renderable : scene.RenderableList)
	{
		if (lastRenderableCfg.VertexBufferView.BufferLocation != renderable.VertexBufferView.BufferLocation)
		{
			lastRenderableCfg.VertexBufferView = renderable.VertexBufferView;
			cmd->IASetVertexBuffers(0, 1, &renderable.VertexBufferView);
		}
		if (lastRenderableCfg.IndexBufferView.BufferLocation != renderable.IndexBufferView.BufferLocation)
		{
			lastRenderableCfg.IndexBufferView = renderable.IndexBufferView;
			cmd->IASetIndexBuffer(&renderable.IndexBufferView);
		}
		MVP_CB mvp{};
		mvp.ModelViewProjection = XMMatrixMultiply(renderable.GlobalModelMatrix, scene.LightView.ViewProjection);
		cmd->SetGraphicsRoot32BitConstants(ShadowMapPipelineState.RootSignature->NameToParameterIndices[ShadowMapPipelineConsts::ModelViewProjectionCB], sizeof(MVP_CB) / sizeof(uint32_t), &mvp, 0);
		cmd->DrawIndexedInstanced(renderable.GetIndexCount(), 1, 0, 0, 0);
	}
}
void DeferredRenderingPipeline::RunLightingPipeline(ID3D12GraphicsCommandList2* cmd, ViewData const& viewData, SceneDataView const& scene, FrameContext& frameCtx)
{
	cmd->RSSetViewports(1, &Viewport);

	TransitionVec{}.Add(AlbedoBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		.Add(DepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		.Add(NormalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		.Add(ShadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		.Add(OutputBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET)
		.Execute(cmd);

	// Set Render Targets
	{
		auto rtv = OutputBufferRTV.GetCPUHandle();
		cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	}

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	LightingPipelineState.Bind(cmd);

	// Update Light Data
	{
		TransitionVec(LightBuffer, D3D12_RESOURCE_STATE_COPY_DEST).Execute(cmd);
		// Update Light Buffer
		constexpr size_t paramCount = sizeof(LightData) / sizeof(UINT);
		D3D12_WRITEBUFFERIMMEDIATE_PARAMETER params[paramCount] = {};
		for (size_t i = 0; i < paramCount; i++)
		{
			params[i].Dest = LightBuffer.GPUAddress(i * sizeof(UINT));
			params[i].Value = reinterpret_cast<const UINT*>(&scene.Light)[i];
		}
		cmd->WriteBufferImmediate(paramCount, params, nullptr);
		TransitionVec(LightBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER).Execute(cmd);
	}
	cmd->SetGraphicsRootConstantBufferView(LightingPipelineState.RootSignature->NameToParameterIndices[LightingPipelineConsts::LightCB], LightBuffer.GPUAddress());
	// Update Light Transformation Matrices
	{
		TransitionVec(LightTransformationMatricesBuffer, D3D12_RESOURCE_STATE_COPY_DEST).Execute(cmd);
		LightingPipelineConsts::TransformationMatrices matrices{};
		matrices.LightViewProjection = scene.LightView.ViewProjection;
		matrices.CamInverseView = DirectX::XMMatrixInverse(nullptr, viewData.View);
		matrices.CamInverseProjection = DirectX::XMMatrixInverse(nullptr, viewData.Projection);

		// Update Light Buffer
		constexpr size_t paramCount = sizeof(matrices) / sizeof(UINT);
		D3D12_WRITEBUFFERIMMEDIATE_PARAMETER params[paramCount] = {};
		for (size_t i = 0; i < paramCount; i++)
		{
			params[i].Dest = LightTransformationMatricesBuffer.GPUAddress(i * sizeof(UINT));
			params[i].Value = reinterpret_cast<const UINT*>(&matrices)[i];
		}
		cmd->WriteBufferImmediate(paramCount, params, nullptr);
		TransitionVec(LightTransformationMatricesBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER).Execute(cmd);
	}
	cmd->SetGraphicsRootConstantBufferView(LightingPipelineState.RootSignature->NameToParameterIndices[LightingPipelineConsts::TransformationMatricesSTR], LightTransformationMatricesBuffer.GPUAddress());
	cmd->SetGraphicsRootDescriptorTable(LightingPipelineState.RootSignature->NameToParameterIndices[LightingPipelineConsts::GBuffers], GBuffersSRV.GetGPUHandle());
	cmd->SetGraphicsRootDescriptorTable(LightingPipelineState.RootSignature->NameToParameterIndices[LightingPipelineConsts::ShadowMap], ShadowMapSRV.GetGPUHandle());

	cmd->DrawInstanced(4, 1, 0, 0);
}
}