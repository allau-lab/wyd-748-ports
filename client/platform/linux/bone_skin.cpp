#include "bone_skin.h"
#include "asset_resolve.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace
{
	bool ReadAll(const std::string& path, std::vector<uint8_t>& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		f.seekg(0, std::ios::end);
		const auto n = f.tellg();
		if (n <= 0)
			return false;
		f.seekg(0, std::ios::beg);
		out.resize(static_cast<size_t>(n));
		f.read(reinterpret_cast<char*>(out.data()), n);
		return static_cast<bool>(f) || f.eof();
	}

	void MatIdentity(float m[16])
	{
		std::memset(m, 0, 16 * sizeof(float));
		m[0] = m[5] = m[10] = m[15] = 1.f;
	}

	void MatMul(float o[16], const float a[16], const float b[16])
	{
		// Row-major (D3DXMATRIX): o = a * b
		float t[16];
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
			{
				t[r * 4 + c] =
					a[r * 4 + 0] * b[0 * 4 + c] +
					a[r * 4 + 1] * b[1 * 4 + c] +
					a[r * 4 + 2] * b[2 * 4 + c] +
					a[r * 4 + 3] * b[3 * 4 + c];
			}
		std::memcpy(o, t, sizeof(t));
	}

	struct Node
	{
		uint32_t id = 0;
		int parent = -1; // index in nodes; -1 = virtual root
		float local[16] {};
		float combined[16] {};
		std::vector<int> children;
	};

	void UpdateNode(std::vector<Node>& nodes, int idx, const float* parentCombined)
	{
		Node& n = nodes[static_cast<size_t>(idx)];
		MatMul(n.combined, n.local, parentCombined);
		for (int c : n.children)
			UpdateNode(nodes, c, n.combined);
	}
}

bool WYD_LoadBonHierarchy(const char* relativeOrAbs, WYDBonHierarchy& out)
{
	out = {};
	if (!relativeOrAbs || !relativeOrAbs[0])
	{
		out.error = "path vazio";
		return false;
	}
	std::string path = WYD_ResolveAssetAbsolute(relativeOrAbs);
	if (path.empty())
		path = relativeOrAbs;
	std::vector<uint8_t> data;
	if (!ReadAll(path, data) || (data.size() % 8) != 0 || data.size() < 8)
	{
		out.error = std::string("falha ao ler ") + path;
		return false;
	}
	out.bone_count = static_cast<uint32_t>(data.size() / 8);
	out.parent.resize(out.bone_count);
	out.id.resize(out.bone_count);
	for (uint32_t i = 0; i < out.bone_count; ++i)
	{
		std::memcpy(&out.parent[i], data.data() + i * 8u, 4);
		std::memcpy(&out.id[i], data.data() + i * 8u + 4, 4);
	}
	out.path = path;
	std::fprintf(stderr, "[WYDLINUX][bon] %s bones=%u\n", path.c_str(), out.bone_count);
	return true;
}

bool WYD_LoadAniClip(const char* relativeOrAbs, WYDAniClip& out)
{
	out = {};
	if (!relativeOrAbs || !relativeOrAbs[0])
	{
		out.error = "path vazio";
		return false;
	}
	std::string path = WYD_ResolveAssetAbsolute(relativeOrAbs);
	if (path.empty())
		path = relativeOrAbs;
	std::vector<uint8_t> data;
	if (!ReadAll(path, data) || data.size() < 8)
	{
		out.error = std::string("falha ao ler ") + path;
		return false;
	}
	std::memcpy(&out.ticks, data.data(), 4);
	std::memcpy(&out.bones, data.data() + 4, 4);
	const size_t need = 8u + static_cast<size_t>(out.ticks) * out.bones * 64u;
	if (out.ticks == 0 || out.bones == 0 || data.size() < need)
	{
		out.error = "ani truncado/inválido";
		return false;
	}
	out.mats.resize(static_cast<size_t>(out.ticks) * out.bones * 16u);
	std::memcpy(out.mats.data(), data.data() + 8, need - 8);
	out.path = path;
	std::fprintf(stderr, "[WYDLINUX][ani] %s ticks=%u bones=%u\n",
		path.c_str(), out.ticks, out.bones);
	return true;
}

bool WYD_EvalBonePalette(const WYDBonHierarchy& bon, const WYDAniClip& ani,
	uint32_t tick,
	const float* bindPose, const uint32_t* boneNames, uint32_t palette,
	float* outMats)
{
	if (!bon.bone_count || !ani.ticks || !ani.bones || !bindPose || !boneNames || !outMats)
		return false;
	if (palette == 0 || palette > 40)
		return false;
	if (ani.bones != bon.bone_count)
		return false;

	tick %= ani.ticks;

	// Espelha TMSkinMesh::InitObject: root virtual + nós por ID do .bon.
	std::vector<Node> nodes;
	nodes.reserve(bon.bone_count + 1);
	Node root {};
	root.id = 0xFFFFFFFFu;
	MatIdentity(root.local);
	nodes.push_back(root);

	std::unordered_map<uint32_t, int> idToIdx;
	idToIdx.reserve(bon.bone_count * 2u);

	for (uint32_t i = 0; i < bon.bone_count; ++i)
	{
		Node n {};
		n.id = bon.id[i];
		MatIdentity(n.local);
		const int idx = static_cast<int>(nodes.size());
		nodes.push_back(n);
		idToIdx[bon.id[i]] = idx;
	}

	for (uint32_t i = 0; i < bon.bone_count; ++i)
	{
		const int idx = idToIdx[bon.id[i]];
		uint32_t parentId = bon.parent[i];
		int parentIdx = 0; // virtual root
		if (parentId != 0xFFFFFFFFu)
		{
			const auto it = idToIdx.find(parentId);
			if (it != idToIdx.end())
				parentIdx = it->second;
		}
		nodes[static_cast<size_t>(idx)].parent = parentIdx;
		nodes[static_cast<size_t>(parentIdx)].children.push_back(idx);
	}

	// Locais do clip: matAnimation[tick * bones + boneId]
	for (uint32_t i = 0; i < bon.bone_count; ++i)
	{
		const uint32_t boneId = bon.id[i];
		if (boneId >= ani.bones)
			continue;
		const auto it = idToIdx.find(boneId);
		if (it == idToIdx.end())
			continue;
		const size_t off = (static_cast<size_t>(tick) * ani.bones + boneId) * 16u;
		std::memcpy(nodes[static_cast<size_t>(it->second)].local,
			ani.mats.data() + off, 16 * sizeof(float));
	}

	float identity[16];
	MatIdentity(identity);
	UpdateNode(nodes, 0, identity);

	for (uint32_t pi = 0; pi < palette; ++pi)
	{
		const uint32_t skelId = boneNames[pi];
		float combined[16];
		MatIdentity(combined);
		const auto it = idToIdx.find(skelId);
		if (it != idToIdx.end())
			std::memcpy(combined, nodes[static_cast<size_t>(it->second)].combined, sizeof(combined));

		const float* bind = bindPose + static_cast<size_t>(pi) * 16u;
		float skin[16];
		MatMul(skin, bind, combined); // bindPose * combined
		std::memcpy(outMats + static_cast<size_t>(pi) * 16u, skin, sizeof(skin));
	}
	return true;
}
