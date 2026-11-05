// GPU culling compute shader.
// Tests instance bounding spheres against 6 frustum planes and writes
// visible instance indices to a compacted array. Atomically increments
// the indirect draw command's instanceCount.

struct PushConstants
{
    float4 planes[6];           // 6 frustum planes (nx, ny, nz, d)
    uint   totalInstanceCount;
    uint   _padding;
};

[[vk::push_constant]] PushConstants pc;

// All buffers bound via descriptors.
RWStructuredBuffer<float4> boundsBuffer;  // InstanceBounds (vec4 per instance)
RWByteAddressBuffer indirectBuffer;       // VkDrawIndirectCommand (16 bytes)
RWStructuredBuffer<uint> visibleIndices;  // compacted visible instance IDs

[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;
    if (idx >= pc.totalInstanceCount)
        return;

    float4 sphere = boundsBuffer[idx]; // (centerX, centerY, centerZ, radius)

    // Test against 6 frustum planes.
    bool visible = true;
    for (int p = 0; p < 6; p++)
    {
        float dist = dot(pc.planes[p].xyz, sphere.xyz) + pc.planes[p].w;
        if (dist < -sphere.w)
        {
            visible = false;
            break;
        }
    }

    if (visible)
    {
        // Atomically increment instanceCount in the indirect command buffer.
        uint offset;
        indirectBuffer.InterlockedAdd(4, 1, offset);
        visibleIndices[offset] = idx;
    }
}