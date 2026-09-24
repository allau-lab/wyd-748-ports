#include "msh_loader.h"
#include "asset_resolve.h"

#include <cstdio>
#include <cstring>
#include <fstream>
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

	uint32_t RdU32(const uint8_t*& p, const uint8_t* end)
	{
		if (p + 4 > end)
			return 0;
		uint32_t v = 0;
		std::memcpy(&v, p, 4);
		p += 4;
		return v;
	}
}

bool WYD_LoadMshMesh(const char* relativeOrAbs, WYDMshMesh& out)
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
	if (!ReadAll(path, data) || data.size() < 32)
	{
		out.error = std::string("falha ao ler ") + path;
		return false;
	}

	const uint8_t* p = data.data();
	const uint8_t* end = p + data.size();
	out.parent_id = RdU32(p, end);
	out.bone_id = RdU32(p, end);
	out.fvf = RdU32(p, end);
	out.stride = RdU32(p, end);
	out.influences = RdU32(p, end);
	out.palette = RdU32(p, end);
	out.vertex_count = RdU32(p, end);
	out.index_count = RdU32(p, end);
	out.path = path;

	if (out.stride == 0 || out.vertex_count == 0 || out.index_count == 0)
	{
		out.error = "header msh inválido";
		return false;
	}
	if (out.influences < 1 || out.influences > 4)
	{
		out.error = "influences fora de 1..4";
		return false;
	}
	static const uint32_t kStride[] = {0, 36, 40, 44, 48};
	if (out.stride != kStride[out.influences])
	{
		out.error = "stride incompatível com influences";
		return false;
	}
	if (out.palette == 0 || out.palette > 40)
	{
		out.error = "palette inválida";
		return false;
	}

	const size_t bindBytes = static_cast<size_t>(out.palette) * 64u;
	const size_t nameBytes = static_cast<size_t>(out.palette) * 4u;
	const size_t vbBytes = static_cast<size_t>(out.vertex_count) * out.stride;
	const size_t ibBytes = static_cast<size_t>(out.index_count) * 2u;
	if (static_cast<size_t>(p - data.data()) + bindBytes + nameBytes + vbBytes + ibBytes > data.size())
	{
		out.error = "msh truncado";
		return false;
	}

	out.bind_pose.resize(static_cast<size_t>(out.palette) * 16u);
	std::memcpy(out.bind_pose.data(), p, bindBytes);
	p += bindBytes;

	out.bone_names.resize(out.palette);
	std::memcpy(out.bone_names.data(), p, nameBytes);
	p += nameBytes;

	out.vb.assign(p, p + vbBytes);
	p += vbBytes;

	out.indices.resize(out.index_count);
	std::memcpy(out.indices.data(), p, ibBytes);

	out.positions.resize(static_cast<size_t>(out.vertex_count) * 3u);
	for (uint32_t i = 0; i < out.vertex_count; ++i)
	{
		float x, y, z;
		std::memcpy(&x, out.vb.data() + i * out.stride + 0, 4);
		std::memcpy(&y, out.vb.data() + i * out.stride + 4, 4);
		std::memcpy(&z, out.vb.data() + i * out.stride + 8, 4);
		out.positions[i * 3u + 0] = x;
		out.positions[i * 3u + 1] = y;
		out.positions[i * 3u + 2] = z;
		if (i == 0)
		{
			out.min_x = out.max_x = x;
			out.min_y = out.max_y = y;
			out.min_z = out.max_z = z;
		}
		else
		{
			if (x < out.min_x) out.min_x = x;
			if (x > out.max_x) out.max_x = x;
			if (y < out.min_y) out.min_y = y;
			if (y > out.max_y) out.max_y = y;
			if (z < out.min_z) out.min_z = z;
			if (z > out.max_z) out.max_z = z;
		}
	}

	std::fprintf(stderr,
		"[WYDLINUX][msh] %s fvf=0x%X stride=%u infl=%u pal=%u verts=%u idx=%u\n",
		path.c_str(), out.fvf, out.stride, out.influences, out.palette,
		out.vertex_count, out.index_count);
	return true;
}
