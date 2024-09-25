#include "DXResource.h"

namespace dxpg
{
DXTexture DXTexture::Create(ID3D12Device* device, std::wstring name, TextureCreateInfo const& info, D3D12_RESOURCE_STATES startState)
{
	// Create the texture
	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(info.Format, info.Width, info.Height, info.DepthOrArraySize * (info.IsCubeMap ? 6 : 1), info.MipLevels, info.SampleDesc.Count, info.SampleDesc.Quality, info.Flags, info.Layout, info.Alignment);
	auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ComPtr<ID3D12Resource> resource;

	device->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		startState,
		info.ClearValue ? &*info.ClearValue : nullptr,
		IID_PPV_ARGS(&resource));

	return DXTexture(name, resource, startState, device, info);
}

DXTexture DXTexture::FromExisting(ID3D12Device* device, std::wstring name, ComPtr<ID3D12Resource> resource, TextureCreateInfo const& info, D3D12_RESOURCE_STATES startState)
{
	return DXTexture(name, resource, startState, device, info);
}

ShaderResourceView DXTexture::CreateSRV(D3D12_SHADER_RESOURCE_VIEW_DESC const* srvDesc)
{
	return ShaderResourceView::Create({ ResourceViewToDesc<ViewTypes::ShaderResourceView>{ srvDesc, Resource.Get() } });
}

UnorderedAccessView DXTexture::CreateUAV(D3D12_UNORDERED_ACCESS_VIEW_DESC const* uavDesc)
{
	return UnorderedAccessView::Create({ ResourceViewToDesc<ViewTypes::UnorderedAccessView>{ uavDesc, Resource.Get() } });
}

RenderTargetView DXTexture::CreateRTV(D3D12_RENDER_TARGET_VIEW_DESC const* rtvDesc)
{
	return RenderTargetView::Create({ ResourceViewToDesc<ViewTypes::RenderTargetView>{ rtvDesc, Resource.Get() } });
}

DepthStencilView DXTexture::CreateDSV(D3D12_DEPTH_STENCIL_VIEW_DESC const* dsvDesc)
{
	return DepthStencilView::Create({ ResourceViewToDesc<ViewTypes::DepthStencilView>{ dsvDesc, Resource.Get() } });
}

DXBuffer DXBuffer::Create(ID3D12Device* device, std::wstring name, size_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags)
{
	size = size < 256 ? 256 : size;
	ComPtr<ID3D12Resource> resource;
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, flags);
	auto heapProp = CD3DX12_HEAP_PROPERTIES(heapType);
	device->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&resource));
	return DXBuffer(name, resource, device, size);
}

DXBuffer DXBuffer::CreateAndUpload(ID3D12Device* device, std::wstring name, ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& outUploadBuf, std::span<const std::byte> data, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags)
{
	auto buffer = Create(device, name, data.size(), D3D12_HEAP_TYPE_DEFAULT, flags);
	TransitionVec(buffer, D3D12_RESOURCE_STATE_COPY_DEST).Execute(cmdList);
	outUploadBuf = buffer.Upload(cmdList, data);
	TransitionVec(buffer, state).Execute(cmdList);
	return buffer;
}


ComPtr<ID3D12Resource> DXBuffer::Upload(ID3D12GraphicsCommandList* cmdList, std::span<const std::byte> data, size_t offset)
{
	auto uploadResource = Create(Device, Name + L"UploadBuffer", data.size(), D3D12_HEAP_TYPE_UPLOAD);
	TransitionVec(uploadResource, D3D12_RESOURCE_STATE_GENERIC_READ).Execute(cmdList);
	CD3DX12_RANGE readRange(0, 0);
	memcpy(uploadResource.Map(), data.data(), data.size());
	uploadResource.Unmap();
	cmdList->CopyBufferRegion(Resource.Get(), offset, uploadResource.Resource.Get(), 0, data.size());
	return uploadResource.Resource;
}

ShaderResourceView DXBuffer::CreateSRV(size_t numElements, size_t stride, size_t offset, D3D12_BUFFER_SRV_FLAGS flags)
{
	auto descriptorAlloc = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	CreatePlacedSRV(descriptorAlloc.GetView(), numElements, stride, offset, flags);
	return static_cast<ShaderResourceView>(descriptorAlloc);
}

UnorderedAccessView DXBuffer::CreateUAV(size_t numElements, size_t stride, size_t firstElement, D3D12_BUFFER_UAV_FLAGS flags)
{
	auto descriptorAlloc = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	CreatePlacedUAV(descriptorAlloc.GetView(), numElements, stride, firstElement, flags);
	return static_cast<UnorderedAccessView>(descriptorAlloc);
}

ConstantBufferView DXBuffer::CreateCBV(size_t size, size_t offset)
{
	auto descriptorAlloc = g_CPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	CreatePlacedCBV(descriptorAlloc.GetView(), size, offset);
	return static_cast<ConstantBufferView>(descriptorAlloc);
}
void DXBuffer::CreatePlacedSRV(DescriptorAllocationView alloc, size_t numElements, size_t stride, size_t offset, D3D12_BUFFER_SRV_FLAGS flags)
{
	if (numElements == 0)
		numElements = stride == 0 ? Size : Size / stride;
	if (stride == 0)
		stride = Size / numElements;
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.Buffer.FirstElement = offset;
	desc.Buffer.NumElements = numElements;
	desc.Buffer.StructureByteStride = stride;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	alloc.Base->Heap->Device->CreateShaderResourceView(Resource.Get(), &desc, alloc.GetCPUHandle());
}
void DXBuffer::CreatePlacedUAV(DescriptorAllocationView alloc, size_t numElements, size_t stride, size_t firstElement, D3D12_BUFFER_UAV_FLAGS flags)
{
	if (numElements == 0)
		numElements = stride == 0 ? Size : Size / stride;
	if (stride == 0)
		stride = Size / numElements;
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	desc.Buffer.FirstElement = firstElement;
	desc.Buffer.NumElements = numElements;
	desc.Buffer.StructureByteStride = stride;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	alloc.Base->Heap->Device->CreateUnorderedAccessView(Resource.Get(), nullptr, &desc, alloc.GetCPUHandle());
}
void DXBuffer::CreatePlacedCBV(DescriptorAllocationView alloc, size_t size, size_t offset)
{
	assert(Size >= 256);
	size = size ? size : Size;
	D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
	desc.BufferLocation = Resource->GetGPUVirtualAddress() + offset;
	desc.SizeInBytes = size;
	alloc.Base->Heap->Device->CreateConstantBufferView(&desc, alloc.GetCPUHandle());
}
void DXTexture::CreatePlacedSRV(DescriptorAllocationView alloc, D3D12_SHADER_RESOURCE_VIEW_DESC const* srvDesc)
{
	alloc.Base->Heap->Device->CreateShaderResourceView(Resource.Get(), srvDesc, alloc.GetCPUHandle());
}
void DXTexture::CreatePlacedUAV(DescriptorAllocationView alloc,  D3D12_UNORDERED_ACCESS_VIEW_DESC const* uavDesc)
{
	alloc.Base->Heap->Device->CreateUnorderedAccessView(Resource.Get(), nullptr, uavDesc, alloc.GetCPUHandle());
}
void DXTexture::CreatePlacedRTV(DescriptorAllocationView alloc, D3D12_RENDER_TARGET_VIEW_DESC const* rtvDesc)
{
	alloc.Base->Heap->Device->CreateRenderTargetView(Resource.Get(), rtvDesc, alloc.GetCPUHandle());
}
void DXTexture::CreatePlacedDSV(DescriptorAllocationView alloc, D3D12_DEPTH_STENCIL_VIEW_DESC const* dsvDesc)
{
	alloc.Base->Heap->Device->CreateDepthStencilView(Resource.Get(), dsvDesc, alloc.GetCPUHandle());
}

}