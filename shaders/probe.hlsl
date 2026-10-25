struct InstanceData
{
    row_major float4x4 World;
    float4 BaseColor;
};

StructuredBuffer<InstanceData> instances : register(t0, space0);

// Raw output deliberately avoids introducing a second structured-buffer issue.
RWByteAddressBuffer results : register(u0, space1);

[numthreads(1, 1, 1)]
void CSMain()
{
    uint count, stride;
    instances.GetDimensions(count, stride);

    // The generated data always contains at least eight records per block.
    InstanceData first = instances[0];
    InstanceData second = instances[1];
    InstanceData eighth = instances[7];

    results.Store4(0, uint4(
        count, stride,
        asuint(first.World[0][0]),
        asuint(second.World[0][0])));

    results.Store4(16, uint4(
        asuint(eighth.World[0][0]),
        asuint(first.BaseColor.x),
        asuint(eighth.BaseColor.x),
        0x12345678));
}
