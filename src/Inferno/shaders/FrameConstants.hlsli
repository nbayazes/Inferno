cbuffer FrameConstants : register(b0) {
    float4x4 ViewProjectionMatrix;
    float3 Eye;
    float Time; // elapsed game time in seconds
    float2 FrameSize;
    float NearClip, FarClip;
};
