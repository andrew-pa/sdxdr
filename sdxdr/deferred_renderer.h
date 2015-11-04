#pragma once
#include "dxut\dxut.h"

const XMFLOAT4X4 identity_matrix = 
	XMFLOAT4X4(	1, 0, 0, 0, 
				0, 1, 0, 0, 
				0, 0, 1, 0, 
				0, 0, 0, 1 );

struct material {
	XMFLOAT4 diffuse_color;
};

struct render_object {
	XMFLOAT4X4* world;
	material* mat;
	shared_ptr<mesh> msh;
	render_object(shared_ptr<mesh> m) : msh(m), world(nullptr), mat(nullptr) {}
	render_object(shared_ptr<mesh> m, material mt, XMFLOAT4X4 wld = identity_matrix) : msh(m), world(new XMFLOAT4X4(wld)), mat(new material(mt)) {}
};


/*
	Usual world space deferred rendering

	G-buffer with (positions, normals, material, diffuse color)
	Render lights -> final
	final -> postprocess -> backbuffer
*/

class deferred_renderer {
	DXDevice* dv;

	ComPtr<ID3D12DescriptorHeap> ro_cbv_heap; uint32_t rocbvhi;

	ComPtr<ID3D12Resource> ro_cbuf_res;
	struct rocbuf {
		XMFLOAT4X4 wld;
		material mat;
		uint8_t padding[256 - (sizeof(XMFLOAT4X4) + sizeof(material))];
	};
	rocbuf* ro_cbuf_data;

	ComPtr<ID3D12RootSignature> basic_root_sig;
	ComPtr<ID3D12PipelineState> basic_pipeline;

public:
	vector<shared_ptr<render_object>> ros;

	deferred_renderer(DXDevice* d) : dv(d) {}
	void init(DXWindow* w, const vector<shared_ptr<render_object>>& render_objects);
	void render(XMFLOAT4X4 viewproj);
	void destroy();
	~deferred_renderer() { dv = nullptr; }
};

