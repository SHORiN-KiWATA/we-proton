struct cp
{
    float4 pos : POSITION;
};

struct patch_out
{
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
    min16float4 extra : TEXCOORD0;
};

patch_out patch_func(InputPatch<cp, 3> ip)
{
    patch_out o;
    o.edges[0] = o.edges[1] = o.edges[2] = 1.0;
    o.inside = 1.0;
    o.extra = (min16float4)ip[0].pos;
    return o;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_func")]
cp main(InputPatch<cp, 3> ip, uint i : SV_OutputControlPointID)
{
    return ip[i];
}
