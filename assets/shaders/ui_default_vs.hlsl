VSOut main(uint vertexId : SV_VertexID)
{
    VSOut o;

    float2 uv;
    if (vertexId == 0) uv = float2(0.0, 0.0);
    else if (vertexId == 1) uv = float2(1.0, 0.0);
    else if (vertexId == 2) uv = float2(0.0, 1.0);
    else if (vertexId == 3) uv = float2(0.0, 1.0);
    else if (vertexId == 4) uv = float2(1.0, 0.0);
    else uv = float2(1.0, 1.0);

    float2 pixel = lerp(gDrawMinMax.xy, gDrawMinMax.zw, uv);
    float2 displaySize = max(gParams2.xy, float2(1.0, 1.0));
    float2 ndc = float2(
        (pixel.x / displaySize.x) * 2.0 - 1.0,
        1.0 - (pixel.y / displaySize.y) * 2.0);

    o.Position = float4(ndc, 0.0, 1.0);
    o.UV = uv;
    o.PixelPos = pixel;
    return o;
}
