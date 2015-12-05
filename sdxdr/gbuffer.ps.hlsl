
struct PSInput
{
	float4 position	: SV_POSITION;
	float3 normal : NORMAL;
	float3 pos_world : POSITION;
	float3 pos_local : POSITION1;
	float2 uv		: TEXCOORD0;
};


struct PSOut {
	float4 pos : SV_Target0, norm : SV_Target1, dif : SV_Target2, mat : SV_Target3;
};

cbuffer cb1 : register(b1)
{
	float4x4 world;
	float4 dif, spc;
};

Texture2D		dfs	: register(t0);

SamplerState	smp : register(s0);


PSOut main(PSInput inp) 
{
	PSOut po;
	po.pos = float4(inp.pos_world*.5f + .5f, 0.f);
	po.norm = float4(inp.normal*.5f+.5f, 0.f);
	po.mat = spc;
	po.dif = dif * dfs.Sample(smp, inp.uv*float2(1.f, -1.f));
	return po;
}