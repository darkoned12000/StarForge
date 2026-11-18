// M5 Phase 2.3: a kernel that actually uses y and z dispatch dimensions.
RWStructuredBuffer<uint> outputData;

[numthreads(4, 4, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint index = tid.x + tid.y * 8u + tid.z * 64u;
    outputData[index] = index + 1000u;
}
