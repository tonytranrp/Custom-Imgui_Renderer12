#ifndef UI_SHADER_SHARED_TYPES
#define UI_SHADER_SHARED_TYPES
cbuffer UIShaderGlobals : register(b0)
{
    float4 gRectMinMax;
    float4 gDrawMinMax;
    float4 gColor;
    float4 gParams0;
    float4 gParams1;
    float4 gParams2;
    float4 gParams3;
};

struct VSOut
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
    float2 PixelPos : TEXCOORD1;
};
#endif
