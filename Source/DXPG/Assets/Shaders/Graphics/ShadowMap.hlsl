#include "BindlessRootSignature.hlsli"
#include "RenderResources.hlsli"

ConstantBuffer<dxpg::ShadowMapResources> Resources : register(b0);

struct VSIn
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
    float3 Tangent : TANGENT;
};

struct VSOut
{
    float4 Pos : SV_POSITION;
};

VSOut VSMain(VSIn IN)
{
    VSOut output;
    output.Pos = mul(Resources.MVP, float4(IN.Pos, 1.0));
    return output;
}