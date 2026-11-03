StructuredBuffer<uint> inputData;
RWStructuredBuffer<uint> outputData;

[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;
    outputData[idx] = inputData[idx] * 3u + 17u;
}
