#include "renderer.h"

const float color_black[] = { 0.f,0.f,0.f,0.f };

#pragma region Initalization
renderer::renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& ___ros) :
	dv(d), window(w), ros(___ros),
	ro_cbv_heap(d->device, ___ros.size(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, L"Render Object CBV Heap")
{
	dv->create_constant_buffer(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		ro_cbv_heap, ro_cbuf_res, &ro_cbuf_data, 0, ros.size());
	ro_cbuf_res->SetName(L"Render Object CBuf Data");

	for (int i = 0; i < ros.size(); ++i) {
		if (ros[i]->world != nullptr) {
			memcpy(&ro_cbuf_data[i].wld, ros[i]->world, sizeof(XMFLOAT4X4));
			delete ros[i]->world;
			ros[i]->world = &ro_cbuf_data[i].wld;
		}
		else {
			ros[i]->world = &ro_cbuf_data[i].wld;
			XMStoreFloat4x4(ros[i]->world, XMMatrixIdentity());
		}
		if (ros[i]->mat != nullptr) {
			memcpy(&ro_cbuf_data[i].mat, ros[i]->mat, sizeof(material));
			delete ros[i]->mat;
			ros[i]->mat = &ro_cbuf_data[i].mat;
		}
		else {
			ros[i]->mat = &ro_cbuf_data[i].mat;
			*ros[i]->mat = material();
		}
	}
	

	dv->create_root_signature({
		DXDevice::root_parameterh::constants(16, 0),
		DXDevice::root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1),
	}, {}, basic_root_sig, false);
	basic_root_sig->SetName(L"Basic Root Sig");

	const D3D12_INPUT_ELEMENT_DESC input_layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pdesc = {};
	pdesc.InputLayout = { input_layout, _countof(input_layout) };
	pdesc.pRootSignature = basic_root_sig.Get();
	pdesc.VS = dv->load_shader(w->GetAssetFullPath(L"basic.vs.cso"));
	pdesc.PS = dv->load_shader(w->GetAssetFullPath(L"basic.ps.cso"));
	pdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pdesc.RasterizerState.FrontCounterClockwise = true;
	//pdesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	//pdesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.NumRenderTargets = 1;
	pdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pdesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pdesc.SampleDesc.Count = 1;

	chk(dv->device->CreateGraphicsPipelineState(&pdesc, IID_PPV_ARGS(&basic_pipeline)));


	dv->free_shaders();
}

#pragma endregion

#pragma region Rendering
void renderer::draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist) {
	cmdlist->SetGraphicsRootSignature(basic_root_sig.Get());
	cmdlist->SetDescriptorHeaps(1, &ro_cbv_heap.heap);
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdlist->SetGraphicsRoot32BitConstants(0, 16, cam.viewproj.m, 0);
	for (int i = 0; i < ros.size(); ++i) {
		cmdlist->SetGraphicsRootDescriptorTable(1,
			ro_cbv_heap.gpu_handle(i));
		ros[i]->msh->draw(cmdlist);
	}
}

#pragma region Passes

#pragma endregion


void renderer::render() {
	chk(dv->commandAllocator->Reset());
	
	if (dv->commandList) {
		chk(dv->commandList->Reset(dv->commandAllocator.Get(), basic_pipeline.Get()));
	}
	else {
		dv->commandList = dv->create_command_list(D3D12_COMMAND_LIST_TYPE_DIRECT, basic_pipeline);
	}

	auto cmdlist = dv->commandList;

	dv->set_default_viewport(cmdlist);

	cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	auto rth = dv->rtvHeap->cpu_handle(dv->frameIndex);
	auto dsh = dv->dsvHeap->cpu_handle(0);
	cmdlist->OMSetRenderTargets(1, &rth, false, &dsh);

	cmdlist->ClearRenderTargetView(rth, color_black, 0, nullptr);
	cmdlist->ClearDepthStencilView(dsh, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0., 0, nullptr);

	draw_geometry(cmdlist);

	cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	chk(cmdlist->Close());
}
#pragma endregion

renderer::~renderer() {

	ro_cbuf_res->Unmap(0, nullptr);
	ro_cbuf_data = nullptr;
	for (auto& ro : ros) {
		if (ro) {
			ro->world = nullptr;
			ro->mat = nullptr;
		}
	}

	dv = nullptr, window = nullptr;
}
