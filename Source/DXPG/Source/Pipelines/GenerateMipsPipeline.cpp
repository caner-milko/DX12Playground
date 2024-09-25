#include "GenerateMipsPipeline.h"

#include "ShaderManager.h"
#include "DXResource.h"

#define A_CPU
#include <ffx_a.h>
#include <ffx_spd.h>

namespace dxpg
{

struct SpdConstants
{
	int mips;
	int numWorkGroupsPerSlice;
	int workGroupOffset[2];
};



bool GenerateMipsPipeline::Setup(ID3D12Device2* dev)
{
	Device = dev;
	auto* shader = ShaderManager::Get().CompileShader(L"SPDImpl.cs", DXPG_SHADERS_DIR L"Compute/SPDImpl.cs.hlsl", ShaderType::Compute, L"main", { {FIDELITYFX_SPD_SHADER_INCLUDE_DIR L""} });
	RootSignatureBuilder rsBuilder;
	rsBuilder.AddConstantBufferView("spdConstants", { .ShaderRegister = 0, .Visibility = D3D12_SHADER_VISIBILITY_ALL });
	rsBuilder.AddDescriptorTable("spdGlobalAtomic", { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1)}});
	rsBuilder.AddDescriptorTable("imgDst6", { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2)} });
	rsBuilder.AddDescriptorTable("imgDst", { { CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, SPD_MAX_MIP_LEVELS + 1, 3)} });
	RootSignature = rsBuilder.Build("SPDRS", dev, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	struct PipelineStateStream : PipelineStateStreamBase
	{
		CD3DX12_PIPELINE_STATE_STREAM_CS CS;
	} pipelineStateStream;

	pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(shader->Blob.Get());

	PipelineState = PipelineState::Create("SPDPipelineState", dev, pipelineStateStream, &RootSignature);

	// Create the global counter buffer

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	GlobalCounterBuffer = DXTypedSingularBuffer<GlobalCounterStruct>::Create(dev, L"GlobalCounterBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	GlobalCounterUAV = GlobalCounterBuffer.CreateTypedUAV();
	return true;
}

void GenerateMipsPipeline::GenerateMips(FrameContext& frameCtx, ID3D12GraphicsCommandList2* cmdList, DXTexture& texture, uint32_t width, uint32_t height, uint32_t arraySize)
{
	varAU2(dispatchThreadGroupCountXY);
	varAU2(workGroupOffset); // needed if Left and Top are not 0,0
	varAU2(numWorkGroupsAndMips);
	varAU4(rectInfo) = initAU4(0, 0, width, height); // left, top, width, height
	SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);

	// downsample
	uint32_t dispatchX = dispatchThreadGroupCountXY[0];
	uint32_t dispatchY = dispatchThreadGroupCountXY[1];
	uint32_t dispatchZ = arraySize;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	auto cb = DXBuffer::Create(Device, L"SPDConstants", sizeof(SpdConstants), D3D12_HEAP_TYPE_UPLOAD);
	TransitionVec(cb, D3D12_RESOURCE_STATE_GENERIC_READ).Execute(cmdList);
	frameCtx.IntermediateResources.push_back(cb.Resource);
	auto* consts = cb.Map<SpdConstants>();

	SpdConstants constants;
	constants.numWorkGroupsPerSlice = numWorkGroupsAndMips[0];
	constants.mips = numWorkGroupsAndMips[1];
	constants.workGroupOffset[0] = workGroupOffset[0];
	constants.workGroupOffset[1] = workGroupOffset[1];
	memcpy(consts, &constants, sizeof(SpdConstants));

	cmdList->SetComputeRootSignature(RootSignature.DXSignature.Get());

	auto mipUavs = frameCtx.GPUHeapPages[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->Allocate(SPD_MAX_MIP_LEVELS+5);
	
	for (int i = 0; i < SPD_MAX_MIP_LEVELS + 5; i++)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uavDesc.Texture2D.MipSlice = constants.mips <= i ? constants.mips : i;
		uavDesc.Texture2D.PlaneSlice = 0;
		texture.CreatePlacedUAV(mipUavs.GetView(i), &uavDesc);
	}
	
	// Bind Pipeline
	//
	cmdList->SetPipelineState(PipelineState.DXPipelineState.Get());

	// set counter to 0
	TransitionVec(GlobalCounterBuffer, D3D12_RESOURCE_STATE_COPY_DEST).Add(texture, D3D12_RESOURCE_STATE_COPY_DEST).Execute(cmdList);

	D3D12_WRITEBUFFERIMMEDIATE_PARAMETER pParams[6];
	for (int i = 0; i < 6; i++)
		pParams[i] = { GlobalCounterBuffer.GPUAddress(sizeof(uint32_t) * i), 0 };
	cmdList->WriteBufferImmediate(6, pParams, nullptr); // 6 counter per slice, each initialized to 0

	TransitionVec().Add(GlobalCounterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS).Add(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS).Execute(cmdList);
	

	// Bind Descriptor the descriptor sets
	//                
	int params = 0;
	cmdList->SetComputeRootConstantBufferView(params++, cb.GPUAddress());
	cmdList->SetComputeRootDescriptorTable(params++, frameCtx.GetGPUAllocation(&GlobalCounterUAV).GetGPUHandle());
	cmdList->SetComputeRootDescriptorTable(params++, mipUavs.GetGPUHandle(6));
	// bind UAVs
	cmdList->SetComputeRootDescriptorTable(params++, mipUavs.GetGPUHandle());
	// Dispatch
	//
	cmdList->Dispatch(dispatchX, dispatchY, dispatchZ);
	TransitionVec(texture, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE).Execute(cmdList);
}

}