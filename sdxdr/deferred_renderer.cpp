#include "deferred_renderer.h"

void deferred_renderer::init(DXWindow* w, const vector<shared_ptr<render_object>>& ___ros) {
	ros = ___ros;

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

	dv->free_shaders();

}

void deferred_renderer::render(XMFLOAT4X4 viewproj) {
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

	dv->commandList->SetGraphicsRoot32BitConstants(0, 16, viewproj.m, 0);

	dv->commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D12DescriptorHeap* hps[] = {ro_cbv_heap.Get()};
	dv->commandList->SetDescriptorHeaps(_countof(hps), hps);

	auto cbv_hdl = CD3DX12_GPU_DESCRIPTOR_HANDLE(ro_cbv_heap->GetGPUDescriptorHandleForHeapStart());
	for (const auto& ro : ros) {
		dv->commandList->SetGraphicsRootDescriptorTable(1, cbv_hdl);
		ro->msh->draw(dv->commandList);
		cbv_hdl.Offset(rocbvhi);
	}

	dv->commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	dv->commandList->Close();

	dv->execute_command_list();
}

void deferred_renderer::destroy() {
	ro_cbuf_res->Unmap(0, nullptr);
	ro_cbuf_data = nullptr;
	for (auto& ro : ros) {
		if (ro) {
			ro->world = nullptr;
			ro->mat = nullptr;
		}
	}
}
