// Point samples a texture with anti-aliasing along the pixel edges. Intended for low resolution textures.
// https://www.shadertoy.com/view/csX3RH
float4 Sample2DAAData(Texture2D tex, float2 uv, SamplerState texSampler) {
    //return tex.Sample(texSampler, uv);
    float width, height;
    tex.GetDimensions(width, height);
    float2 texsize = float2(width, height); // 64x64
    float2 uvTex = uv * texsize; // 0.1 * 64 -> 6.4
    float2 seam = floor(uvTex + .5); // 6.4 + .5 -> 6
    
    //float2 dd = abs(ddx(uvTex)) + abs(ddy(uvTex));
    //float2 fw = fwidth(uvTex);
    //uvTex = (uvTex - seam) / dd + seam; // // (6.4 - 6) / fwidth(6.4) + 6
    //uvTex = (uvTex - seam) / fw + seam;
    uvTex = (uvTex - seam) / fwidth(uvTex) + seam;

    uvTex = clamp(uvTex, seam - .5, seam + .5);
    float4 color = tex.Sample(texSampler, uvTex / texsize);
    //float4 color = tex.SampleLevel(texSampler, uvTex / texsize, 0);
    return color;
}

float4 Sample2DAAData2(Texture2D tex, float2 uv, SamplerState texSampler) {
    //return tex.Sample(texSampler, uv);
    float width, height;
    tex.GetDimensions(width, height);
    float2 texsize = float2(width, height);
    float2 uvTex = uv * texsize;
    float2 seam = floor(uvTex + .5) + .5;
    //float2 dd = abs(ddx(uvTex)) + abs(ddy(uvTex));
    //uvTex = (uvTex - seam) / dd + seam;
    uvTex = (uvTex - seam) / fwidth(uvTex) + seam;
    //uvTex = 0 + seam;

    uvTex = clamp(uvTex, seam - 1 , seam );
    //uv *= 0.5;

    // what is half a pixel in uvs?
    // 1 / width
    float4 color = tex.Sample(texSampler, uvTex / texsize);
    float4 x = tex.GatherRed(texSampler, uv);
    float4 y = tex.GatherGreen(texSampler, uv);
    float4 z = tex.GatherBlue(texSampler, uv);
    color.r = (x.x + x.y + x.z + x.w) / 4;
    color.g = (y.x + y.y + y.z + y.w) / 4;
    color.b = (z.x + z.y + z.z + z.w) / 4;
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
