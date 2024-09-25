#pragma once

#include "BindlessRootSignature.hlsli"

struct VSOut
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

[RootSignature(BindlessRootSignature)]
VSOut VSMain(uint vI : SV_VERTEXID)
{
    float2 texcoord = float2(vI & 1, vI >> 1);
    VSOut output;
    output.Position = float4(texcoord.x * 2 - 1, 1 - texcoord.y * 2, 0, 1);
    output.TexCoord = texcoord;
    return output;
}