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
    float rounding = max(0.0, gParams1.w);
    rounding = min(rounding, max(0.0, min(halfSize.x, halfSize.y) - 0.01));

    float radius = max(1.0, gParams0.x);
    float intensity = max(0.0, gParams0.y);
    float falloff = max(0.2, gParams0.z);
    float coreStrength = saturate(gParams0.w);

    float outerOnly = gParams1.y;
    float innerGlow = gParams1.z;

    float dist = sdRoundRect(i.PixelPos - center, halfSize, rounding);
    float dOuter = max(dist, 0.0);
    float outerMask = step(0.0, dist);

    float sigma = max(1.0, radius * (0.45 / falloff));
    float glow = exp(-(dOuter * dOuter) / (2.0 * sigma * sigma)) * outerMask;

    float edgeSigma = max(1.0, radius * 0.22);
    float edge = exp(-(abs(dist) * abs(dist)) / (2.0 * edgeSigma * edgeSigma));
    glow += edge * coreStrength;

    if (innerGlow > 0.5)
    {
        float inner = exp(-(max(-dist, 0.0) * max(-dist, 0.0)) / (2.0 * max(1.0, radius * 0.3) * max(1.0, radius * 0.3)));
        glow += inner * 0.35;
    }

    if (outerOnly > 0.5)
    {
        glow *= outerMask;
    }

    float alpha = saturate(glow * intensity) * gColor.a * saturate(gParams2.w);
    return float4(gColor.rgb * alpha, alpha);
}
