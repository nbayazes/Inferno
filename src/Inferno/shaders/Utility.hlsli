#ifndef __UTILITY_HLSLI__
#define __UTILITY_HLSLI__

// Encodes a smooth logarithmic gradient for even distribution of precision natural to vision
float LinearToLogLuminance(float x, float gamma = 4.0) {
    return log2(lerp(1, exp2(gamma), x)) / gamma;
}

// This assumes the default color gamut found in sRGB and REC709. The color primaries determine these
// coefficients. Note that this operates on linear values, not gamma space.
float RGBToLuminance(float3 x) {
    return dot(x, float3(0.212671, 0.715160, 0.072169)); // Defined by sRGB/Rec.709 gamut
}

// Converts the linear luminance value to a more subjective "perceived luminance", 
// which could be called the Log-Luminance.
float RGBToLogLuminance(float3 x, float gamma = 4.0) {
    return LinearToLogLuminance(RGBToLuminance(x), gamma);
}

// The standard 32-bit HDR color format. Each float has a 5-bit exponent and no sign bit.
uint Pack_R11G11B10_FLOAT(float3 rgb) {
    // Clamp upper bound so that it doesn't accidentally round up to INF 
    // Exponent=15, Mantissa=1.11111
    rgb = min(rgb, asfloat(0x477C0000));
    uint r = ((f32tof16(rgb.x) + 8) >> 4) & 0x000007FF;
    uint g = ((f32tof16(rgb.y) + 8) << 7) & 0x003FF800;
    uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}


uint PcgHash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Used to advance the PCG state.
uint PcgRandU32(inout uint rngState) {
    uint state = rngState;
    rngState = rngState * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Advances the prng state and returns the corresponding random float.
float PcgRandFloat(inout uint state, float min, float max) {
    state = PcgRandU32(state);
    float f = float(state) / asfloat(0x2f800004u); // 0xFFFFFFFF; // uint max //  asfloat(0x2f800004u)
    return f * (max - min) + min;
}

#endif // __UTILITY_HLSLI__
