#pragma once
#include "dxut\dxut.h"

#include "DXRDevice.h"

const XMFLOAT4X4 identity_matrix = 
	XMFLOAT4X4(	1, 0, 0, 0, 
				0, 1, 0, 0, 
				0, 0, 1, 0, 
				0, 0, 0, 1 );

struct material {
	XMFLOAT4 diffuse_color, specular_color;
	material() :
	 diffuse_color(1.f,1.f,1.f,1.f), specular_color(1.f,1.f,1.f,1.f) {}
	material(XMFLOAT4 Kd, XMFLOAT4 Ks = XMFLOAT4(1.f, 1.f, 1.f, 1.f)) 
		: diffuse_color(Kd), specular_color(Ks) {}
};

struct render_object {
	XMFLOAT4X4* world;
	material* mat;
	shared_ptr<mesh> msh;
	ComPtr<ID3D12Resource> texture;
	int srv_idx;
	render_object(shared_ptr<mesh> m) : msh(m), world(nullptr), mat(nullptr), texture(nullptr) {}
	render_object(shared_ptr<mesh> m, material mt, XMFLOAT4X4 wld = identity_matrix, ComPtr<ID3D12Resource> tx = nullptr) 
		: msh(m), world(new XMFLOAT4X4(wld)), mat(new material(mt)), texture(tx) {}
};

struct camera {
	XMFLOAT4X4 viewproj;
	XMFLOAT3 position;
};

struct directional_light {
	XMFLOAT4 direction;
	XMFLOAT4 color;

	bool casts_shadow;
	float scene_radius;

	directional_light(XMFLOAT4 d, XMFLOAT4 c, bool cshdw, float scrrad = 32.f)
		: direction(d), color(c), casts_shadow(cshdw), scene_radius(scrrad) {	}
};

/*
	Usual world space deferred rendering

	G-buffer with (positions, normals, material, diffuse color)
	Render lights -> final
	final -> postprocess -> backbuffer
*/
class renderer {
	DXDevice* dv;
	DXRDevice* dr;
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

	void draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist, bool shading = true, XMFLOAT4X4* vp = nullptr);
	ComPtr<ID3D12Resource> blank_texture;
	
	// rtsrv_heap
	// 0: geometry buffer [position]
	// 1: geometry buffer [normal]
	// 2: geometry buffer [material]
	// 3: geometry buffer [diffuse]
	// 4: interm buffer
	// 5: directional light shadow buffers [tex array]
	descriptor_heap rtsrv_heap;
	
	// rtv_heap
	// 0: geometry buffer [position]
	// 1: geometry buffer [normal]
	// 2: geometry buffer [material]
	// 3: geometry buffer [diffuse]
	// 4: interm buffer
	descriptor_heap rtv_heap;


	//-- Geometry Buffer --
	enum class gbuffer_id : uint32_t {
		position = 0, normal, material, diffuse, total_count
	};
	ComPtr<ID3D12Resource> geometry_buffers[(uint32_t)gbuffer_id::total_count];
	pass geometry_pass;
	void create_geometry_buffer_and_pass();
	void render_geometry_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist);
	
	ComPtr<ID3D12Resource> interm_buffer;

	//-- Directional Light Pass --
	ComPtr<ID3D12Resource> shadow_dir_light_buffer; 
	D3D12_VIEWPORT shadow_dir_light_vp;
	descriptor_heap shadow_dir_light_dsv_heap;
	pass dir_light_pass, shadow_dir_light_pass;
	void create_directional_light_pass();
	void render_directional_light_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist);

	//-- Postprocess Pass --
	pass postprocess_pass;
	void create_postprocess_pass();
	void render_postprocess_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist);

	//-- Raytracing --
	void build_raytracing_acceleration_structure();
public:
	vector<shared_ptr<render_object>> ros;

	vector<directional_light> directional_lights;

	camera cam;

	renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& render_objects);

	void render();
	~renderer();
};

