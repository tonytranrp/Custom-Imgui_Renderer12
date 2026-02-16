// Default shadow pixel shader.
// Shared globals (UIShaderGlobals at b0 and UIUserParams at b1) are prepended by ShaderSystem.

float sdRoundRect(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 main(VSOut i) : SV_Target
{
    float2 rectMin = gRectMinMax.xy + gParams3.xy;
    float2 rectMax = gRectMinMax.zw + gParams3.xy;
    float2 center = 0.5 * (rectMin + rectMax);
    float2 halfSize = max(float2(1.0, 1.0), 0.5 * (rectMax - rectMin));

    float blur = max(0.5, gParams3.z);
    float spread = gParams3.w;
    float inset = gParams1.y;
    float rounding = max(0.0, gParams1.w);
    rounding = min(rounding, max(0.0, min(halfSize.x, halfSize.y) - 0.01));

    float dist = sdRoundRect(i.PixelPos - center, halfSize, rounding);
    float shadowDist = dist - spread;

    float alpha = 0.0;
    if (inset > 0.5)
    {
        float inside = max(-shadowDist, 0.0);
        alpha = exp(-(inside * inside) / (2.0 * blur * blur));
    }
    else
    {
        float outside = max(shadowDist, 0.0);
        alpha = exp(-(outside * outside) / (2.0 * blur * blur));
    }

    alpha *= gColor.a * saturate(gParams2.w);
    return float4(gColor.rgb, saturate(alpha));
}
