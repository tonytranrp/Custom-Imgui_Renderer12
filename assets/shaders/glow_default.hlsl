// Default glow pixel shader.
// Shared globals (UIShaderGlobals at b0 and UIUserParams at b1) are prepended by ShaderSystem.

float sdRoundRect(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float getGlowPower(float dist, float radius, float exponent, float softness)
{
    float safeDist = sqrt(dist * dist + softness * softness);
    float ratio = max(0.0, radius / safeDist);
    return pow(ratio, exponent);
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

    float mode = gParams1.x;
    float outerOnly = gParams1.y;
    float innerGlow = gParams1.z;
    float quality = gParams3.x;
    float supportRadiusPx = max(1.0, gParams3.y);
    float edgeFadePx = max(1.0, gParams3.z);
    float alphaEpsilon = max(0.0001, gParams3.w);

    float dist = sdRoundRect(i.PixelPos - center, halfSize, rounding);
    float dOuter = max(dist, 0.0);
    float outerMask = step(0.0, dist);
    float2 outside = max(abs(i.PixelPos - center) - halfSize + rounding, 0.0);
    float rectLikeOuter = outside.x + outside.y;
    float boxOuter = max(outside.x, outside.y);

    float modeRadiusScale = 1.0;
    float modeEnergyScale = 1.0;
    float glowExponent = 1.35;
    float coreWidth = 2.2;
    float outerWeight = 1.0;
    float tailRectBlend = 0.45;
    float tailRangeFactor = 3.2;
    float tailDampScale = 1.0;
    if (mode > 0.5 && mode < 1.5) // NeonTube
    {
        modeRadiusScale = 0.84;
        modeEnergyScale = 1.08;
        glowExponent = 1.72;
        coreWidth = 1.5;
        outerWeight = 0.84;
        tailRectBlend = 0.88;
        tailRangeFactor = 2.2;
        tailDampScale = 1.45;
    }
    else if (mode >= 1.5) // AmbientSoft
    {
        modeRadiusScale = 1.18;
        modeEnergyScale = 0.82;
        glowExponent = 1.18;
        coreWidth = 2.8;
        outerWeight = 1.15;
        tailRectBlend = 0.32;
        tailRangeFactor = 3.6;
        tailDampScale = 0.92;
    }

    float qualityRadiusScale = 1.0;
    float qualityEnergyScale = 1.0;
    if (quality < 0.5) // Performance
    {
        qualityRadiusScale = 0.90;
        qualityEnergyScale = 0.86;
    }
    else if (quality >= 1.5) // Ultra
    {
        qualityRadiusScale = 1.08;
        qualityEnergyScale = 1.06;
    }

    float falloffScale = clamp(1.0 / falloff, 0.35, 2.8);
    float radiusEff = max(1.0, radius * modeRadiusScale * qualityRadiusScale * falloffScale);
    float tailDist = lerp(dOuter, rectLikeOuter, tailRectBlend);
    tailDist = lerp(tailDist, boxOuter, tailRectBlend * 0.35);
    float tailRange = max(1.0, radiusEff * tailRangeFactor / tailDampScale);
    float tailDamp = exp(-tailDist / tailRange);
    float powerSoftness = max(0.8, radiusEff * 0.06);
    float outerHalo = getGlowPower(tailDist + 1.0, radiusEff, glowExponent, powerSoftness) * tailDamp * outerMask * outerWeight;

    float aa = max(0.35, fwidth(dist) * 1.35);
    float edgeSigma = max(0.8, coreWidth + radiusEff * 0.08);
    float edgeSigmaAA = edgeSigma + aa;
    float edgeBand = exp(-(abs(dist) * abs(dist)) / (2.0 * edgeSigmaAA * edgeSigmaAA));
    float coreLine = 1.0 - smoothstep(0.35 - aa, coreWidth + 1.2 + aa, abs(dist));

    float innerTerm = 0.0;
    if (innerGlow > 0.5 && outerOnly < 0.5)
    {
        float inside = max(-dist, 0.0);
        float innerDepth = 1.0 - smoothstep(0.0, radiusEff * 0.75 + 2.0 + aa * 6.0, inside);
        innerTerm = innerDepth * (mode >= 1.5 ? 0.16 : (mode > 0.5 ? 0.30 : 0.22));
    }

    float glow = outerHalo + edgeBand * (0.45 + coreStrength * 1.20) + coreLine * (0.40 + coreStrength * 1.60) + innerTerm;
    if (outerOnly > 0.5)
    {
        glow = outerHalo + edgeBand * (0.16 + coreStrength * 0.35) * outerMask;
    }

    float energyScale = intensity * modeEnergyScale * qualityEnergyScale * gColor.a * saturate(gParams2.w);
    float energy = max(0.0, glow * energyScale);

    float supportT = saturate((supportRadiusPx - tailDist) / max(1.0, edgeFadePx + aa * 2.0));
    float supportMask = supportT * supportT * (3.0 - 2.0 * supportT);

    float edgeDist = min(
        min(i.PixelPos.x - gDrawMinMax.x, gDrawMinMax.z - i.PixelPos.x),
        min(i.PixelPos.y - gDrawMinMax.y, gDrawMinMax.w - i.PixelPos.y));
    float containerT = saturate(edgeDist / max(1.0, edgeFadePx + aa * 2.0));
    float containerMask = containerT * containerT * (3.0 - 2.0 * containerT);

    float mask = supportMask * containerMask;
    energy *= mask;

    float alphaRaw = 1.0 - exp(-energy * 0.85);
    float epsilonDenom = max(0.0001, 1.0 - alphaEpsilon);
    float alpha = saturate((alphaRaw - alphaEpsilon) / epsilonDenom);

    float coreWhiten = saturate(coreLine * (0.35 + coreStrength * 0.65));
    float3 emissive = gColor.rgb * energy + coreWhiten * alpha * (0.70 + 0.60 * modeEnergyScale);
    float3 color = 1.0 - exp(-emissive);

    // Additive pipeline is SrcAlpha + One, so return straight emissive RGB (not premultiplied).
    return float4(color, alpha);
}
