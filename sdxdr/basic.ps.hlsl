struct PSInput
{
	float4 position	: SV_POSITION;
	float3 normal : NORMAL;
	float3 pos_world : POSITION;
	float3 pos_local : POSITION1;
	float2 uv		: TEXCOORD0;
};

cbuffer cb0 : register(b0)
{
	float4x4 view_proj;
};
cbuffer cb1 : register(b1)
{
	float4x4 world;
	float4 dif, spc;
};

float4 main(PSInput input) : SV_TARGET
{
	return float4(dif.xyz,1.f);
}