// Point samples a texture with anti-aliasing along the pixel edges. Intended for low resolution textures.
float4 Sample2DAAData(Texture2D tex, float2 uv, SamplerState texSampler) {
    //return tex.Sample(texSampler, uv);
    float width, height;
    tex.GetDimensions(width, height);
    float2 texsize = float2(width, height);
    float2 uv_texspace = uv * texsize;
    float2 seam = floor(uv_texspace + .5);
    uv_texspace = (uv_texspace - seam) / fwidth(uv_texspace) + seam;
    uv_texspace = clamp(uv_texspace, seam - .5, seam + .5);
    float4 color = tex.Sample(texSampler, uv_texspace / texsize);
    return color;
}

// Point samples a texture with anti-aliasing along the pixel edges and converts to linear color. Intended for low resolution textures.
float4 Sample2DAA(Texture2D tex, float2 uv, SamplerState texSampler) {
    float4 color = Sample2DAAData(tex, uv, texSampler);
    color.rgb = pow(color.rgb, 2.2); // sRGB to linear
    return color;
}

// Samples a texture and converts to linear color.
float4 Sample2D(Texture2D tex, float2 uv, SamplerState texSampler) {
    float4 color = tex.Sample(texSampler, uv);
    color.rgb = pow(color.rgb, 2.2); // sRGB to linear
    return color;
}
