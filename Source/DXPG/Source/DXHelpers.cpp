#include "DXHelpers.h"

namespace dxpg::dx12
{
    std::unique_ptr<CPUDescriptorHeapAllocator> g_CPUDescriptorAllocator = nullptr;
    std::unique_ptr<GPUDescriptorHeapAllocator> g_GPUDescriptorAllocator = nullptr;

    std::unique_ptr<DescriptorHeap> DescriptorHeap::Create(D3D12_DESCRIPTOR_HEAP_DESC desc, ID3D12Device* device)
    {
        auto heap = std::make_unique<DescriptorHeap>();
        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap->Heap))))
			return nullptr;
        heap->Device = device;
        heap->Desc = desc;
        heap->Increment = device->GetDescriptorHandleIncrementSize(desc.Type);
        heap->Top = 0;
        return heap;
    }

    std::unique_ptr<DescriptorAllocation> DescriptorAllocation::Create(DescriptorHeap* heap, uint32_t size)
    {
        auto alloc = std::unique_ptr<DescriptorAllocation>(new DescriptorAllocation());
        alloc->Heap = heap;
        alloc->Index = heap->Allocate(size);
        alloc->Size = size;
        return alloc;
    }

    std::unique_ptr<DescriptorAllocation> DescriptorAllocation::CreatePreAllocated(DescriptorHeap* heap, size_t index, size_t size)
    {
        auto alloc = std::unique_ptr<DescriptorAllocation>(new DescriptorAllocation());
        alloc->Heap = heap;
        alloc->Index = index;
        alloc->Size = size;
    	return alloc;
    }


    template<bool CPU>
    std::unique_ptr<DescriptorHeapAllocator<CPU>> DescriptorHeapAllocator<CPU>::Create(ID3D12Device* dev, size_t numDescriptors, uint32_t pageCount, size_t staticPageSize)
    {
        auto allocator = std::make_unique<DescriptorHeapAllocator>();
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = numDescriptors;
        if constexpr (CPU)
        {

            for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i++)
            {
                desc.Type = (D3D12_DESCRIPTOR_HEAP_TYPE)i;
                allocator->Heaps[(D3D12_DESCRIPTOR_HEAP_TYPE)i] = DescriptorHeapPageCollection::Create(desc, dev, pageCount, staticPageSize);
            }
        }
        else
        {
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            allocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = DescriptorHeapPageCollection::Create(desc, dev, pageCount, staticPageSize);
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            allocator->Heaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = DescriptorHeapPageCollection::Create(desc, dev, pageCount, staticPageSize);
        }

        return allocator;
    }
    template class DescriptorHeapAllocator<true>;
    template class DescriptorHeapAllocator<false>;


    template<ViewTypes type>
    std::unique_ptr<ResourceView<type>> ResourceView<type>::Create(ID3D12Device * device, std::span<std::pair<ID3D12Resource*, typename ResourceViewToDesc<type>::Desc*>> resourcesXDescs)
    {
        using ViewToDesc = ResourceViewToDesc<type>;
        auto* view = g_CPUDescriptorAllocator->AllocateFromStatic(ViewToDesc::HeapType, resourcesXDescs.size()).release();
        for (size_t i = 0; i < resourcesXDescs.size(); i++)
        {
            if constexpr (type == ViewTypes::ShaderResourceView)
                device->CreateShaderResourceView(resourcesXDescs[i].first, resourcesXDescs[i].second, view->GetCPUHandle(i));
            else if constexpr (type == ViewTypes::UnorderedAccessView)
            {
                assert(false);
                //device->CreateUnorderedAccessView(resourcesXDescs[i].first, resourcesXDescs[i].second, view->Allocation->GetCPUHandle(i));
            }
            else if constexpr (type == ViewTypes::ConstantBufferView)
                device->CreateConstantBufferView(resourcesXDescs[i].second, view->GetCPUHandle(i));
            else if constexpr (type == ViewTypes::Sampler)
                device->CreateSampler(resourcesXDescs[i].second, view->GetCPUHandle(i));
            else if constexpr (type == ViewTypes::RenderTargetView)
                device->CreateRenderTargetView(resourcesXDescs[i].first, resourcesXDescs[i].second, view->GetCPUHandle(i));
            else if constexpr (type == ViewTypes::DepthStencilView)
                device->CreateDepthStencilView(resourcesXDescs[i].first, resourcesXDescs[i].second, view->GetCPUHandle(i));
            else
            {
                assert(false);
            }
        }
        return std::unique_ptr<ResourceView<type>>(reinterpret_cast<ResourceView<type>*>(view));
    }
    template class ResourceView<ViewTypes::ShaderResourceView>;
    template class ResourceView<ViewTypes::UnorderedAccessView>;
    template class ResourceView<ViewTypes::ConstantBufferView>;
    template class ResourceView<ViewTypes::Sampler>;
    template class ResourceView<ViewTypes::RenderTargetView>;
    template class ResourceView<ViewTypes::DepthStencilView>;

    std::unique_ptr<VertexData> VertexData::Create(ID3D12Device* device, size_t positionsCount, size_t normalsCount, size_t texCoordsCount)
    {
        auto vertexData = std::make_unique<VertexData>();
        size_t posBytes = positionsCount * 3 * sizeof(float);
        D3D12_RESOURCE_DESC posDesc = CD3DX12_RESOURCE_DESC::Buffer(posBytes);

        size_t normalBytes = normalsCount * 3 * sizeof(float);
        D3D12_RESOURCE_DESC normalDesc = CD3DX12_RESOURCE_DESC::Buffer(normalBytes);
        
        size_t texCoordBytes = texCoordsCount * 2 * sizeof(float);
        D3D12_RESOURCE_DESC texCoordDesc = CD3DX12_RESOURCE_DESC::Buffer(texCoordBytes);

        D3D12_RESOURCE_DESC descs[] = { posDesc, normalDesc, texCoordDesc };
        
        auto posAllocInfo = device->GetResourceAllocationInfo(0, 1, &posDesc);
        auto normalAllocInfo = device->GetResourceAllocationInfo(0, 1, &normalDesc);
        auto texCoordAllocInfo = device->GetResourceAllocationInfo(0, 1, &texCoordDesc);

        
        auto allocInfo = device->GetResourceAllocationInfo(0, _countof(descs), descs);


        auto heapDesc = CD3DX12_HEAP_DESC(allocInfo.SizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_NONE);
        device->CreateHeap(&heapDesc, IID_PPV_ARGS(&vertexData->Heap));

        device->CreatePlacedResource(vertexData->Heap.Get(), 0, &posDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexData->PositionsBuffer));

        device->CreatePlacedResource(vertexData->Heap.Get(), posAllocInfo.SizeInBytes, &normalDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexData->NormalsBuffer));

        device->CreatePlacedResource(vertexData->Heap.Get(), posAllocInfo.SizeInBytes + normalAllocInfo.SizeInBytes, &texCoordDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexData->TexCoordsBuffer));

        std::pair<ID3D12Resource*, D3D12_SHADER_RESOURCE_VIEW_DESC*> resources[3] = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC posViewDesc{};
        posViewDesc.Format = DXGI_FORMAT_UNKNOWN;
        posViewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        posViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        posViewDesc.Buffer.StructureByteStride = 3 * sizeof(float);
        posViewDesc.Buffer.FirstElement = 0;
        posViewDesc.Buffer.NumElements = positionsCount;
        resources[0] = { vertexData->PositionsBuffer.Get(), &posViewDesc };
        auto normalsViewDesc = posViewDesc;
        normalsViewDesc.Buffer.NumElements = normalsCount;
        resources[1] = { vertexData->NormalsBuffer.Get(), &normalsViewDesc};
        auto texCoordsViewDesc = normalsViewDesc;
        texCoordsViewDesc.Buffer.StructureByteStride = 2 * sizeof(float);
        texCoordsViewDesc.Buffer.NumElements = texCoordsCount;
        resources[2] = { vertexData->TexCoordsBuffer.Get(), &texCoordsViewDesc };

        vertexData->VertexSRV = ShaderResourceView::Create(device, std::span{ resources });
        return vertexData;
    }

    std::unique_ptr<D3D12Mesh> D3D12Mesh::Create(ID3D12Device* device, dx12::VertexData* vertexData, size_t indicesCount, size_t indexSize)
    {
        auto mesh = std::unique_ptr<D3D12Mesh>(new D3D12Mesh());
		mesh->VertexData = vertexData;
        mesh->IndicesCount = indicesCount;
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(indicesCount * indexSize);
        device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mesh->Indices));
        mesh->IndicesView.BufferLocation = mesh->Indices->GetGPUVirtualAddress();
        mesh->IndicesView.SizeInBytes = indicesCount * indexSize;
        mesh->IndicesView.StrideInBytes = indexSize;
		return mesh;
    }

}