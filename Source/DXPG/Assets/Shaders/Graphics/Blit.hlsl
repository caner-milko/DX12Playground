#include "FullscreenVS.hlsli"
#include "RenderResources.hlsli"

ConstantBuffer<dxpg::BlitResources> Resources : register(b0);

SamplerState Sampler : register(s0);

[RootSignature(BindlessRootSignature)]
float4 PSMain(VSOut IN) : SV_TARGET
{
    Texture2D<float4> Source = ResourceDescriptorHeap[Resources.SourceTextureIndex];
    return Source.Sample(Sampler, IN.TexCoord);
}
