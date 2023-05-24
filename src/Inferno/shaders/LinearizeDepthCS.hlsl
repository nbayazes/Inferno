#define RS \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 2), " \
    "DescriptorTable(UAV(u0, numDescriptors = 1))," \
    "DescriptorTable(SRV(t0, numDescriptors = 1)),"

struct Arguments {
    float Near;
    float Far;
};

ConstantBuffer<Arguments> Args : register(b0);
RWTexture2D<float> LinearZ : register(u0);
Texture2D<float> Depth : register(t0);

[RootSignature(RS)]
[numthreads(16, 16, 1)]
void main(uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID) {
    //LinearZ[DTid.xy] = 1.0 / (ZMagic * Depth[DTid.xy] + 1.0);
    LinearZ[DTid.xy] = Args.Near / (Args.Far + Depth[DTid.xy] * (Args.Near - Args.Far));
    
    //z = 1 - z;
    //LinearZ[DTid.xy] = (near / (far + near - z * (near - far))) ;
    //LinearZ[DTid.xy] = (2 * near * far) / (far + near - z * (far - near));
    
    //float z_n = 2.0 * Depth[DTid.xy] - 1.0;
    //float z_e = zNear * zFar / (zFar + Depth[DTid.xy] * (zFar - zNear));
    
    //zNear * zFar / (zFar + d * (zNear - zFar));
    //ZMagic = (3000 - 2) / 3000;
    //LinearZ[DTid.xy] = near * far / (far + ZMagic * (far - near));
    
    //float ndc = z * 2.0 - 1.0;
    //LinearZ[DTid.xy] = (2.0 * near * far) / (far + near - ndc * (far - near));
    //LinearZ[DTid.xy] = z < 1 ? 1 : 0;
}
