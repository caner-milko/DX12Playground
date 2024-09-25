#pragma once

#include "DXHelpers.h"

namespace dxpg
{


struct DXResource
{
	std::wstring Name;
	ComPtr<ID3D12Resource> Resource = nullptr;
	D3D12_RESOURCE_STATES State;
	ID3D12Device* Device = nullptr;

	// Remove this with multi threaded rendering
	D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES newState)
	{
		assert(State != newState && "Resource already in requested state");
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(Resource.Get(), State, newState);
		State = newState;
		return barrier;
	}

	DXResource() = default;
	DXResource(std::wstring name, ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES startState, ID3D12Device* dev) noexcept : Name(std::move(name)), Resource(resource), State(startState), Device(dev) {if(Resource) Resource->SetName(Name.c_str());}
};

struct DXTexture : DXResource
{
	struct TextureCreateInfo
	{
		D3D12_RESOURCE_DIMENSION Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		uint32_t Width;
		uint32_t Height;
		//Multipled by 6 for cube maps
		uint16_t DepthOrArraySize = 1;
		uint16_t MipLevels = 0;
		DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		DXGI_SAMPLE_DESC SampleDesc = { 1, 0 };
		D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE;
		D3D12_TEXTURE_LAYOUT Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		uint64_t Alignment = 0;
		std::optional<D3D12_CLEAR_VALUE> ClearValue = std::nullopt;
		bool IsCubeMap = false;

		TextureCreateInfo& AddFlags(D3D12_RESOURCE_FLAGS flags)
		{
			Flags |= flags;
			return *this;
		}
	};

	static DXTexture Create(ID3D12Device* device, std::wstring name, TextureCreateInfo const& info, D3D12_RESOURCE_STATES startState = D3D12_RESOURCE_STATE_COMMON);
	static DXTexture FromExisting(ID3D12Device* device, std::wstring name, ComPtr<ID3D12Resource> resource, TextureCreateInfo const& info, D3D12_RESOURCE_STATES startState = D3D12_RESOURCE_STATE_COMMON);


	ShaderResourceView CreateSRV(D3D12_SHADER_RESOURCE_VIEW_DESC const* srvDesc);
	UnorderedAccessView CreateUAV(D3D12_UNORDERED_ACCESS_VIEW_DESC const* uavDesc);
	RenderTargetView CreateRTV(D3D12_RENDER_TARGET_VIEW_DESC const* rtvDesc);
	DepthStencilView CreateDSV(D3D12_DEPTH_STENCIL_VIEW_DESC const* dsvDesc);

	void CreatePlacedSRV(DescriptorAllocationView alloc, D3D12_SHADER_RESOURCE_VIEW_DESC const* srvDesc);
	void CreatePlacedUAV(DescriptorAllocationView alloc, D3D12_UNORDERED_ACCESS_VIEW_DESC const* uavDesc);
	void CreatePlacedRTV(DescriptorAllocationView alloc, D3D12_RENDER_TARGET_VIEW_DESC const* rtvDesc);
	void CreatePlacedDSV(DescriptorAllocationView alloc, D3D12_DEPTH_STENCIL_VIEW_DESC const* dsvDesc);

	TextureCreateInfo Info;

	DXTexture() = default;
	DXTexture(std::wstring name, ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES startState, ID3D12Device* dev, TextureCreateInfo const& info) noexcept : DXResource(name, resource, startState, dev), Info(info) {}
};

struct DXBuffer : DXResource
{
	static DXBuffer Create(ID3D12Device* device, std::wstring name, size_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

	template<typename T>
	static DXBuffer CreateAndUpload(ID3D12Device* device, std::wstring name, ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& outUploadBuf, std::span<const T> data, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		return CreateAndUpload(device, name, cmdList, outUploadBuf, std::span<const std::byte>((std::byte const*)data.data(), data.size_bytes()), state, flags);
	}

	static DXBuffer CreateAndUpload(ID3D12Device* device, std::wstring name, ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& outUploadBuf, std::span<const std::byte> data, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

	template<typename T>
	ComPtr<ID3D12Resource> Upload(ID3D12GraphicsCommandList* cmdList, std::span<const T> data, size_t offset = 0)
	{
		return Upload(cmdList, std::span<const std::byte>((std::byte const*)data.data(), data.size_bytes()), offset);
	}

	ComPtr<ID3D12Resource> Upload(ID3D12GraphicsCommandList* cmdList, std::span<const std::byte> data, size_t offset = 0);

	ShaderResourceView CreateSRV(size_t numElements = 0, size_t stride = 0, size_t offset = 0, D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE);

	template<typename T>
	ShaderResourceView CreateTypedSRV(size_t size = 0, size_t offset = 0, D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE)
	{
		return CreateSRV(size, offset, sizeof(T), flags);
	}

	UnorderedAccessView CreateUAV(size_t numElements, size_t stride = 0, size_t firstElement = 0, D3D12_BUFFER_UAV_FLAGS flags = D3D12_BUFFER_UAV_FLAG_NONE);

	ConstantBufferView CreateCBV(size_t size = 0, size_t offset = 0);
	template<typename T>
	ConstantBufferView CreateTypedCBV(size_t size = 0, size_t offset = 0)
	{
		return CreateCBV(size, offset);
	}

	void CreatePlacedSRV(DescriptorAllocationView alloc, size_t numElements = 0, size_t stride = 0, size_t offset = 0, D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE);
	void CreatePlacedUAV(DescriptorAllocationView alloc, size_t numElements = 0, size_t stride = 0, size_t offset = 0, D3D12_BUFFER_UAV_FLAGS flags = D3D12_BUFFER_UAV_FLAG_NONE);
	void CreatePlacedCBV(DescriptorAllocationView alloc, size_t size = 0, size_t offset = 0);

	inline D3D12_GPU_VIRTUAL_ADDRESS GPUAddress(size_t offset = 0)
	{
		return Resource->GetGPUVirtualAddress() + offset;
	}

	template<typename T = void>
	inline T* Map()
	{
		if constexpr (!std::is_same_v<T, void>)
		{
			assert(sizeof(T) <= Size);
		}
		T* Data = nullptr;
		Resource->Map(0, nullptr, (void**)&Data);
		return Data;
	}

	inline void Unmap()
	{
		Resource->Unmap(0, nullptr);
	}

	size_t Size = 0;

	DXBuffer() = default;
	DXBuffer(std::wstring name, ComPtr<ID3D12Resource> resource, ID3D12Device* dev, size_t size) noexcept : DXResource(name, resource, D3D12_RESOURCE_STATE_COMMON, dev), Size(size) {}
};

template<typename T>
struct DXTypedBuffer : DXBuffer
{
	static DXTypedBuffer Create(ID3D12Device* device, std::wstring name, size_t numElements, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		return DXTypedBuffer(DXBuffer::Create(device, name, numElements * sizeof(T), heapType, flags));
	}

	static DXTypedBuffer CreateAndUpload(ID3D12Device* device, std::wstring name, ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& outUploadBuf, std::span<const T> data, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		return DXTypedBuffer(DXBuffer::CreateAndUpload(device, name, cmdList, outUploadBuf, std::span<const std::byte>((std::byte const*)data.data(), data.size_bytes()), state, flags));
	}

	UnorderedAccessView CreateTypedUAV(size_t firstElement = 0, D3D12_BUFFER_UAV_FLAGS flags = D3D12_BUFFER_UAV_FLAG_NONE)
	{
		return CreateUAV(Size / sizeof(T), sizeof(T), firstElement, flags);
	}

	ShaderResourceView CreateTypedSRV(size_t offset = 0, D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE)
	{
		return CreateSRV(Size / sizeof(T), offset, sizeof(T), flags);
	}

	using DXBuffer::DXBuffer;
	explicit DXTypedBuffer(DXBuffer const& buf) : DXBuffer(buf) { Size = buf.Size; }
};

template<typename T>
struct DXTypedSingularBuffer : DXTypedBuffer<T>
{
	static DXTypedSingularBuffer<T> Create(ID3D12Device* device, std::wstring name, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		return static_cast<DXTypedSingularBuffer<T>>(DXTypedBuffer<T>::Create(device, name, 1, D3D12_HEAP_TYPE_DEFAULT, flags));
	}

	static DXTypedSingularBuffer<T> CreateAndUpload(ID3D12Device* device, std::wstring name, ID3D12GraphicsCommandList* cmdList, ComPtr<ID3D12Resource>& outUploadResource, T const& data, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		return static_cast<DXTypedSingularBuffer<T>>(DXTypedBuffer<T>::CreateAndUpload(device, name, cmdList, outUploadResource, std::span{ &data, 1 }, state, flags));
	}

	using DXTypedBuffer<T>::DXTypedBuffer;
	explicit DXTypedSingularBuffer(DXBuffer const& buf) : DXTypedBuffer<T>(buf) {}
};

}