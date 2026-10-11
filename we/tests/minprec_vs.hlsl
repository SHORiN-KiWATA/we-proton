struct vs_in
{
    min16float4 pos : POSITION;
    min16uint2 id : TEXCOORD0;
    float2 tex : TEXCOORD1;
    uint instance : SV_InstanceID;
};

struct vs_out
{
    float4 pos : SV_Position;
    min16float2 tex : TEXCOORD0;
    min16int2 id : TEXCOORD1;
};

vs_out main(vs_in i)
{
    vs_out o;
    o.pos = i.pos + i.instance;
    o.tex = i.tex + (min16float2)i.id;
    o.id = (min16int2)i.id;
    return o;
}
