
// Point samples a texture with anti-aliasing along the pixel edges. Intended for low resolution textures.
float4 Sample2DAA(Texture2D tex, float2 uv, SamplerState texSampler) {
    //return tex.Sample(Sampler, uv);
    
    float width, height;
    tex.GetDimensions(width, height);
    float2 texsize = float2(width, height);
    float2 uv_texspace = uv * texsize;
    float2 seam = floor(uv_texspace + .5);
    uv_texspace = (uv_texspace - seam) / fwidth(uv_texspace) + seam;
    uv_texspace = clamp(uv_texspace, seam - .5, seam + .5);
    float4 color = tex.Sample(texSampler, uv_texspace / texsize);
    //color.rgb *= 0.999;
    //color.a = 1;
    //color.a = tex.Sample(Sampler, uv).a;
    //if (color.a < 1)
    //    color = tex.Sample(Sampler, uv);

    //if (color.a < 0.00001)
        //color.rgb = float3(0, 0, 0);
    //color.rgb *= color.a; // apply straight alpha
    color.rgb = pow(color.rgb, 2.2); // sRGB to linear
    //color.a = 1;
    return color;
}
