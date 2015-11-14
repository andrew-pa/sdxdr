#pragma once
#include "dxut\dxut.h"
#include "meshloader.h"

#include "renderer.h"

struct test_app : public DXWindow, public DXDevice {
	
	test_app()
		: DXWindow(800, 600, L"sdxdr test"), DXDevice(), dfr(nullptr) {}

	unique_ptr<renderer> dfr;

	StepTimer tim;

	SimpleCamera cam;


	void OnInit() override {
		init_d3d(this);
		vector<shared_ptr<render_object>> ros;

		commandList = create_command_list(D3D12_COMMAND_LIST_TYPE_DIRECT);

		vector<vertex> v; vector<uint32_t> i;
		generate_cube_mesh(v, i, XMFLOAT3(3.f, 3.f, 3.f));
		auto cube_mesh = make_shared<mesh>(this, commandList, v, i);
		ros.push_back(make_shared<render_object>(cube_mesh));
		ros.push_back(make_shared<render_object>(cube_mesh));

		const int size = 3;
		for (int x = 0; x < size; ++x) {
			for (int y = 0; y < size; ++y) {
				for (int z = 0; z < size; ++z) {
					XMFLOAT4X4 wld;
					XMStoreFloat4x4(&wld, XMMatrixTranslation(x*5.f, y*5.f, -25.f + z*5.f));
					ros.push_back(make_shared<render_object>(cube_mesh,
						material(
							XMFLOAT4(	0.1f + ((float)x / (float)size), 
										0.1f + ((float)y / (float)size), 
										0.1f + ((float)z / (float)size), 1.f)),
						wld));
				}
			}
		}


		dfr = make_unique<renderer>(this, this, ros);
		dfr->directional_lights.push_back(
			directional_light(XMFLOAT4(0.f, 1.f, 0.f, 0.f),
				XMFLOAT4(1.f, 1.f, 1.f, 1.f)));

		commandList->Close();

		execute_command_list();
		wait_for_gpu();
		empty_upload_pool();

		cam.Init(XMFLOAT3(2.f, 3.f, 5.f));
	}

	void OnUpdate() override {
		start_frame();
		tim.Tick(nullptr);
		cam.Update(tim.GetElapsedSeconds());
		XMStoreFloat4x4(&dfr->cam.viewproj, cam.GetViewMatrix()*cam.GetProjectionMatrix(45.f, aspectRatio));
		dfr->cam.position = cam.GetPosition();

		float t = tim.GetTotalSeconds();
		XMStoreFloat4x4(dfr->ros[0]->world, XMMatrixRotationRollPitchYaw(t, t*.5f, t*.25f));
		dfr->ros[0]->mat->diffuse_color = XMFLOAT4(sin(t)*.5f+.5f, cos(t)*.5f+.5f, sin(1.f-t)*.5f+.5f, 1.f);
	
		XMStoreFloat4x4(dfr->ros[1]->world, XMMatrixRotationRollPitchYaw(t*.25, t*.5f, t)*XMMatrixTranslation(6.f, 0.f, 0.f));
		dfr->ros[1]->mat->diffuse_color = XMFLOAT4(cos(t)*.5f + .5f, sin(t)*.5f + .5f, cos(1.f - t)*.5f + .5f, 1.f);

	}

	void OnRender() override {
		dfr->render();
		execute_command_list();
		next_frame();
	}

	void OnDestroy() override {
		wait_for_gpu();
		dfr.reset();
		destroy_d3d();
	}

	bool OnEvent(MSG msg) override {
		switch (msg.message)
		{
		case WM_KEYDOWN:
			cam.OnKeyDown(msg.wParam);
			break;

		case WM_KEYUP:
			cam.OnKeyUp(msg.wParam);
			break;
		}
		return false;
	}
};