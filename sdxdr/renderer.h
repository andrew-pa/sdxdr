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

	ComPtr<ID3D12RootSignature> basic_root_sig;
	ComPtr<ID3D12PipelineState> basic_pipeline;

	void draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist);
public:
	vector<shared_ptr<render_object>> ros;

	vector<directional_light> directional_lights;

	camera cam;

	renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& render_objects);

	void render();
	~renderer();
};

