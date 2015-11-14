#include "renderer.h"

const float color_black[] = { 0.f,0.f,0.f,0.f };

#pragma region Initalization
renderer::renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& ___ros) :
	dv(d), window(w), ros(___ros),
	ro_cbv_heap(d->device, ___ros.size(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, L"Render Object CBV Heap"),
	rtv_heap(d->device, (uint32_t)gbuffer_id::total_count, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false),
	rtsrv_heap(d->device, (uint32_t)gbuffer_id::total_count, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, L"Render Target SRV Heap")
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

	const D3D12_INPUT_ELEMENT_DESC input_layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pdesc = {};
	pdesc.InputLayout = { input_layout, _countof(input_layout) };
	pdesc.VS = dv->load_shader(w->GetAssetFullPath(L"basic.vs.cso"));
	pdesc.PS = dv->load_shader(w->GetAssetFullPath(L"basic.ps.cso"));
	pdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pdesc.RasterizerState.FrontCounterClockwise = true;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.NumRenderTargets = 1;
	pdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pdesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pdesc.SampleDesc.Count = 1;

	basic_pass = pass(dv, {
		root_parameterh::constants(16, 0),
		root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1),
	}, {}, pdesc, L"Basic");

	create_geometry_buffer_and_pass();

	dv->free_shaders();
}


void renderer::create_geometry_buffer_and_pass() {
	auto desc = dv->renderTargets[0]->GetDesc();
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	auto rtv_desc = D3D12_RENDER_TARGET_VIEW_DESC{};
	rtv_desc.Format = desc.Format;
	rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	for (uint32_t i = 0; i < (uint32_t)gbuffer_id::total_count; ++i) {
		chk(dv->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&CD3DX12_CLEAR_VALUE(desc.Format, color_black), 
			IID_PPV_ARGS(&(geometry_buffers[i]))));
		dv->device->CreateShaderResourceView(geometry_buffers[i].Get(), nullptr,
			rtsrv_heap.cpu_handle(i));
		dv->device->CreateRenderTargetView(geometry_buffers[i].Get(), &rtv_desc,
			rtv_heap.cpu_handle(i));
	}
	
	const D3D12_INPUT_ELEMENT_DESC input_layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pdesc = {};
	pdesc.InputLayout = { input_layout, _countof(input_layout) };
	pdesc.VS = dv->load_shader(window->GetAssetFullPath(L"basic.vs.cso"));
	pdesc.PS = dv->load_shader(window->GetAssetFullPath(L"gbuffer.ps.cso"));
	pdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pdesc.RasterizerState.FrontCounterClockwise = true;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.NumRenderTargets = (uint32_t)gbuffer_id::total_count;
	for (uint32_t i = 0; i < (uint32_t)gbuffer_id::total_count; ++i)
		pdesc.RTVFormats[i] = desc.Format;
	pdesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pdesc.SampleDesc.Count = 1;
	geometry_pass = pass(dv, basic_pass.root_sig, pdesc, L"Geometry Pass");
}
#pragma endregion

#pragma region Rendering
void renderer::draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist) {
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
void renderer::render_geometry_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist) {
	geometry_pass.apply(cmdlist);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
		rtv_heap.cpu_handle(0),
		rtv_heap.cpu_handle(1),
		rtv_heap.cpu_handle(2),
		rtv_heap.cpu_handle(3),
	};
	cmdlist->OMSetRenderTargets((UINT)gbuffer_id::total_count, rtvs, 
		false, &dv->dsvHeap->cpu_handle());
	
	for (uint32_t i = 0; i < (uint32_t)gbuffer_id::total_count; ++i) {
		cmdlist->ClearRenderTargetView(rtv_heap.cpu_handle(i), color_black, 0, nullptr);
	}
	cmdlist->ClearDepthStencilView(dv->dsvHeap->cpu_handle(), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0., 0, nullptr);

	draw_geometry(cmdlist);
}
#pragma endregion


void renderer::render() {
	chk(dv->commandAllocator->Reset());
	
	if (dv->commandList) {
		chk(dv->commandList->Reset(dv->commandAllocator.Get(), basic_pass.pipeline.Get()));
	}
	else {
		dv->commandList = dv->create_command_list(D3D12_COMMAND_LIST_TYPE_DIRECT, basic_pass.pipeline);
	}

	auto cmdlist = dv->commandList;

	dv->set_default_viewport(cmdlist);
	
	dv->resource_barrier(cmdlist, {
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[0].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[1].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[2].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[3].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
	});

	render_geometry_pass(cmdlist);

	dv->resource_barrier(cmdlist, {
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[0].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[1].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[2].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(geometry_buffers[3].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
	});


	cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	auto rth = dv->rtvHeap->cpu_handle(dv->frameIndex);
	auto dsh = dv->dsvHeap->cpu_handle(0);
	cmdlist->OMSetRenderTargets(1, &rth, false, &dsh);

	cmdlist->ClearRenderTargetView(rth, color_black, 0, nullptr);
	cmdlist->ClearDepthStencilView(dsh, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0., 0, nullptr);
	
	basic_pass.apply(cmdlist);
	
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
