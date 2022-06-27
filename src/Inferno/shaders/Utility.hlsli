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

#endif // __UTILITY_HLSLI__
