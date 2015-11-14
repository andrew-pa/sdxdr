
struct PSOut {
	float4 pos : SV_TARGET0, norm : SV_TARGET1, mat : SV_TARGET2, dif : SV_TARGET3;
};

PSOut main() 
{
	PSOut po;
	po.pos = float4(1.f, 0.f, 0.f, 0.f);
	po.norm = float4(0.f, 1.f, 0.f, 0.f);
	po.mat = float4(0.f, 0.f, 1.f, 0.f);
	po.dif = float4(0.f, 0.1f, 0.1f, 1.f);
	return po;
}