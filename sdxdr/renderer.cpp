#include "renderer.h"

const float color_black[] = { 0.f,0.f,0.f,0.f };
const float color_white[] = { 1.f,1.f,1.f,1.f };

#pragma region Initalization
uint32_t count_textured_objects(const vector<shared_ptr<render_object>>& ros) {
	uint32_t c = 0;
	for (const auto& ro : ros) {
		if (ro->texture != nullptr) c++;
	}
	return c;
}

renderer::renderer(DXDevice* d, DXWindow* w, const vector<shared_ptr<render_object>>& ___ros) :
	dv(d), window(w), ros(___ros),
	ro_cbv_heap(d->device, ___ros.size() + count_textured_objects(___ros) + 1/*blank texture*/, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, L"Render Object CBV Heap"),
	rtv_heap(d->device, (uint32_t)gbuffer_id::total_count + 1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false, L"Renderer Render Target Heap"),
	rtsrv_heap(d->device, (uint32_t)gbuffer_id::total_count + 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, L"Render Target SRV Heap"),
	shadow_dir_light_dsv_heap(d->device, 1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false, L"Directional Light Shadow Depth Stencil Heap"),
	fsq(mesh::create_full_screen_quad(d, d->commandList))
{
	//this buffer should have all the render_objects first, then SRVs 
	dv->create_constant_buffer(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		ro_cbv_heap, ro_cbuf_res, &ro_cbuf_data, 0, ros.size());
	ro_cbuf_res->SetName(L"Render Object CBuf Data");

	{
		D3D12_RESOURCE_DESC rsd = {};
		rsd.Width = rsd.Height = 1;
		D3D12_SUBRESOURCE_DATA txd = {};
		txd.pData = color_white;

		rsd.MipLevels = 1;
		rsd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		rsd.Flags = D3D12_RESOURCE_FLAG_NONE;
		rsd.DepthOrArraySize = 1;
		rsd.SampleDesc.Count = 1;
		rsd.SampleDesc.Quality = 1;
		rsd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		chk(dv->device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rsd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&blank_texture)));
		const uint32_t subres_cnt = 1;
		const uint64_t uplbuf_siz = GetRequiredIntermediateSize(blank_texture.Get(), 0, subres_cnt);

		auto txupl = dv->new_upload_resource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uplbuf_siz),
			D3D12_RESOURCE_STATE_GENERIC_READ);

		txd.RowPitch = rsd.Width * 4;
		txd.SlicePitch = rsd.Width * rsd.Height * 4;

		UpdateSubresources(dv->commandList.Get(), blank_texture.Get(), txupl.Get(), 0, 0, 1, &txd);
		dv->commandList->ResourceBarrier(1,
			&CD3DX12_RESOURCE_BARRIER::Transition(blank_texture.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	for (int i = 0, ti = 0; i < ros.size(); ++i) {
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
		if (ros[i]->texture != nullptr) {
			ros[i]->srv_idx = ros.size() + ti + 1; ti++;
			dv->device->CreateShaderResourceView(
				ros[i]->texture.Get(), nullptr, 
				ro_cbv_heap.cpu_handle(ros[i]->srv_idx));
		}
		else ros[i]->srv_idx = ros.size();
	}

	dv->device->CreateShaderResourceView(blank_texture.Get(), nullptr, ro_cbv_heap.cpu_handle(ros.size()));

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
		root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0)
	}, {
		CD3DX12_STATIC_SAMPLER_DESC(0)
	}, pdesc, L"Basic");
	
	{
		auto d = dv->renderTargets[0]->GetDesc();
		d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		chk(dv->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE, &d, 
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
			&CD3DX12_CLEAR_VALUE(d.Format, color_black),
			IID_PPV_ARGS(&interm_buffer)));
		dv->device->CreateRenderTargetView(interm_buffer.Get(), nullptr, rtv_heap.cpu_handle(4));
		dv->device->CreateShaderResourceView(interm_buffer.Get(), nullptr, rtsrv_heap.cpu_handle(4));
	}

	create_geometry_buffer_and_pass();
	create_directional_light_pass();
	create_postprocess_pass();

	dv->free_shaders();
}


const D3D12_INPUT_ELEMENT_DESC basic_input_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

void renderer::create_geometry_buffer_and_pass() {
	auto desc = dv->renderTargets[0]->GetDesc();
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	auto rtv_desc = D3D12_RENDER_TARGET_VIEW_DESC{};
	rtv_desc.Format = desc.Format;
	rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	for (uint32_t i = 0; i < (uint32_t)gbuffer_id::total_count; ++i) {
		chk(dv->device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			&CD3DX12_CLEAR_VALUE(desc.Format, color_black), 
			IID_PPV_ARGS(&(geometry_buffers[i]))));
		dv->device->CreateShaderResourceView(geometry_buffers[i].Get(), nullptr,
			rtsrv_heap.cpu_handle(i));
		dv->device->CreateRenderTargetView(geometry_buffers[i].Get(), &rtv_desc,
			rtv_heap.cpu_handle(i));
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pdesc = {};
	pdesc.InputLayout = { basic_input_layout, _countof(basic_input_layout) };
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

const D3D12_INPUT_ELEMENT_DESC posonly_input_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
void renderer::create_directional_light_pass() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pdesc = {};

	pdesc.InputLayout = { posonly_input_layout, _countof(posonly_input_layout) };
	pdesc.VS = dv->load_shader(window->GetAssetFullPath(L"fsq.vs.cso"));
	pdesc.PS = dv->load_shader(window->GetAssetFullPath(L"light-directional.ps.cso"));
	pdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pdesc.RasterizerState.FrontCounterClockwise = true;
	pdesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.DepthStencilState.DepthEnable = false;
	pdesc.BlendState.RenderTarget[0].BlendEnable = true;
	pdesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	pdesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	pdesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	pdesc.NumRenderTargets = 1;
	pdesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	pdesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	pdesc.SampleDesc.Count = 1;

	dir_light_pass = pass(dv, {
		root_parameterh::constants(2, 0),
		root_parameterh::constants(24, 1),
		root_parameterh::descriptor_table({ //geometry buffer
			root_parameterh::descriptor_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0),
			root_parameterh::descriptor_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 1),
			root_parameterh::descriptor_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 2),
			root_parameterh::descriptor_range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 3),
		}),
		root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, 0), //shadow map
	}, {
		CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT),
		CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER)
	}, pdesc, L"Directional Light Render");

	D3D12_DEPTH_STENCIL_VIEW_DESC dsd = {};
	dsd.Format = DXGI_FORMAT_D32_FLOAT;
	dsd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	const uint32_t shadow_map_res = 1024;
	chk(dv->device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, shadow_map_res, shadow_map_res, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&CD3DX12_CLEAR_VALUE(dsd.Format, 1.f, 0), IID_PPV_ARGS(&shadow_dir_light_buffer)));
	dv->device->CreateDepthStencilView(shadow_dir_light_buffer.Get(), &dsd,
		shadow_dir_light_dsv_heap.cpu_handle(0));

	D3D12_SHADER_RESOURCE_VIEW_DESC srd = {};
	srd.Format = DXGI_FORMAT_R32_FLOAT;
	srd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srd.Texture2D.MipLevels = 1;
	srd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	dv->device->CreateShaderResourceView(shadow_dir_light_buffer.Get(), &srd,
		rtsrv_heap.cpu_handle(5));

	shadow_dir_light_vp = dv->viewport;
	shadow_dir_light_vp.Height  = shadow_dir_light_vp.Width = shadow_map_res;
	
	pdesc.InputLayout = { basic_input_layout, _countof(basic_input_layout) };
	pdesc.VS = dv->load_shader(window->GetAssetFullPath(L"basic.vs.cso"));
	pdesc.PS = {};
	pdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pdesc.RasterizerState.FrontCounterClockwise = true;
	pdesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.NumRenderTargets = 0;
	pdesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	pdesc.DSVFormat = dsd.Format;
	pdesc.SampleDesc.Count = 1;
	shadow_dir_light_pass = pass(dv, {
		root_parameterh::constants(16, 0),
		root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1),
	}, {}, pdesc, L"Shadow for Directional Lights");
}

void renderer::create_postprocess_pass() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pdesc = {};

	pdesc.InputLayout = { posonly_input_layout, _countof(posonly_input_layout) };
	pdesc.VS = dv->load_shader(window->GetAssetFullPath(L"fsq.vs.cso"));
	pdesc.PS = dv->load_shader(window->GetAssetFullPath(L"postprocess.ps.cso"));
	pdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pdesc.RasterizerState.FrontCounterClockwise = true;
	pdesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pdesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pdesc.SampleMask = UINT_MAX;
	pdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pdesc.DepthStencilState.DepthEnable = false;
	pdesc.NumRenderTargets = 1;
	pdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pdesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	pdesc.SampleDesc.Count = 1;

	postprocess_pass = pass(dv, {
		root_parameterh::constants(2, 0),
		root_parameterh::descriptor_table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0)
	}, {
		CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT)
	}, pdesc, L"Postprocess Render");
}
#pragma endregion

#pragma region Rendering
void renderer::draw_geometry(ComPtr<ID3D12GraphicsCommandList> cmdlist, bool shading, 
		XMFLOAT4X4* vp) {
	cmdlist->SetDescriptorHeaps(1, &ro_cbv_heap.heap);
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdlist->SetGraphicsRoot32BitConstants(0, 16, vp==nullptr ? cam.viewproj.m : vp->m, 0);
	for (int i = 0; i < ros.size(); ++i) {
		cmdlist->SetGraphicsRootDescriptorTable(1,
			ro_cbv_heap.gpu_handle(i));
		if(shading) cmdlist->SetGraphicsRootDescriptorTable(2,
			ro_cbv_heap.gpu_handle(ros[i]->srv_idx));
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

void renderer::render_directional_light_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist) {

	XMMATRIX ligP, ligV;
	XMVECTOR vL;
	XMFLOAT4X4 ligT;
	XMFLOAT4 v[2];
	XMFLOAT2 res(window->width, window->height);
	
	for (const auto& l : directional_lights) {
		if (l.casts_shadow) {
			dv->resource_barrier(cmdlist, {
				CD3DX12_RESOURCE_BARRIER::Transition(shadow_dir_light_buffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE)
			});
			shadow_dir_light_pass.apply(cmdlist);
			cmdlist->RSSetViewports(1, &shadow_dir_light_vp);
			cmdlist->OMSetRenderTargets(0, nullptr, false, &shadow_dir_light_dsv_heap.cpu_handle(0));
			cmdlist->ClearDepthStencilView(shadow_dir_light_dsv_heap.cpu_handle(0), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

			vL = XMLoadFloat4(&l.direction);
			ligP = XMMatrixOrthographicRH(l.scene_radius, l.scene_radius, 1.f, l.scene_radius);
			ligV = XMMatrixLookToRH(vL*(l.scene_radius-0.1f), -vL, XMVectorSet(1, 0, 0, 0));
			XMStoreFloat4x4(&ligT, ligV * ligP);

			draw_geometry(cmdlist, false, &ligT);
			dv->resource_barrier(cmdlist, {
				CD3DX12_RESOURCE_BARRIER::Transition(shadow_dir_light_buffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			});
			dv->set_default_viewport(cmdlist);
		}

		auto rth = rtv_heap.cpu_handle(4);
		cmdlist->OMSetRenderTargets(1, &rth, false, nullptr);

		dir_light_pass.apply(cmdlist);
		cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmdlist->SetDescriptorHeaps(1, &rtsrv_heap.heap);

		cmdlist->SetGraphicsRoot32BitConstants(0, 2, &res, 0);

		cmdlist->SetGraphicsRootDescriptorTable(2, rtsrv_heap.gpu_handle(0));
		cmdlist->SetGraphicsRootDescriptorTable(3, rtsrv_heap.gpu_handle(5));

		v[0] = l.direction; v[1] = l.color;
		cmdlist->SetGraphicsRoot32BitConstants(1, 8, v, 0);
		cmdlist->SetGraphicsRoot32BitConstants(1, 16, ligT.m, 8);
		fsq->draw(cmdlist);
	}
}

void renderer::render_postprocess_pass(ComPtr<ID3D12GraphicsCommandList> cmdlist) {
	postprocess_pass.apply(cmdlist);
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdlist->SetDescriptorHeaps(1, &rtsrv_heap.heap);

	XMFLOAT2 res(window->width, window->height);
	cmdlist->SetGraphicsRoot32BitConstants(0, 2, &res, 0);
	cmdlist->SetGraphicsRootDescriptorTable(1, rtsrv_heap.gpu_handle(4));
	fsq->draw(cmdlist);
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
		CD3DX12_RESOURCE_BARRIER::Transition(interm_buffer.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
	});


	
	//auto rth = dv->rtvHeap->cpu_handle(dv->frameIndex);
	//auto dsh = dv->dsvHeap->cpu_handle(0);
	auto rth = rtv_heap.cpu_handle(4);
	cmdlist->OMSetRenderTargets(1, &rth, false, nullptr);

	cmdlist->ClearRenderTargetView(rth, color_black, 0, nullptr);
	//cmdlist->ClearDepthStencilView(dsh, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0., 0, nullptr);
	
	render_directional_light_pass(cmdlist);

	dv->resource_barrier(cmdlist, {
		CD3DX12_RESOURCE_BARRIER::Transition(interm_buffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(dv->renderTargets[dv->frameIndex].Get(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	});
	

	rth = dv->rtvHeap->cpu_handle(dv->frameIndex);
	cmdlist->OMSetRenderTargets(1, &rth, false, nullptr);

	cmdlist->ClearRenderTargetView(rth, color_black, 0, nullptr);
	render_postprocess_pass(cmdlist);

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
