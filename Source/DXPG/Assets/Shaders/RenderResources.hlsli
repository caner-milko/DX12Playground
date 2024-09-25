#pragma once

#ifdef __cplusplus

#define float4 DirectX::XMFLOAT4
#define float3 DirectX::XMFLOAT3
#define float2 DirectX::XMFLOAT2

#define uint uint32_t

#define float4x4 DirectX::XMMATRIX

#endif

namespace dxpg
{
#ifdef __cplusplus
namespace hlsl
{
#endif
    
struct StaticMeshResources
{
    float4x4 MVP;
    float4x4 Normal;
    uint MaterialBufferIndex;
};

struct ShadowMapResources
{
    float4x4 MVP;
};
    
struct LightingResources
{
    uint AlbedoTextureIndex;
    uint NormalTextureIndex;
    uint DepthTextureIndex;
        
    uint ShadowMapTextureIndex;
    uint ShadowMapSamplerIndex;
    uint LightDataBufferIndex;
    uint LightTransformBufferIndex;
};
    
struct BlitResources
{
    uint SourceTextureIndex;
};
    
#ifdef __cplusplus
};
#endif
};