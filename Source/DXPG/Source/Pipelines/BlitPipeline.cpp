#include "BlitPipeline.h"
#include "ShaderManager.h"
#include "DXResource.h"
namespace dxpg
{
bool BlitPipeline::Setup(ID3D12Device2* dev)
{
	Device = dev;

	struct BlitPipelineStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
	} pipelineStateStream;

	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	auto [vertexShader, pixelShader] = ShaderManager::Get().CompileBindlessGraphicsShader(L"Blit", DXPG_SHADERS_DIR L"Graphics/Blit.hlsl");

	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShader->Blob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShader->Blob.Get());

	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 1;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineStateStream.RTVFormats = rtvFormats;

	pipelineStateStream.Rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	PipelineState = PipelineState::Create("BlitPipeline", Device, pipelineStateStream, &ShaderManager::Get().BindlessRootSignature);
	return true;
}

void BlitPipeline::Blit(ID3D12GraphicsCommandList2* cmdList, struct DXTexture* dstTex, struct DXTexture* srcTex, D3D12_CPU_DESCRIPTOR_HANDLE dstRTV, uint32_t srcSRVIndex)
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

	dxpg::hlsl::BlitResources blitResources{};
	blitResources.SourceTextureIndex = srcSRVIndex;
	cmdList->SetGraphicsRoot32BitConstants(0, sizeof(blitResources) / 4, &blitResources, 0);

	cmdList->DrawInstanced(4, 1, 0, 0);
}

}
