#pragma once
#include "dxut\dxut.h"
#include "meshloader.h"

#include "deferred_renderer.h"

struct test_app : public DXWindow, public DXDevice {
	
	test_app()
		: DXWindow(800, 600, L"sdxdr test"), DXDevice(), dfr(nullptr) {}

	unique_ptr<deferred_renderer> dfr;

	StepTimer tim;

	SimpleCamera cam;


	void OnInit() override {
		init_d3d(this);
		vector<shared_ptr<render_object>> ros;
		vector<ComPtr<ID3D12Resource>> upl_res(2);

		commandList = create_command_list(D3D12_COMMAND_LIST_TYPE_DIRECT);

		vector<vertex> v; vector<uint32_t> i;
		generate_cube_mesh(v, i, XMFLOAT3(3.f, 3.f, 3.f));
		auto cube_mesh = make_shared<mesh>(device, commandList, v, i, upl_res[0], upl_res[1]);
		ros.push_back(make_shared<render_object>(cube_mesh));
		ros.push_back(make_shared<render_object>(cube_mesh));

		commandList->Close();

		execute_command_list();

		dfr = make_unique<deferred_renderer>(this, this, ros);

		cam.Init(XMFLOAT3(2.f, 3.f, 5.f));
	}

	void OnUpdate() override {
		start_frame();
		tim.Tick(nullptr);
		cam.Update(tim.GetElapsedSeconds());

		float t = tim.GetTotalSeconds();
		XMStoreFloat4x4(dfr->ros[0]->world, XMMatrixRotationRollPitchYaw(t, t*.5f, t*.25f));
		dfr->ros[0]->mat->diffuse_color = XMFLOAT4(sin(t)*.5f+.5f, cos(t)*.5f+.5f, sin(1.f-t)*.5f+.5f, 1.f);
	
		XMStoreFloat4x4(dfr->ros[1]->world, XMMatrixRotationRollPitchYaw(t*.25, t*.5f, t)*XMMatrixTranslation(6.f, 0.f, 0.f));
		dfr->ros[1]->mat->diffuse_color = XMFLOAT4(cos(t)*.5f + .5f, sin(t)*.5f + .5f, cos(1.f - t)*.5f + .5f, 1.f);

	}

	void OnRender() override {
		
		XMFLOAT4X4 vp; XMStoreFloat4x4(&vp, cam.GetViewMatrix()*cam.GetProjectionMatrix(45.f, aspectRatio));
		dfr->render(vp);

		next_frame();
	}

	void OnDestroy() override {
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