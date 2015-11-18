#pragma once
#include "dxut\dxut.h"

const XMFLOAT4X4 identity_matrix = 
	XMFLOAT4X4(	1, 0, 0, 0, 
				0, 1, 0, 0, 
				0, 0, 1, 0, 
				0, 0, 0, 1 );

struct material {
	XMFLOAT4 diffuse_color, specular_color;
	material() {}
	material(XMFLOAT4 Kd, XMFLOAT4 Ks = XMFLOAT4(1.f, 1.f, 1.f, 1.f)) 
		: diffuse_color(Kd), specular_color(Ks) {}
};

struct render_object {
	XMFLOAT4X4* world;
	material* mat;
	shared_ptr<mesh> msh;
	render_object(shared_ptr<mesh> m) : msh(m), world(nullptr), mat(nullptr) {}
	render_object(shared_ptr<mesh> m, material mt, XMFLOAT4X4 wld = identity_matrix) : msh(m), world(new XMFLOAT4X4(wld)), mat(new material(mt)) {}
};

struct camera {
	XMFLOAT4X4 viewproj;
	XMFLOAT3 position;
};

struct directional_light {
	XMFLOAT4 direction;
	XMFLOAT4 color;

	directional_light(XMFLOAT4 d, XMFLOAT4 c)
		: direction(d), color(c) {	}
};

struct pass {
	ComPtr<ID3D12RootSignature> root_sig;
	ComPtr<ID3D12PipelineState> pipeline;

	pass() : root_sig(nullptr), pipeline(nullptr) {}
	pass(ComPtr<ID3D12RootSignature> rs, ComPtr<ID3D12PipelineState> ps)
		: root_sig(rs), pipeline(ps) { }

	pass(DXDevice* dv,
		vector<CD3DX12_ROOT_PARAMETER> rs_params, vector<CD3DX12_STATIC_SAMPLER_DESC> stat_smps,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pdsc,
		wstring name = wstring(),
		D3D12_ROOT_SIGNATURE_FLAGS rsf = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) 
	{
		dv->create_root_signature(rs_params, stat_smps, root_sig, true, 
			(name + wstring(L" Root Signature")).c_str(), rsf);
		pdsc.pRootSignature = root_sig.Get();
		chk(dv->device->CreateGraphicsPipelineState(&pdsc, IID_PPV_ARGS(&pipeline)));
		pipeline->SetName((name + wstring(L" Pipeline")).c_str());
	}

	pass(DXDevice* dv, ComPtr<ID3D12RootSignature> exisitingRS,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pdsc,
		wstring name = wstring()) :
		root_sig(exisitingRS)
	{
		pdsc.pRootSignature = exisitingRS.Get();
		chk(dv->device->CreateGraphicsPipelineState(&pdsc, IID_PPV_ARGS(&pipeline)));
		pipeline->SetName((name + wstring(L" Pipeline")).c_str());
	}

	void apply(ComPtr<ID3D12GraphicsCommandList> cmdlist) {
		cmdlist->SetPipelineState(pipeline.Get());
		cmdlist->SetGraphicsRootSignature(root_sig.Get());
	}
};

/*
	Usual world space deferred rendering

	G-buffer with (positions, normals, material, diffuse color)
	Render lights -> final
	final -> postprocess -> backbuffer
*/
class renderer {
	DXDevice* dv;
	DXWindow* window;

	descriptor_heap ro_cbv_heap;
	
	ComPtr<ID3D12Resource> ro_cbuf_res;
	struct rocbuf {
		XMFLOAT4X4 wld;
		material mat;
		uint8_t padding[256 - (sizeof(XMFLOAT4X4) + sizeof(material))];
	};
	rocbuf* ro_cbuf_data;

	unique_ptr<mesh> fsq;

	pass basic_pass;

	void draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist);
	
	// rtsrv_heap
	// 0: geometry buffer [position]
	// 1: geometry buffer [normal]
	// 2: geometry buffer [material]
	// 3: geometry buffer [diffuse]
	descriptor_heap rtsrv_heap;
	
	// rtv_heap
	// 0: geometry buffer [position]
	// 1: geometry buffer [normal]
	// 2: geometry buffer [material]
	// 3: geometry buffer [diffuse]
	descriptor_heap rtv_heap;

	//-- Geometry Buffer --
	enum class gbuffer_id : uint32_t {
		position = 0, normal, material, diffuse, total_count
	};
	ComPtr<ID3D12Resource> geometry_buffers[(uint32_t)gbuffer_id::total_count];
	pass geometry_pass;
	void create_geometry_buffer_and_pass();
	void render_geometry_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist);
	
	pass test_pass;

public:
	vector<shared_ptr<render_object>> ros;

	vector<directional_light> directional_lights;

	camera cam;

	renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& render_objects);

	void render();
	~renderer();
};

