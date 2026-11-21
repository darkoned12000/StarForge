// Shared output stage for HDR-ready shaders.
//
// The shipped frame renders straight into an 8-bit sRGB swapchain, so linear
// values above 1.0 would clip per channel: an amber glow turns yellow-white,
// a blue-white star turns cyan. kHdrOutput is a Vulkan specialization constant
// (id 0) that the future offscreen RGBA16F path will set to true. It defaults
// to false, so the direct path gets a hue-preserving shoulder while the HDR
// path passes linear values through untouched - the shader files do not need
// to change twice.
[vk::constant_id(0)] const bool kHdrOutput = false;

float3 outputColor(float3 c)
{
    if (kHdrOutput) return c;

    // Hard-knee shoulder: values at or below the knee pass through untouched
    // (preserving the whole diffuse range), brighter values compress smoothly
    // into [knee, 1) instead of clipping.
    const float knee = 0.8f;
    const float range = 1.0f - knee;
    float3 over = max(c - float3(knee, knee, knee), float3(0.0f));
    return min(c, float3(knee, knee, knee)) + range * (1.0f - exp(-over / range));
}
