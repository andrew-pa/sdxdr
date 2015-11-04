#pragma once
#include "dxut\mesh.h"

#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

#undef min
#undef max

namespace detail {
	inline XMFLOAT3 ai_xm3(const aiVector3D& v) {
		return XMFLOAT3(v.x, v.y, v.z);
	}
	inline XMFLOAT2 ai_xm2(const aiVector3D& v) {
		return XMFLOAT2(v.x, v.y);
	}
}

void load_aimesh(const aiMesh* msh, vector<vertex>& v, vector<uint32_t>& i, XMFLOAT3* minv = nullptr, XMFLOAT3* maxv = nullptr, XMMATRIX* mat = nullptr) {
	if (!(msh->HasPositions() && msh->HasNormals() && msh->HasTextureCoords(0)))
		throw exception("mesh missing data");
	bool tng = msh->HasTangentsAndBitangents();
	v.resize(msh->mNumVertices);
	XMVECTOR x = XMVectorSet(numeric_limits<float>::min(), numeric_limits<float>::min(), numeric_limits<float>::min(), 0.f), 
				m = XMVectorSet(numeric_limits<float>::max(), numeric_limits<float>::max(), numeric_limits<float>::max(), 0.f);
	for (uint32_t i = 0; i < msh->mNumVertices; ++i) {
		v[i].position = detail::ai_xm3(msh->mVertices[i]);
		XMVECTOR p = XMLoadFloat3(&v[i].position);
		m = XMVectorMin(p, m);
		x = XMVectorMax(p, x);
		v[i].normal = detail::ai_xm3(msh->mNormals[i]);
		if (mat) {
			p = XMVector3Transform(p, *mat);
			XMStoreFloat3(&v[i].position, p);
			XMStoreFloat3(&v[i].normal,
				XMVector3TransformNormal(XMLoadFloat3(&v[i].normal), *mat));
		}
		v[i].texcoord = detail::ai_xm2(msh->mTextureCoords[0][i]);
		if(tng) v[i].tangent = detail::ai_xm3(msh->mTangents[i]);
	}
	i.clear();
	for (uint32_t fi = 0; fi < msh->mNumFaces; ++fi) {
		for (uint32_t ii = 0; ii < msh->mFaces[fi].mNumIndices; ++ii) {
			i.push_back(msh->mFaces[fi].mIndices[ii]);
		}
	}
	if (minv) XMStoreFloat3(minv, m);
	if (maxv) XMStoreFloat3(maxv, x);
}