#include "BlitPipeline.h"
#include "ShaderManager.h"
#include "DXResource.h"
namespace dxpg
{
bool BlitPipeline::Setup(ID3D12Device2* dev)
{
	Device = dev;

	RootSignatureBuilder builder{};
	builder.AddDescriptorTable("SourceSRV", { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0) } }, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(0);
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	staticSampler.MaxAnisotropy = 0;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	builder.AddStaticSampler(staticSampler);
	RootSignature = builder.Build("BlitRS", Device, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	struct BlitPipelineStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
	} pipelineStateStream;

	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(ShaderManager::Get().CompileShader(L"Fullscreen.vs", DXPG_SHADERS_DIR L"Vertex/Fullscreen.vs.hlsl", ShaderType::Vertex)->Blob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ShaderManager::Get().CompileShader(L"Blit.ps", DXPG_SHADERS_DIR L"Pixel/Blit.ps.hlsl", ShaderType::Pixel)->Blob.Get());

	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 1;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	PipelineState = PipelineState::Create("BlitPipeline", Device, pipelineStateStream, &RootSignature);

	return true;
}

void BlitPipeline::Blit(ID3D12GraphicsCommandList2* cmdList, struct DXTexture* dstTex, struct DXTexture* srcTex, D3D12_CPU_DESCRIPTOR_HANDLE dstRTV, D3D12_GPU_DESCRIPTOR_HANDLE srcSRV)
{
	D3D12_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(dstTex->Info.Width);
	viewport.Height = static_cast<float>(dstTex->Info.Height);
	cmdList->RSSetViewports(1, &viewport);
	TransitionVec{}.Add(*dstTex, D3D12_RESOURCE_STATE_RENDER_TARGET)
		.Add(*srcTex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		.Execute(cmdList);

	PipelineState.Bind(cmdList);

	cmdList->OMSetRenderTargets(1, &dstRTV, FALSE, nullptr);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	cmdList->SetGraphicsRootDescriptorTable(0, srcSRV);

	cmdList->DrawInstanced(4, 1, 0, 0);
}

}
