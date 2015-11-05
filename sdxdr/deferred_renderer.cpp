#include "deferred_renderer.h"

deferred_renderer::deferred_renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& ___ros)
	: dv(d), window(w), ros(___ros)
{
	dv->create_descriptor_heap(ros.size(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, ro_cbv_heap, rocbvhi);
	dv->create_constant_buffer(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		ro_cbv_heap, rocbvhi, ro_cbuf_res, &ro_cbuf_data, 0, ros.size());
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

	dv->create_descriptor_heap(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		rtvheap, rtvhs);
	dv->create_descriptor_heap(1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
		dsvheap, dsvhs);
	
	dv->create_root_signature({
		DXDevice::root_parameterh::constants(16, 0),
		DXDevice::root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1),
	}, {}, basic_root_sig, false);
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
	pdesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.NumRenderTargets = 1;
	pdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pdesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pdesc.SampleDesc.Count = 1;

	chk(dv->device->CreateGraphicsPipelineState(&pdesc, IID_PPV_ARGS(&basic_pipeline)));

	init_gbuffer();

	dv->free_shaders();
}

void deferred_renderer::init_gbuffer() {
	const static float black_color[] = { 0.f, 0.f, 0.f, 0.f };
	D3D12_RESOURCE_DESC desc = dv->renderTargets[0]->GetDesc();
	dv->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, 
		&CD3DX12_CLEAR_VALUE(desc.Format, black_color),
		IID_PPV_ARGS(&gbuf_resource));
	auto dest_rtv_h = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvheap->GetCPUDescriptorHandleForHeapStart());
	auto rtvd = D3D12_RENDER_TARGET_VIEW_DESC();
	rtvd.Format = desc.Format;
	rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvd.Texture2DArray.ArraySize = 1;
	rtvd.Texture2DArray.FirstArraySlice = 0;
	rtvd.Texture2DArray.MipSlice = 1;
	rtvd.Texture2DArray.PlaneSlice = 0;
	for (int i = 0; i < (int)gbufferid::total_count; ++i) {
		dv->device->CreateRenderTargetView(gbuf_resource.Get(), rtvd,
			dest_rtv_h);
		dest_rtv_h.Offset(rtvhs);
	}
}

void deferred_renderer::draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist) {
	cmdlist->SetGraphicsRoot32BitConstants(0, 16, cur_viewproj.m, 0);

	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D12DescriptorHeap* hps[] = { ro_cbv_heap.Get() };
	cmdlist->SetDescriptorHeaps(_countof(hps), hps);

	auto cbv_hdl = CD3DX12_GPU_DESCRIPTOR_HANDLE(ro_cbv_heap->GetGPUDescriptorHandleForHeapStart());
	for (const auto& ro : ros) {
		cmdlist->SetGraphicsRootDescriptorTable(1, cbv_hdl);
		ro->msh->draw(cmdlist);
		cbv_hdl.Offset(rocbvhi);
	}
}

void deferred_renderer::render(XMFLOAT4X4 viewproj) {
	cur_viewproj = viewproj;

	chk(dv->commandAllocator->Reset());
	
	if (dv->commandList) {
		chk(dv->commandList->Reset(dv->commandAllocator.Get(), basic_pipeline.Get()));
	}
	else {
		dv->commandList = dv->create_command_list(D3D12_COMMAND_LIST_TYPE_DIRECT, basic_pipeline);
	}

	dv->commandList->SetGraphicsRootSignature(basic_root_sig.Get());

	dv->commandList->RSSetViewports(1, &dv->viewport);
	dv->commandList->RSSetScissorRects(1, &dv->scissorRect);

	dv->commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle1(dv->rtvHeap->GetCPUDescriptorHandleForHeapStart(), dv->frameIndex, dv->rtvHeapIncr);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dv->dsvHeap->GetCPUDescriptorHandleForHeapStart());
	dv->commandList->OMSetRenderTargets(1, &rtvHandle1, FALSE, &dsvHandle);

	const float clearColor[] = { 0.1f, 0.0f, 0.2f, 1.0f };
	dv->commandList->ClearRenderTargetView(rtvHandle1, clearColor, 0, nullptr);
	dv->commandList->ClearDepthStencilView(dv->dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	draw_geometry(dv->commandList);

	dv->commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	dv->commandList->Close();

	dv->execute_command_list();
}

deferred_renderer::~deferred_renderer() {

	ro_cbuf_res->Unmap(0, nullptr);
	ro_cbuf_data = nullptr;
	for (auto& ro : ros) {
		if (ro) {
			ro->world = nullptr;
			ro->mat = nullptr;
		}
	}

	dv = nullptr;
}
