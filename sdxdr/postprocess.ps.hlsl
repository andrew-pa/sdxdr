

cbuffer cb0 : register(b0)
{
	float2 resolution;
};

Texture2D		bb	: register(t0);

SamplerState	smp : register(s0);

float len2(float2 p) {
	return dot(p, p);
}

float3 lumaBasedReinhardToneMapping(float3 color)
{
	float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
	float toneMappedLuma = luma / (1. + luma);
	color *= toneMappedLuma / luma;
	//color = pow(color, float3(1. / gamma));
	return color;
}

float3 whitePreservingLumaBasedReinhardToneMapping(float3 color)
{
	float white = 2.;
	float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
	float toneMappedLuma = luma * (1. + luma / (white*white)) / (1. + luma);
	color *= toneMappedLuma / luma;
	return color;
}

float3 RomBinDaHouseToneMapping(float3 color)
{
	color = exp(-1.0 / (2.72*color + 0.15));
	return color;
}

float3 filmicToneMapping(float3 color)
{
	color = max(float3(0.f, 0.f, 0.f), color - float3(0.004, 0.004, 0.004f));
	color = (color * (6.2 * color + .5)) / (color * (6.2 * color + 1.7) + 0.06);
	return color;
}


float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
	float2 uv = pos.xy / resolution;
	float2 p = uv*2.f - 1.f;
	float4 bbv = bb.Sample(smp, uv);
	bbv.xyz = whitePreservingLumaBasedReinhardToneMapping(bbv.xyz);
	float rf = sqrt(len2(p)) * 1.0f;
	float rf2_1 = rf * rf + 1.0;
	float vig = 1.0 / (rf2_1 * rf2_1);

	return pow(bbv*vig, 1.f / 2.2f);
}