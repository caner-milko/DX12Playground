#include "FullscreenVS.hlsli"
#include "RenderResources.hlsli"
#include "ConstantBuffers.hlsli"

ConstantBuffer<dxpg::LightingResources> Resources : register(b0);

SamplerState PointSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

struct PSIn
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float3 WorldPosFromDepth(dxpg::LightTransformBuffer lightTransform, float2 texCoord, float depth)
{
    float2 screenPos = texCoord * 2 - 1;
    screenPos.y = -screenPos.y;
    float4 clipPos = float4(screenPos, depth, 1);
    float4 viewPos = mul(lightTransform.CamInverseProjection, clipPos);
    viewPos /= viewPos.w;
    float4 worldPos = mul(lightTransform.CamInverseView, viewPos);
    return worldPos.xyz;
}

float3 LightSpaceFromWorld(dxpg::LightTransformBuffer lightTransform, float3 worldPos)
{
    float4 lightSpacePos = mul(lightTransform.LightViewProjection, float4(worldPos, 1));
    lightSpacePos /= lightSpacePos.w;
    // Transform from [-1, 1] to [0, 1]
    lightSpacePos.xy = lightSpacePos.xy * 0.5 + 0.5;
    lightSpacePos.y = 1 - lightSpacePos.y;
    return lightSpacePos.xyz;
}

float shadow_offset_lookup(Texture2D<float> shadowMap, SamplerComparisonState shadowMapSampler, float3 loc, float2 offset)
{
    return shadowMap.SampleCmp(shadowMapSampler, loc.xy + offset * float2(1.0 / 1024.0, 1.0 / 1024.0), loc.z - 1e-3).r;
}

float4 PSMain(PSIn IN) : SV_TARGET
{
    Texture2D<float4> albedoTex = ResourceDescriptorHeap[Resources.AlbedoTextureIndex];
    Texture2D<float4> normalTex = ResourceDescriptorHeap[Resources.NormalTextureIndex];
    Texture2D<float> depthMap = ResourceDescriptorHeap[Resources.DepthTextureIndex];
    
    Texture2D<float> shadowMap = ResourceDescriptorHeap[Resources.ShadowMapTextureIndex];
    SamplerComparisonState shadowMapSampler = SamplerDescriptorHeap[Resources.ShadowMapSamplerIndex];
    ConstantBuffer<dxpg::LightDataBuffer> lightData = ResourceDescriptorHeap[Resources.LightDataBufferIndex];
    ConstantBuffer<dxpg::LightTransformBuffer> lightTransform = ResourceDescriptorHeap[Resources.LightTransformBufferIndex];
    
    float3 normal = normalTex.Sample(PointSampler, IN.TexCoord).rgb;
    float3 albedo = albedoTex.Sample(PointSampler, IN.TexCoord).rgb;
    
    float diffuse = saturate(dot(normal, -lightData.DirectionOrPosition));
    
    float halfVector = saturate(dot(normal, normalize(-lightData.DirectionOrPosition + float3(0, 0, 1))));
    float specular = pow(halfVector, 32);
    
    diffuse *= lightData.Intensity;
    
    float depth = depthMap.Sample(PointSampler, IN.TexCoord);
    float3 worldPos = WorldPosFromDepth(lightTransform, IN.TexCoord, depth);
    float3 lightSpacePos = LightSpaceFromWorld(lightTransform, worldPos);
    
    bool inBounds = lightSpacePos.x > 0 && lightSpacePos.x < 1 && lightSpacePos.y > 0 && lightSpacePos.y < 1 && lightSpacePos.z > 0 && lightSpacePos.z < 1;
    
    float shadowCoeff = 1;
    if (inBounds)
    {
        float sum = 0;
        float x, y;
        for (y = -1.5; y <= 1.5; y += 1.0)
            for (x = -1.5; x <= 1.5; x += 1.0)
                sum += shadow_offset_lookup(shadowMap, shadowMapSampler, lightSpacePos, float2(x, y));
        shadowCoeff = sum / 16.0;
    }
    
    shadowCoeff = 1 - saturate(shadowCoeff);
    
    diffuse *= shadowCoeff;
    specular *= shadowCoeff;
    
    
    return float4((diffuse * lightData.Color + float3(0.1, 0.1, 0.1) * specular + lightData.AmbientColor) * albedo, 1);
}
