#define RS "DescriptorTable(SRV(t0)), DescriptorTable(UAV(u0))"

RWTexture2D<float3> SceneColor : register( u0 );
Texture2D<uint> PostBuffer : register( t0 );

float3 Unpack_R11G11B10_FLOAT(uint rgb) {
    float r = f16tof32((rgb << 4) & 0x7FF0);
    float g = f16tof32((rgb >> 7) & 0x7FF0);
    float b = f16tof32((rgb >> 17) & 0x7FE0);
    return float3(r, g, b);
}

[RootSignature(RS)]
[numthreads( 8, 8, 1 )]
void main(uint3 DTid : SV_DispatchThreadID) {
    SceneColor[DTid.xy] = Unpack_R11G11B10_FLOAT(PostBuffer[DTid.xy]);
}
