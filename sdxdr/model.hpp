#pragma once
#include "dxut\dxut.h"
#include "meshloader.h"
#include "renderer.h"


inline XMFLOAT4 as_xmf4(const aiColor3D& c, float w = 1.f) {
	return XMFLOAT4(c.r, c.g, c.b, w);
}

vector<material> load_materials(const aiScene* scn) {
	vector<material> mats;
	for (int k = 0; k < scn->mNumMaterials; ++k) {
		auto am = scn->mMaterials[k];

		aiColor3D diffuse_c, spec_c;
		am->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_c);
		am->Get(AI_MATKEY_COLOR_SPECULAR, spec_c);
		float shn;
		am->Get(AI_MATKEY_SHININESS_STRENGTH, shn);

		mats.push_back(material(as_xmf4(diffuse_c), as_xmf4(spec_c, shn*6.f)));
	}
	return mats;
}

struct model {
	shared_ptr<renderer> dfr;
	int start_rosid, ros_count;
	XMFLOAT4X4 world;

	model(int sr = 0, int rc = 0, XMFLOAT4X4 wld = {}) : start_rosid(sr), ros_count(rc), world(wld) {}

	model(DXDevice* dv, ComPtr<ID3D12GraphicsCommandList> uplcmdlst,
		vector<shared_ptr<render_object>>& ros, const aiScene* scn, const string& name, const vector<material>& matcsh, XMFLOAT4X4 wld)
		: world(wld), start_rosid(ros.size()), ros_count(0)
	{
		auto nm = aiString(name);
		vector<vertex> v; vector<uint32_t> i;
		for (int k = 0; k < scn->mNumMeshes; ++k) {
			if (name.empty() || scn->mMeshes[k]->mName == nm) {
				load_aimesh(scn->mMeshes[k], v, i);
				auto ro = make_shared<render_object>(
					make_shared<mesh>(dv, uplcmdlst, v, i),
					matcsh[scn->mMeshes[k]->mMaterialIndex]);
				aiString texture_path;
				if (scn->mMaterials[scn->mMeshes[k]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE, 0, &texture_path) == AI_SUCCESS) {
					dv->load_texture(uplcmdlst, texture_path.C_Str(), ro->texture);
				}
				ros.push_back(ro);
				ros_count++;
			}
		}
	}

	model make_instance(vector<shared_ptr<render_object>>& ros, XMFLOAT4X4 wld) {
		if (dfr) throw;
		int strs = ros.size();
		int rosc = 0;
		for (int i = 0; i < ros_count; ++i) {
			ros.push_back(make_shared<render_object>(
				ros[i + start_rosid]->msh,
				*ros[i + start_rosid]->mat,
				*ros[i + start_rosid]->world,
				ros[i + start_rosid]->texture
				));
			rosc++;
		}
		return model(strs, rosc, wld);
	}

	void init(shared_ptr<renderer> dfr_) { dfr = dfr_; }

	void update() {
		for (int i = 0; i < ros_count; ++i) {
			*dfr->ros[i + start_rosid]->world = world;
		}
	}
};
