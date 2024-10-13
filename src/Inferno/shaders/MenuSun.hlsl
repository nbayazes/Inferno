#include "Common.hlsli"
#include "ObjectVertex.hlsli"

#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),"\
    "CBV(b0),"\
    "CBV(b1),"\
    "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL),"\
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_WRAP," \
        "addressV = TEXTURE_ADDRESS_WRAP," \
        "addressW = TEXTURE_ADDRESS_WRAP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

struct Constants {
    float4x4 WorldMatrix;
    float4 Ambient;
};

ConstantBuffer<FrameConstants> Frame : register(b0);
ConstantBuffer<Constants> Object : register(b1);
SamplerState Sampler : register(s0);
Texture2D Noise : register(t0);

struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    centroid float4 col : COLOR0;
    centroid float3 normal : NORMAL;
    centroid float3 normal_obj : NORMAL1;
    //centroid float3 tangent : TANGENT;
    //centroid float3 bitangent : BITANGENT;
    float3 world : TEXCOORD1;
    float3 object : TEXCOORD2;
    nointerpolation int texid: TEXID;
};

[RootSignature(RS)]
PS_INPUT vsmain(ObjectVertex input) {
    float4x4 wvp = mul(Frame.ViewProjectionMatrix, Object.WorldMatrix);
    PS_INPUT output;
    output.pos = mul(wvp, float4(input.pos, 1));
    output.col = input.col;
    output.uv = input.uv;

    // transform from object space to world space
    output.normal_obj = input.normal;
    output.normal = normalize(mul((float3x3)Object.WorldMatrix, input.normal));
    //output.tangent = normalize(mul((float3x3)Object.WorldMatrix, input.tangent));
    //output.bitangent = normalize(mul((float3x3)Object.WorldMatrix, input.bitangent));
    output.world = mul(Object.WorldMatrix, float4(input.pos, 1)).xyz;
    output.object = input.pos;
    output.texid = input.texid;
    return output;
}

//float3 SampleNoise(float2 uv, float2 offset, float power, float speed) {
//    uv += frac(Frame.Time * speed);
//    float noise = Noise.Sample(Sampler, uv * float2(3, 2)).r;
//    noise = pow(noise, power);
//}

//float NoiseLayer1(float2 uv) {
//    float t = Frame.Time * 2;
//    uv += float2(frac(t * .01), frac(t * .008));
//    float noise = Noise.Sample(Sampler, uv * float2(3, 2)).r;
//    return pow(noise + .2, 4) * 3;
//}

//float NoiseLayer2(float2 uv) {
//    float t = Frame.Time * 2;
//    uv += float2(frac(t * .01), frac(t * .008));
//    float2 uv2 = uv - float2(frac(t * .002), frac(t * .0005));
//    float noise2 = Noise.Sample(Sampler, uv2 * float2(8, 6)).r;
//    float noise3 = Noise.Sample(Sampler, (uv2 + float2(0.4, 0.4)) * float2(8, 6)).r;

//    //float ss = smoothstep(-1, 1, sin(t * 1));
//    float ss = sin(t * .4) * 0.5 + 0.5;
//    noise2 = noise2 * ss + noise3 * (1 - ss) * 0.7;
//    noise2 = pow(noise2 + .2, 3);
//    return noise2 * 10;
//}

float NoiseLayer1(float2 uv, float2 shift, float speed = 1) {
    float t = Frame.Time * speed;
    uv += float2(frac(t * shift.x), frac(t * shift.y));
    float noise = Noise.Sample(Sampler, uv * float2(3, 2)).r;
    //return noise;
    return pow(noise + .2, 4) * 3;
}

float NoiseLayer2(float2 uv, float speed, float theta = 3.14 / 0.64) {
    float t = Frame.Time * speed;
    float2 uv2 = uv - float2(frac(t * .002), frac(t * .0005));
    uv2 += float2(frac(t * .01), frac(t * .008));

    float2 uv3 = (uv + float2(-0.44, -0.65)) * float2(16, 8);
    uv3 =
        float2(uv3.x * cos(theta) - uv3.y * sin(theta),
               uv3.x * sin(theta) + uv3.y * cos(theta));
    uv3 += float2(frac(t * .05), frac(t * -.09)); // must apply sliding after rotation

    //uv3 = uv3 - float2(frac(t * .002), frac(t * .0005));

    float noise = Noise.Sample(Sampler, uv2 * float2(2, 1) * 10).r;
    //float noise2 = Noise.Sample(Sampler, ((uv3 + float2(0.35, 0.4)) * float2(16, 8))).r;
    float noise2 = Noise.Sample(Sampler, uv3).r;
    return pow(.05 + noise, 1.5) * pow(noise2 + .25, 2);

    //noise = pow(saturate((noise - .1) * (noise2 + .1)), 2);
    //return noise;
    //float noise3 = Noise.Sample(Sampler, (uv2 + float2(2, 1) * 0.25) * float2(16, 12)).r;

    //float ss = smoothstep(-1, 1, sin(t * 1));
    //float ss = sin(t * .4) * 0.5 + 0.5;
    //noise2 = noise2 * ss + noise3 * (1 - ss) * 0.7;
    //noise2 = pow(noise2 + .2, 5);
    //return noise2 * 10;
}

float SurfaceNoise(float2 uv, float2 speed) {
    float noise = 0;

    {
        float2 uv2 = uv - frac(speed * float2(.4, -2) * Frame.Time);
        noise += Noise.Sample(Sampler, uv2 * 7).r;
    }

    {
        float theta = 0.9;
        float2 uv3 =
            float2(uv.x * cos(theta) - uv.y * sin(theta),
                   uv.x * sin(theta) + uv.y * cos(theta));

        uv3 += frac(-speed * float2(.5, 1) * Frame.Time);
        noise += saturate(Noise.Sample(Sampler, uv3 * float2(4, 10)).r - .1) * 1.2;
    }

    {
        float theta = 2.4;
        float2 uv3 =
            float2(uv.x * cos(theta) - uv.y * sin(theta),
                   uv.x * sin(theta) + uv.y * cos(theta));

        uv3 += frac(-speed * 2 * Frame.Time);

        float noise2 = saturate(Noise.Sample(Sampler, uv3 * 6).r - .2);
        noise2 *= Noise.Sample(Sampler, uv3 * 2 + .23).r;
        noise += noise2 * 2;

        //noise *= Noise.Sample(Sampler, uv3 * 2 + .23).r * 0.75 + 0.25;
    }

    //return .05 + noise;
    return noise + .1;
    //return pow(noise + .15, 1.2) * .8;
}

float4 psmain(PS_INPUT input) : SV_Target {
    //float t = Frame.Time * 2;
    //float2 uv = input.uv + float2(frac(t * .01), frac(t * .008));
    //float noise = Noise.Sample(Sampler, uv * float2(3, 2)).r;
    //noise = pow(noise + .2, 4) * 3;


    //float t = Frame.Time * 2;
    //float2 uv = input.uv + float2(frac(t * .01), frac(t * .008));
    //float2 uv2 = uv - float2(frac(t * .002), frac(t * .0005));
    //float noise2 = Noise.Sample(Sampler, uv2 * float2(8, 6)).r;
    //float noise3 = Noise.Sample(Sampler, (uv2 + float2(0.4, 0.4)) * float2(8, 6)).r;

    ////float ss = smoothstep(-1, 1, sin(t * 1));
    //float ss = sin(t * .4) * 0.5 + 0.5;
    //noise2 = noise2 * ss + noise3 * (1 - ss) * 0.7;
    //noise2 = pow(noise2, 2);


    //float uvNoise = /*NoiseLayer1(input.uv) * */NoiseLayer2(input.uv, 1);

    //float poleNoiseWeight = pow(saturate(1 *abs(dot(float3(0, 0, 1), input.normal))), 14);
    float poleNoiseWeight = abs(dot(float3(0, 1, 0), normalize(input.normal)));
    poleNoiseWeight = pow(smoothstep(0, 1, pow(poleNoiseWeight * 1.075, 12)), 2);
    //return float4(poleNoiseWeight.xxx, 1);
    //poleNoiseWeight = saturate(poleNoiseWeight * 1.2);

    float2 uvPoles = input.object.xz / 3;
    // .01), frac(t * .008
    //float poleNoise = NoiseLayer1(uvPoles, float2(.01, .008)) * NoiseLayer1(uvPoles + .5, float2(-.02, -.012)));
    //float noise = lerp(uvNoise, poleNoise, 0);
    //noise = noise + 0.01;


    //return float4(NoiseLayer2(input.uv).xxx, 1);

    //float noise = poleNoise * abs(input.normal_obj.z);
    //return float4(noise.xxx, 1);
    //return float4(abs(input.normal_obj.zzz), 1);

    //float2 uvpole = input.object.xz / 2;
    //float2 uv = input.uv + float2(frac(t * .01), frac(t * .008));
    //float pole_noise = Noise.Sample(Sampler, uv * float2(3, 2)).r;
    //noise = pole_noise;

    const float3 color1 = float3(1, .2, 0) * 6;
    //const float3 color2 = float3(80, 35, 15) * .5;
    const float3 ringColor = float3(0.6, 0.5, .2) * 12;

    //return float4(input.uv.y, 0, 0, 1);
    float3 viewDir = normalize(input.world - Frame.Eye);
    //float highlight = pow(saturate(dot(input.normal, -viewDir)), 2);
    //float highlight = saturate(dot(input.normal, -viewDir)), 2);
    float3 color = color1;

    //color *= pow(1 + saturate(NoiseLayer2(input.uv, 0.25)) * 4, 4);

    color *= SurfaceNoise(input.uv, float2(0.0005, 0.0005) * 4);
    //color = pow(color, 2);
    //    pow(1 + saturate(NoiseLayer2(input.uv + .25, 0.25, .11) * NoiseLayer2(input.uv + .5, .5, 3.14 / 2.44)) * 4, 2) * 2;

    float sunspotNoise = NoiseLayer1(input.uv, float2(.75, -.01), 0.002) * NoiseLayer1(input.uv + 0.5, float2(-.525, -.01), .01);
    sunspotNoise += NoiseLayer1(input.uv + .2, float2(1, .25), 0.01) * NoiseLayer1(input.uv + 0.8, float2(-.0001, -.005), 1.5);

    //return float4(NoiseLayer1(input.uv, float2(.75, -.01), 0.02).xxx, 1);
    //float sunspots = pow(1 - max(sunspotNoise, 1), 2);
    //float sunspots = pow(1 - sunspotNoise, 2);
    //return float4(sunspots.xxx, 1);
    //color *= color2 * sunspots;
    //color = lerp(color, color2, saturate(sunspots));
    //color += (sunspots) * color2;
    //color *= (1 - saturate(sunspots) * .8);
    //color = lerp(color, color * .1, saturate(sunspots));
    //float ndotv = saturate(dot(input.normal, -viewDir));
    //return float4((1 - ndotv).xxx, 1);

    //color *= 1 - saturate(pow(sunspotNoise, .3)) * .80;

    //color *= sunspots;

    // Add some depth to center
    //color *= saturate(1 - pow(saturate(dot(input.normal, -viewDir)), .9) * 0.85);

    //color = pow(color, 1.5);
    //float nDotL = pow(dot(input.normal, -viewDir) * 0.5 + 0.5, 2);
    //color = saturate(nDotL).xxx;
    //  * NoiseLayer1(input.uv, float2(.75, -.01), 0.02) * .5)
    //color *= 1.03 - pow(saturate(dot(input.normal, -viewDir)),.1);


    //color *= pow(saturate(dot(input.normal, -viewDir) ), 1).xxx * 2;

    //color *= pow(saturate(dot(input.normal, -viewDir)), 4);
    //color.r *= 2;
    //color.gb = pow(color.gb, 1.5);
    //color.r = pow(color.r, 1.5);
    color = pow(color, 1.5);
    //color = pow(color, 2);

    // outer ring
    //float3 ring = pow(saturate(1 - dot(input.normal, -viewDir)), 2);
    float3 ring = saturate(1 - dot(input.normal, -viewDir));
    color *= 1 + ring * ringColor;

    return float4(color, 1);
}

//float4 psmain(PS_INPUT input) : SV_Target {
//    float t = Frame.Time * 2;
//    //float2 uv = input.uv + float2(frac(t * .01), frac(t * .008));
//    float2 uv0 = input.world.xy / 2;

//    float noise = Noise.Sample(Sampler, uv0 * float2(3, 2)).r;
//    noise = pow(noise + .2, 4) * 3;

//    float2 uv2 = uv0 - float2(frac(t * .002), frac(t * .0005));
//    float noise2 = Noise.Sample(Sampler, uv2 * float2(8, 6)).r;
//    float noise3 = Noise.Sample(Sampler, (uv2 + float2(0.4, 0.4)) * float2(8, 6)).r;

//    //float ss = smoothstep(-1, 1, sin(t * 1));
//    float ss = sin(t * .4) * 0.5 + 0.5;
//    noise2 = noise2 * ss + noise3 * (1 - ss) * 0.7;
//    noise2 = pow(noise2, 2);

//    noise *= noise2 * 10;

//    //return float4(input.uv.y, 0, 0, 1);
//    float3 viewDir = normalize(input.world - Frame.Eye);
//    float highlight = pow(saturate(dot(input.normal, -viewDir)), 2);
//    //float highlight = saturate(dot(input.normal, -viewDir)), 2);
//    float3 color = Object.Ambient.rgb * noise + Object.Ambient.rgb * 0.001;
//    return float4(color * highlight, 1) + pow(float4(0.1, 0.1, 0.1, 0), 2.2);
//}

// Shield shader
//float4 psmain(PS_INPUT input) : SV_Target {
//    float3 rgb =  float3(0.1, 0.1f, 2.5f) * 30;
//    float t = Frame.Time * 2;
//    float2 uv = input.uv + float2(frac(t * .01), frac(t * .008));
//    float noise = Noise.Sample(Sampler, uv * float2(4, 3)).r;
//    noise = saturate(noise - 0.2);
//    noise = pow(noise + .2, 2);

//    float2 uv2 = input.uv - float2(frac(t * .002), frac(t * .0005));
//    float noise2 = Noise.Sample(Sampler, uv2 * float2(6, 4)).r;
//    float noise3 = Noise.Sample(Sampler, (uv2 + float2(0.4, 0.4)) * float2(8, 6)).r;

//    //float ss = smoothstep(-1, 1, sin(t * 1));
//    float ss = sin(t * .4) * 0.5 + 0.5;
//    noise2 = noise2 * ss + noise3 * (1 - ss) * 1;
//    noise2 = pow(saturate(noise2 - 0.1), 4);

//    //noise *= noise2 * 10;

//    //return float4(input.uv.y, 0, 0, 1);
//    float3 viewDir = normalize(input.world - Frame.Eye);
//    //float highlight = saturate(dot(input.normal, -viewDir)), 2);
//    float3 color =
//        rgb * noise * float3(0, 0, 0.4) * pow(saturate(dot(input.normal, -viewDir)), 5)
//        + rgb * noise2 * float3(0.5, 0.5, 1) * pow(saturate(dot(input.normal, -viewDir)), 3);

//    // outer ring
//    color += pow(saturate(1 - dot(input.normal, -viewDir)), 10) * float3(0, 0, 30) * noise;

//    color *= pow(saturate(1.1 - dot(input.normal, -viewDir)), 5) * float3(0, 0, 7);
//    return float4(color, 1) + pow(float4(0.1, 0.1, 0.1, 0), 2.2);
//}
