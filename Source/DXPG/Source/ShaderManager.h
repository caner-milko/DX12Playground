#pragma once

#include "DXHelpers.h"
#include "Shader.h"
#include "DXPGCommon.h"
#include "RootSignature.h"


#include <dxcapi.h>
#include <d3d12shader.h>

namespace dxpg
{

struct ShaderManager : Singleton<ShaderManager>
{
	ShaderManager();
	void Init(ID3D12Device* device);

	std::pair<Shader*, Shader*> CompileBindlessGraphicsShader(std::wstring_view name, std::wstring_view shaderPath, std::span<const std::wstring_view> includeFolders = {});
	Shader* CompileBindlessVertexShader(std::wstring_view name, std::wstring_view shaderPath, std::span<const std::wstring_view> includeFolders = {});
	Shader* CompileBindlessComputeShader(std::wstring_view name, std::wstring_view shaderPath, std::span<const std::wstring_view> includeFolders = {});
	Shader* CompileShader(std::wstring_view name, std::wstring_view shaderPath, ShaderType type, std::wstring_view entryPoint = L"main", std::span<const std::wstring_view> includeFolders = {});



	std::unordered_map<std::wstring, std::unique_ptr<Shader>> LoadedShaders;

	RootSignature BindlessRootSignature;

private:
	ComPtr<IDxcUtils> Utils;
	ComPtr<IDxcCompiler3> Compiler;
	ComPtr<IDxcIncludeHandler> IncludeHandler;

};

}