

cbuffer cb0 : register(b0)
{
	float2 resolution;
};

Texture2D		bb	: register(t0);

SamplerState	smp : register(s0);

float len2(float2 p) {
	return dot(p, p);
}

float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
	float2 uv = pos.xy / resolution;
	float2 p = uv*2.f - 1.f;
	float4 bbv = bb.Sample(smp, uv);
	float vig = .8f - len2(p*0.6f);
	return pow(bbv*vig, 1.f / 2.2f);
}