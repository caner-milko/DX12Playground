// clang-format off
#pragma once

// To be able to share Constant Buffer Types (Structs) between HLSL and C++, a few defines are set here.
// Define the Structs in HLSLS syntax, and with this defines the C++ application can also use the structs.
#ifdef __cplusplus

#define float4 DirectX::XMFLOAT4
#define float3 DirectX::XMFLOAT3
#define float2 DirectX::XMFLOAT2

#define uint uint32_t

// Note : using the typedef of matrix (float4x4) and struct (ConstantBufferStruct) to prevent name collision on the cpp
// code side.
#define float4x4 DirectX::XMMATRIX

#define ConstantBufferStruct struct alignas(256)

#else
// if HLSL

#define ConstantBufferStruct struct

#endif

namespace dxpg
{
#ifdef __cplusplus
namespace hlsl
{
#endif

ConstantBufferStruct TransformBuffer
{
	float4x4 MVP;
	float4x4 Normal;
};

ConstantBufferStruct MaterialBuffer
{
    float4 Diffuse;
	// Do not sample if 0
    uint DiffuseTextureIndex;
    uint NormalMapTextureIndex;
};
    
struct LightDataBuffer
{
    float3 DirectionOrPosition;
    float Padding;
    float3 Color;
    float Intensity;
    float3 AmbientColor;
    float Padding2;
};

struct LightTransformBuffer
{
    float4x4 LightViewProjection;
    float4x4 CamInverseView;
    float4x4 CamInverseProjection;
};
    
#ifdef __cplusplus
};
#endif
};