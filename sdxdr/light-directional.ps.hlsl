

cbuffer cb0 : register(b0)
{
	float2 resolution;
};

cbuffer cb1 : register(b1)
{
	float4 _L;
	float4 Lc;
};


Texture2D		position	: register(t0);
Texture2D		normal		: register(t1);
Texture2D		diffuse		: register(t2);
Texture2D		material	: register(t3);

SamplerState	smp : register(s0);

float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
	float2 uv = pos.xy / resolution;
	
	float3 n = normalize(
		(normal.Sample(smp, uv).xyz*float3(2.f,2.f,2.f)) - float3(1.f,1.f,1.f));
	float3 L = normalize(_L.xyz);
	
	float ndl = max(0.f, dot(n, L));

	return diffuse.Sample(smp, uv)*ndl*Lc;
}