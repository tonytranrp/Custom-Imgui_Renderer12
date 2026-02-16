// Default glow pixel shader.
// Shared globals (UIShaderGlobals at b0 and UIUserParams at b1) are prepended by ShaderSystem.

float sdRoundRect(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 main(VSOut i) : SV_Target
{
    float2 center = 0.5 * (gRectMinMax.xy + gRectMinMax.zw);
    float2 halfSize = max(float2(1.0, 1.0), 0.5 * (gRectMinMax.zw - gRectMinMax.xy));

    float radius = max(1.0, gParams0.x);
    float intensity = max(0.0, gParams0.y);
    float falloff = max(0.2, gParams0.z);
    float coreStrength = saturate(gParams0.w);

    float outerOnly = gParams1.y;
    float innerGlow = gParams1.z;

    float dist = sdRoundRect(i.PixelPos - center, halfSize, 10.0);
    float dOuter = max(dist, 0.0);

    float sigma = max(1.0, radius * (0.45 / falloff));
    float glow = exp(-(dOuter * dOuter) / (2.0 * sigma * sigma));

    float edge = exp(-(abs(dist) * abs(dist)) / (2.0 * max(1.0, radius * 0.22) * max(1.0, radius * 0.22)));
    glow += edge * coreStrength;

    if (outerOnly > 0.5 && dist < 0.0)
    {
        glow *= 0.2;
    }

    if (innerGlow > 0.5)
    {
        float inner = exp(-(max(-dist, 0.0) * max(-dist, 0.0)) / (2.0 * max(1.0, radius * 0.3) * max(1.0, radius * 0.3)));
        glow += inner * 0.35;
    }

    float alpha = saturate(glow * intensity) * gColor.a * saturate(gParams2.w);
    return float4(gColor.rgb * alpha, alpha);
}
