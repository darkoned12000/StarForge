// Compute kernel for GPU timestamp testing. Does a fixed number of
// iterations of floating-point work per element.
RWStructuredBuffer<uint> outputData;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint idx = dispatchThreadID.x;
    if (idx >= 262144) return;

    // Do a fixed amount of work.
    float result = float(idx) * 0.001 + 1.0;
    for (uint i = 0; i < 100; i++)
    {
        result = result * 1.00001 + 0.00001;
    }
    // Write result to prevent optimization.
    outputData[idx] = uint(result * 1000.0);
}
