#include "TextureManager.h"

#include <stb_image.h>

namespace dxpg
{

std::unique_ptr<TextureManager> TextureManager::Instance = nullptr;
void TextureManager::Init(ID3D12Device2* device)
{
	Device = device;
	GenerateMips.Setup(device);
}
DXTexture* dxpg::TextureManager::LoadTexture(std::filesystem::path const& path, TextureManager::TextureLoadInfo const& info, FrameContext& frameCtx, ID3D12GraphicsCommandList2* cmdList, bool generateMips)
{
	auto it = LoadedTextures.find(path);
	if (it != LoadedTextures.end())
	{
		return Textures[it->second].get();
	}

	int width, height, comp;

	if (stbi_info(path.string().c_str(), &width, &height, &comp) == 0)
	{
		std::cout << "Failed to load texture info for " << path << ". Reason: " << stbi_failure_reason() << std::endl;
		return { 0 };
	}

	DXTexture::TextureCreateInfo createInfo = {
	.Width = uint32_t(width),
	.Height = uint32_t(height),
	.MipLevels = generateMips ? 0u : 1u,
	.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
	.Flags = info.Flags,

	};

	if (generateMips)
		createInfo.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	int desiredComp = info.AlphaOnly ? STBI_grey : STBI_rgb_alpha;
	stbi_set_flip_vertically_on_load(true);
	stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &comp, desiredComp);

	if (data == nullptr)
	{
		std::cout << "Failed to load texture " << path << ". Reason: " << stbi_failure_reason() << std::endl;
		return { 0 };
	}

	auto texture = DXTexture::Create(Device, path.filename().wstring(), createInfo, D3D12_RESOURCE_STATE_COPY_DEST);

	// Copy the data to the texture
	{
		// Create the intermediate upload heap
		auto intermediateBuf = DXBuffer::Create(Device, path.filename().wstring() + L"_IntermediateBuffer", width * height * desiredComp, D3D12_HEAP_TYPE_UPLOAD);
		TransitionVec(intermediateBuf, D3D12_RESOURCE_STATE_GENERIC_READ).Execute(cmdList);
		frameCtx.IntermediateResources.push_back(intermediateBuf.Resource);

		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = data;
		subresourceData.RowPitch = width * desiredComp;
		subresourceData.SlicePitch = height * subresourceData.RowPitch;

		UpdateSubresources(cmdList, texture.Resource.Get(), intermediateBuf.Resource.Get(), 0, 0, 1, &subresourceData);
	}

	stbi_image_free(data);

	if (generateMips)
	{
		GenerateMips.GenerateMips(frameCtx, cmdList, texture, width, height);
	}


	TextureId id = NextId;
	NextId.Id++;
	
	auto& tex = Textures[id] = std::make_unique<DXTexture>(texture);
	LoadedTextures[path] = id;
	return tex.get();
}

}