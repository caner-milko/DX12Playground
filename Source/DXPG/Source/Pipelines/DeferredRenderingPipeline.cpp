#include "DeferredRenderingPipeline.h"

#include "ShaderManager.h"

#include "RenderResources.hlsli"
#include "ConstantBuffers.hlsli"

namespace dxpg
{

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

	auto [vertexShader, pixelShader] = ShaderManager::Get().CompileBindlessGraphicsShader(L"Triangle", DXPG_SHADERS_DIR L"Graphics/StaticMesh.hlsl");
	
	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShader->Blob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader->Blob.Get());

	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 2;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvFormats.RTFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	StaticMeshPipelineState = PipelineState::Create("StaticMeshPipeline", Device, pipelineStateStream, &ShaderManager::Get().BindlessRootSignature);
	return true;
}

bool DeferredRenderingPipeline::SetupShadowMapPipeline()
{
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

	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(ShaderManager::Get().CompileBindlessVertexShader(L"ShadowMap", DXPG_SHADERS_DIR L"Graphics/Shadowmap.hlsl")->Blob.Get());

	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	ShadowMapPipelineState = PipelineState::Create("ShadowMapPipeline", Device, pipelineStateStream, &ShaderManager::Get().BindlessRootSignature);
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
	ShadowMapSampler = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

	//D3D12_FILTER filter = D3D12_FILTER_ANISOTROPIC,
	//	D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	//	D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	//	D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	//	FLOAT mipLODBias = 0,
	//	UINT maxAnisotropy = 16,
	//	D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
	//	D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
	//	FLOAT minLOD = 0.f,
	//	FLOAT maxLOD = D3D12_FLOAT32_MAX,
	//	D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
	//	UINT registerSpace = 0

	D3D12_SAMPLER_DESC shadowSampler{};
	shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.MipLODBias = 0;
	shadowSampler.MaxAnisotropy = 16;
	shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	memset(shadowSampler.BorderColor, std::bit_cast<int>(1.0f), sizeof(shadowSampler.BorderColor));
	shadowSampler.MinLOD = 0;
	shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
	
	Device->CreateSampler(&shadowSampler, ShadowMapSampler.GetCPUHandle());
	
	return true;
}

bool DeferredRenderingPipeline::SetupLightingPipeline()
{

	struct LightingPipelineStateStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
	} pipelineStateStream;

	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	auto [vertexShader, pixelShader] = ShaderManager::Get().CompileBindlessGraphicsShader(L"Lighting", DXPG_SHADERS_DIR L"Graphics/Lighting.hlsl");
	
	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShader->Blob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader->Blob.Get());

	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 1;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	LightingPipelineState = PipelineState::Create("LightingPipeline", Device, pipelineStateStream, &ShaderManager::Get().BindlessRootSignature);

	LightBuffer = DXBuffer::Create(Device, L"LightBuffer", sizeof(dxpg::hlsl::LightDataBuffer), D3D12_HEAP_TYPE_DEFAULT);
	LightBufferCBV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	LightBuffer.CreatePlacedCBV(LightBufferCBV.GetView());


	LightTransformationMatricesBuffer = DXBuffer::Create(Device, L"LightTransformationMatricesBuffer", sizeof(dxpg::hlsl::LightTransformBuffer), D3D12_HEAP_TYPE_DEFAULT);
	LightTransformationMatricesBufferCBV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	LightTransformationMatricesBuffer.CreatePlacedCBV(LightTransformationMatricesBufferCBV.GetView());

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
	StaticMeshPipelineState.Bind(cmd);

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
		dxpg::hlsl::StaticMeshResources staticMeshResources{};
		staticMeshResources.MVP = XMMatrixMultiply(renderable.GlobalModelMatrix, viewData.ViewProjection);
		staticMeshResources.Normal = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, renderable.GlobalModelMatrix));
		staticMeshResources.MaterialBufferIndex = renderable.MaterialBufferIndex;
		cmd->SetGraphicsRoot32BitConstants(0, sizeof(staticMeshResources) / 4, &staticMeshResources, 0);
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
	ShadowMapPipelineState.Bind(cmd);

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
		dxpg::hlsl::ShadowMapResources shadowMapResources{};
		shadowMapResources.MVP = XMMatrixMultiply(renderable.GlobalModelMatrix, scene.LightView.ViewProjection);
		cmd->SetGraphicsRoot32BitConstants(0, sizeof(shadowMapResources) / 4, &shadowMapResources, 0);
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
		constexpr size_t paramCount = sizeof(dxpg::hlsl::LightDataBuffer) / sizeof(UINT);
		D3D12_WRITEBUFFERIMMEDIATE_PARAMETER params[paramCount] = {};
		for (size_t i = 0; i < paramCount; i++)
		{
			params[i].Dest = LightBuffer.GPUAddress(i * sizeof(UINT));
			params[i].Value = reinterpret_cast<const UINT*>(&scene.Light)[i];
		}
		cmd->WriteBufferImmediate(paramCount, params, nullptr);
		TransitionVec(LightBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER).Execute(cmd);
	}

	// Update Light Transformation Matrices
	{
		TransitionVec(LightTransformationMatricesBuffer, D3D12_RESOURCE_STATE_COPY_DEST).Execute(cmd);
		dxpg::hlsl::LightTransformBuffer lighTransform{};
		lighTransform.LightViewProjection = scene.LightView.ViewProjection;
		lighTransform.CamInverseView = DirectX::XMMatrixInverse(nullptr, viewData.View);
		lighTransform.CamInverseProjection = DirectX::XMMatrixInverse(nullptr, viewData.Projection);

		// Update Light Buffer
		constexpr size_t paramCount = sizeof(lighTransform) / sizeof(UINT);
		D3D12_WRITEBUFFERIMMEDIATE_PARAMETER params[paramCount] = {};
		for (size_t i = 0; i < paramCount; i++)
		{
			params[i].Dest = LightTransformationMatricesBuffer.GPUAddress(i * sizeof(UINT));
			params[i].Value = reinterpret_cast<const UINT*>(&lighTransform)[i];
		}
		cmd->WriteBufferImmediate(paramCount, params, nullptr);
		TransitionVec(LightTransformationMatricesBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER).Execute(cmd);
	}
	dxpg::hlsl::LightingResources lightingResources{};
	lightingResources.AlbedoTextureIndex = GBuffersSRV.Index;
	lightingResources.NormalTextureIndex = GBuffersSRV.GetView(1).GetIndex();
	lightingResources.DepthTextureIndex = GBuffersSRV.GetView(2).GetIndex();
	lightingResources.ShadowMapTextureIndex = ShadowMapSRV.Index;
	lightingResources.ShadowMapSamplerIndex = ShadowMapSampler.Index;
	lightingResources.LightDataBufferIndex = LightBufferCBV.Index;
	lightingResources.LightTransformBufferIndex = LightTransformationMatricesBufferCBV.Index;

	cmd->SetGraphicsRoot32BitConstants(0, sizeof(lightingResources) / 4, &lightingResources, 0);

	cmd->DrawInstanced(4, 1, 0, 0);
}
}