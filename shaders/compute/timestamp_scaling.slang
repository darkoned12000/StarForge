// M5 Phase 2.3: real compute work whose cost is controlled by a push constant,
// used for GPU timestamp N-vs-2N scaling measurements.
struct PushConstants
{
    uint iterations;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
PushConstants pc;

RWStructuredBuffer<uint> outputData;

[numthreads(64, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint value = tid.x;
    for (uint i = 0; i < pc.iterations; ++i)
    {
        value = value * 3u + 17u;
    }
    outputData[tid.x] = value;
}
