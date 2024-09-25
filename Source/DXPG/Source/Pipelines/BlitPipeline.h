#pragma once

#include "DXPGCommon.h"

#include "RootSignature.h"
#include "PipelineState.h"

#include "RendererCommon.h"

namespace dxpg
{

struct BlitPipeline
{
	bool Setup(ID3D12Device2* dev);
	void Blit(ID3D12GraphicsCommandList2* cmdList, struct DXTexture* dstTex, struct DXTexture* srcTex, D3D12_CPU_DESCRIPTOR_HANDLE dstRTV, uint32_t srcSRVIndex);
	
	ID3D12Device2* Device = nullptr;
	PipelineState PipelineState;
};

}