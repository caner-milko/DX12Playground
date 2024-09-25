#include "ModelManager.h"

#include <filesystem>
#include <tiny_obj_loader.h>
#include "TextureManager.h"
using namespace DirectX;

// Hash function for Vertex
namespace std
{
	template<> struct hash<DirectX::XMFLOAT2>
	{
		size_t operator()(DirectX::XMFLOAT2 const& v) const
		{
			size_t seed = 0;
			dxpg::HashCombine(seed, v.x, v.y);
			return seed;
		}
	};
	template<> struct hash<DirectX::XMFLOAT3>
	{
		size_t operator()(DirectX::XMFLOAT3 const& v) const
		{
			size_t seed = 0;
			dxpg::HashCombine(seed, v.x, v.y, v.z);
			return seed;
		}
	};
	template<> struct hash<dxpg::Vertex>
	{
		size_t operator()(dxpg::Vertex const& vertex) const
		{
			size_t seed = 0;
			dxpg::HashCombine(seed, vertex.Position, vertex.Normal, vertex.TexCoord);
			return seed;
		}
	};
}

namespace dxpg
{
std::unique_ptr<ModelManager> ModelManager::Instance = nullptr;
void ModelManager::Init(ID3D12Device* device)
{
	Device = device;
}

void LoadVerticesAndIndexBuffer(const tinyobj::ObjReader& reader, std::vector<Vertex>& vertices, std::vector<std::vector<uint32_t>>& indexPerShape)
{
	auto& attrib = reader.GetAttrib();
	auto shapes = reader.GetShapes();

	std::unordered_map<Vertex, size_t> uniqueVertices;

	for (size_t s = 0; s < shapes.size(); s++) {
		auto& indices = indexPerShape.emplace_back();
		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) 
		{
			//hardcode loading to triangles
			int fv = 3;
			Vertex newVertices[3];
			Vector4 bitangent;
			// Loop over vertices in the face.
			for (size_t v = 0; v < fv; v++) 
			{
				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

				//vertex position
				tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
				//vertex normal
				tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];
				//vertex texcoords
				tinyobj::real_t tx = attrib.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t ty = attrib.texcoords[2 * idx.texcoord_index + 1];

				//copy it into our vertex
				Vertex new_vert;
				new_vert.Position = { vx, vy, vz };

				new_vert.Normal = { nx, ny, nz };

				new_vert.TexCoord = { tx, ty };

				newVertices[v] = new_vert;
			}

			XMVECTOR deltaPos1 = XMLoadFloat3(&newVertices[1].Position) - XMLoadFloat3(&newVertices[0].Position);
			XMVECTOR deltaPos2 = XMLoadFloat3(&newVertices[2].Position) - XMLoadFloat3(&newVertices[0].Position);

			XMVECTOR deltaUV1 = XMLoadFloat2(&newVertices[1].TexCoord) - XMLoadFloat2(&newVertices[0].TexCoord);
			XMVECTOR deltaUV2 = XMLoadFloat2(&newVertices[2].TexCoord) - XMLoadFloat2(&newVertices[0].TexCoord);

			float r = 1.0F / (DirectX::XMVectorGetX(deltaUV1) * DirectX::XMVectorGetY(deltaUV2) - DirectX::XMVectorGetY(deltaUV1) * DirectX::XMVectorGetX(deltaUV2));
			XMVECTOR tangent = (deltaPos1 * DirectX::XMVectorGetY(deltaUV2) - deltaPos2 * DirectX::XMVectorGetY(deltaUV1)) * r;
			bitangent = (deltaPos2 * DirectX::XMVectorGetX(deltaUV1) - deltaPos1 * DirectX::XMVectorGetX(deltaUV2)) * r;
			DirectX::XMStoreFloat3(&newVertices[0].Tangent, tangent);
			DirectX::XMStoreFloat3(&newVertices[1].Tangent, tangent);
			DirectX::XMStoreFloat3(&newVertices[2].Tangent, tangent);

			for (size_t v = 0; v < fv; v++)
			{
				Vertex& vtx = newVertices[v];

				XMVECTOR n = XMLoadFloat3(&vtx.Normal);
				XMVECTOR t = XMLoadFloat3(&vtx.Tangent);

				// Gram-Schmidt orthogonalize
				t = XMVector3Normalize(t - n * XMVector3Dot(n, t));

				// Calculate handedness
				if(XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), bitangent)) < 0.0f)
					t *= -1;
				
				XMStoreFloat3(&vtx.Tangent, t);
			}

			for (size_t v = 0; v < fv; v++) 
			{
				Vertex& vtx = newVertices[v];
				size_t index;
				if (auto it = uniqueVertices.find(vtx); it != uniqueVertices.end())
				{
					index = it->second;
					Vertex& existing = vertices[index];
					XMStoreFloat3(&existing.Tangent, XMLoadFloat3(&existing.Tangent) + XMLoadFloat3(&vtx.Tangent));
				}
				else
				{
					index = vertices.size();
					vertices.emplace_back(vtx);
					uniqueVertices.emplace(vtx, index);
				}
				indices.push_back(index);
			}

			index_offset += fv;

		}
	}
}

ObjModel* ModelManager::LoadModel(const std::string& modelPath, FrameContext& frameCtx, ID3D12GraphicsCommandList2* cmdList)
{
	auto it = Models.find(modelPath);
	if (it != Models.end())
	{
		return it->second.get();
	}
    
	auto& objModel = Models[modelPath] = std::make_unique<ObjModel>();

    tinyobj::ObjReaderConfig readerConfig;
    tinyobj::ObjReader reader;
    reader.ParseFromFile(modelPath, readerConfig);
    assert(reader.Valid());
    auto& attrib = reader.GetAttrib();
    auto shapes = reader.GetShapes();

    objModel->ModelViews.reserve(shapes.size());
	objModel->Materials.reserve(reader.GetMaterials().size());

	std::vector<Vertex> vertices;
	std::vector<std::vector<uint32_t>> indicesPerShape;
	LoadVerticesAndIndexBuffer(reader, vertices, indicesPerShape);
    {
		auto& model = objModel->Model;
	    model.VerticesBuffer = DXTypedBuffer<Vertex>::CreateAndUpload(Device, s2ws(modelPath) + L"_Vertices", cmdList, frameCtx.IntermediateResources.emplace_back(), vertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		model.VertexBufferView.BufferLocation = model.VerticesBuffer.Resource->GetGPUVirtualAddress();
		model.VertexBufferView.SizeInBytes = model.VerticesBuffer.Size;
		model.VertexBufferView.StrideInBytes = sizeof(Vertex);
    }


    for (auto& mat : reader.GetMaterials())
    {
        auto& material = objModel->Materials[mat.name];
        material.Name = mat.name;

        material.MaterialInfo = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		dxpg::hlsl::MaterialBuffer matInfo = {};
        bool difTexLoaded = false;
        // Load the textures
        if (!mat.diffuse_texname.empty())
        {
            material.DiffuseTextureName = std::filesystem::path(modelPath).parent_path().string() + "/" + mat.diffuse_texname;
            // Load texture into memory
            auto* tex = TextureManager::Get().LoadTexture(std::filesystem::path(*material.DiffuseTextureName), {}, frameCtx, cmdList, true);
            if (tex)
            {
		        material.DiffuseTextureSRV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = -1;
                tex->CreatePlacedSRV(material.DiffuseTextureSRV->GetView(0), &srvDesc);
				difTexLoaded = true;
				matInfo.DiffuseTextureIndex = material.DiffuseTextureSRV->Index;
            }
        }
        if (!difTexLoaded)
        {
            matInfo.Diffuse = XMFLOAT4{ mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f };
            material.DiffuseColor = Vector3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
        }
		if (!mat.displacement_texname.empty())
		{
			material.NormalMapTextureName = std::filesystem::path(modelPath).parent_path().string() + "/" + mat.displacement_texname;
			// Load texture into memory
			auto* tex = TextureManager::Get().LoadTexture(std::filesystem::path(*material.NormalMapTextureName), {}, frameCtx, cmdList, true);
			if (tex)
			{
				material.NormalMapTextureSRV = g_GPUDescriptorAllocator->AllocateFromStatic(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Texture2D.MipLevels = -1;
				tex->CreatePlacedSRV(material.NormalMapTextureSRV->GetView(0), &srvDesc);
				matInfo.NormalMapTextureIndex = material.NormalMapTextureSRV->Index;
			}
		}

        material.MaterialInfoBuffer = DXTypedSingularBuffer<dxpg::hlsl::MaterialBuffer>::CreateAndUpload(Device, s2ws(mat.name) + L"_MaterialInfo", cmdList, frameCtx.IntermediateResources.emplace_back(), matInfo, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		material.MaterialInfoBuffer.CreatePlacedCBV(material.MaterialInfo.GetView(0));
    }

    // Loop over shapes
    for (size_t i = 0; i <shapes.size(); i++)
    {
		auto& shape = shapes[i];
		auto& indices = indicesPerShape[i];
		auto& indexedModel = objModel->ModelViews[shape.name];
		indexedModel.Model = &objModel->Model;
        indexedModel.Name = shape.name;
        indexedModel.Indices = DXTypedBuffer<uint32_t>::CreateAndUpload(Device, s2ws(shape.name), cmdList, frameCtx.IntermediateResources.emplace_back(), indices, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		indexedModel.IndexBufferView.BufferLocation = indexedModel.Indices.Resource->GetGPUVirtualAddress();
		indexedModel.IndexBufferView.SizeInBytes = indexedModel.Indices.Size;
		indexedModel.IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    }

	objModel->Objects.reserve(shapes.size());

    for (size_t i = 0; i < shapes.size(); ++i)
    {
		auto& shape = shapes[i];
		auto& indexedModel = objModel->ModelViews[shape.name];

		Material* mat = nullptr;

        if (!shape.mesh.material_ids.empty())
        {
            auto matName =reader.GetMaterials()[shape.mesh.material_ids[0]].name;
			assert(objModel->Materials.find(matName) != objModel->Materials.end());
			mat = &objModel->Materials[matName];
        }
		objModel->Objects.emplace_back(&indexedModel, mat);
    }

    return objModel.get();
}

}