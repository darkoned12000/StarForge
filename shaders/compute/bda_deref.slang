// M5 Phase 2.3: prove a compute shader can dereference a 64-bit device address
// through vk::BufferPointer and write through it.
struct PushConstants
{
    uint64_t srcAddress;
    uint64_t dstAddress;
    uint32_t count;
    uint32_t padding;
};

[[vk::push_constant]]
PushConstants pc;

typedef vk::BufferPointer<uint> UintPtr;

[numthreads(64, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint index = tid.x;
    if (index >= pc.count) return;

    UintPtr src = { pc.srcAddress + uint64_t(index) * 4u };
    UintPtr dst = { pc.dstAddress + uint64_t(index) * 4u };

    uint value = src.Get();
    dst.Get() = value * 3u + 17u;
}
