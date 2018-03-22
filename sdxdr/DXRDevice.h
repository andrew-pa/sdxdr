#pragma once
#include "dxut\dxut.h"

class DXRDevice {
	DXDevice* dev;

	bool dxr_enabled;

	ComPtr<ID3D12RaytracingFallbackDevice> rtr_dev;
	ComPtr<ID3D12RaytracingFallbackStateObject> rtr_state;

	ComPtr<ID3D12DeviceRaytracingPrototype> dxr_dev;
	ComPtr<ID3D12StateObjectPrototype> dxr_state;
public:
	DXRDevice(DXDevice* d) : dev(d) {
		// enable DXR
		UUID experimentalFeaturesSMandDXR[] = { D3D12ExperimentalShaderModels, D3D12RaytracingPrototype };
		UUID experimentalFeaturesSM[] = { D3D12ExperimentalShaderModels };

		ComPtr<ID3D12Device> testDevice;
		if (FAILED(D3D12EnableExperimentalFeatures(ARRAYSIZE(experimentalFeaturesSMandDXR), experimentalFeaturesSMandDXR, nullptr, nullptr))
			|| FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice))))
		{
			chk(SUCCEEDED(D3D12EnableExperimentalFeatures(ARRAYSIZE(experimentalFeaturesSM), experimentalFeaturesSM, nullptr, nullptr))
				&& SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice))));
		}
	}

	void init_dxr(bool enable_dxr) {
		dxr_enabled = enable_dxr;

		if (dxr_enabled) {
			chk(dev->device->QueryInterface(__uuidof(ID3D12DeviceRaytracingPrototype), &dxr_dev));
		}
		else {
			CreateRaytracingFallbackDeviceFlags cdf = CreateRaytracingFallbackDeviceFlags::None;
			chk(D3D12CreateRaytracingFallbackDevice(dev->device.Get(), CreateRaytracingFallbackDeviceFlags::None, 0, IID_PPV_ARGS(&rtr_dev)));
		}
	}

	template<typename F>
	void transmute_cmdlist(ComPtr<ID3D12GraphicsCommandList> cmdlist, F f) {
		if (dxr_enabled) {
			ComPtr<ID3D12CommandListRaytracingPrototype> cl;
			chk(cmdlist->QueryInterface(__uuidof(ID3D12CommandListRaytracingPrototype), &cl));
			f(cl);
		} else {
			ComPtr<ID3D12RaytracingFallbackCommandList> cl;
			rtr_dev->QueryRaytracingCommandList(cmdlist.Get(), IID_PPV_ARGS(&cl));
			f(cl);
		}
	}

	ComPtr<ID3D12RootSignature> create_root_signature(vector<CD3DX12_ROOT_PARAMETER> paras,
		vector<CD3DX12_STATIC_SAMPLER_DESC> static_samps,
		bool free_paras_ptrs = false, const wchar_t* name = nullptr,
		D3D12_ROOT_SIGNATURE_FLAGS rsf = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)
	{
		D3D12_ROOT_SIGNATURE_DESC rsd;
		rsd.pParameters = paras.data();
		rsd.NumParameters = paras.size();
		rsd.pStaticSamplers = (static_samps.size() > 0 ? static_samps.data() : nullptr);
		rsd.NumStaticSamplers = static_samps.size();
		rsd.Flags = rsf;
		ComPtr<ID3DBlob> sig, err;
		ComPtr<ID3D12RootSignature> rs;

		if (dxr_enabled) {
			chk(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
			chk(dev->device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rs)));
		}
		else {
			chk(rtr_dev->D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
			chk(rtr_dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rs)));
		}

		for (auto& p : paras) {
			if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE && p.DescriptorTable.pDescriptorRanges) delete p.DescriptorTable.pDescriptorRanges;
		}

		if (name) rs->SetName(name);

		return rs;
	}

	void build_acceleration_structure(vector<tuple<D3D12_RAYTRACING_GEOMETRY_DESC, XMFLOAT4X4>> geometry) {
		D3D12_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_DESC prebuild_info_req;
		prebuild_info_req.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuild_info_req.NumDescs = geometry.size();

		prebuild_info_req.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		prebuild_info_req.pGeometryDescs = nullptr;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO toplvl_prebuild_info;
		if (dxr_enabled)
			dxr_dev->GetRaytracingAccelerationStructurePrebuildInfo(&prebuild_info_req, &toplvl_prebuild_info);
		else
			rtr_dev->GetRaytracingAccelerationStructurePrebuildInfo(&prebuild_info_req, &toplvl_prebuild_info);
		assert(toplvl_prebuild_info.ResultDataMaxSizeInBytes > 0);

		prebuild_info_req.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		prebuild_info_req.pGeometryDescs = geometry.data();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO botlvl_prebuild_info;
		if (dxr_enabled)
			dxr_dev->GetRaytracingAccelerationStructurePrebuildInfo(&prebuild_info_req, &botlvl_prebuild_info);
		else
			rtr_dev->GetRaytracingAccelerationStructurePrebuildInfo(&prebuild_info_req, &botlvl_prebuild_info);
		assert(botlvl_prebuild_info.ResultDataMaxSizeInBytes > 0);

		auto heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto res = dev->new_upload_resource(&heap_prop,
			D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(
				max(toplvl_prebuild_info.ScratchDataSizeInBytes, botlvl_prebuild_info.ScratchDataSizeInBytes),
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), 
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		ComPtr<ID3D12Resource> toplvl_accls, botlvl_accls;
		auto init_accls_state = dxr_enabled ? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE : rtr_dev->GetAccelerationStructureResourceState();
		dev->device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(botlvl_prebuild_info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			init_accls_state, nullptr, IID_PPV_ARGS(&botlvl_accls));
		dev->device->CreateCommittedResource(&heap_prop, D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(toplvl_prebuild_info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			init_accls_state, nullptr, IID_PPV_ARGS(&toplvl_accls));

		ComPtr<ID3D12Resource> instance_desc;
		if()
	}
};