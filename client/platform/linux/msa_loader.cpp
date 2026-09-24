#include "msa_loader.h"
#include "asset_resolve.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace
{
	bool ReadExact(std::ifstream& in, void* dst, size_t n)
	{
		in.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
		return static_cast<size_t>(in.gcount()) == n;
	}
}

bool WYD_LoadMsaMesh(const char* relativeOrListPath, WYDMsaMesh& out)
{
	out = {};
	const std::string abs = WYD_ResolveAssetAbsolute(relativeOrListPath);
	if (abs.empty())
	{
		out.error = std::string("MSA não encontrado (case-sensitive): ") +
			(relativeOrListPath ? relativeOrListPath : "(null)");
		return false;
	}

	std::ifstream in(abs, std::ios::binary);
	if (!in)
	{
		out.error = "falha ao abrir " + abs;
		return false;
	}

	if (!ReadExact(in, &out.fvf, 4) ||
		!ReadExact(in, &out.vertex_stride, 4) ||
		!ReadExact(in, &out.attribute_count, 4))
	{
		out.error = "header MSA truncado";
		return false;
	}
	if (out.attribute_count == 0 || out.attribute_count > 32 ||
		out.vertex_stride < 12 || (out.vertex_stride % 4) != 0)
	{
		out.error = "header MSA inválido";
		return false;
	}

	// D3DXATTRIBUTERANGE = 5 * DWORD = 20 bytes
	std::vector<uint8_t> attrs(static_cast<size_t>(out.attribute_count) * 20u);
	if (!ReadExact(in, attrs.data(), attrs.size()))
	{
		out.error = "attribute ranges truncados";
		return false;
	}

	char tex[12] {};
	if (!ReadExact(in, tex, 11))
	{
		out.error = "nome de textura truncado";
		return false;
	}
	tex[11] = 0;
	out.texture_hint = tex;
	// demais atributos: ainda há (attCount-1)*11 bytes de nomes
	for (uint32_t i = 1; i < out.attribute_count; ++i)
	{
		char skip[11];
		if (!ReadExact(in, skip, 11))
		{
			out.error = "nomes de textura extras truncados";
			return false;
		}
	}

	uint32_t ibSize = 0;
	if (!ReadExact(in, &ibSize, 4))
	{
		out.error = "ib size truncado";
		return false;
	}
	if (ibSize > 0)
	{
		if ((ibSize % 2) != 0)
		{
			out.error = "ib size ímpar";
			return false;
		}
		out.indices.resize(ibSize / 2);
		if (!ReadExact(in, out.indices.data(), ibSize))
		{
			out.error = "índices truncados";
			return false;
		}
	}

	uint32_t vbSize = 0;
	if (!ReadExact(in, &vbSize, 4))
	{
		out.error = "vb size truncado";
		return false;
	}
	if (vbSize == 0 || (vbSize % out.vertex_stride) != 0)
	{
		out.error = "vb size incompatível com stride";
		return false;
	}

	out.vertex_count = vbSize / out.vertex_stride;
	std::vector<uint8_t> vb(vbSize);
	if (!ReadExact(in, vb.data(), vbSize))
	{
		out.error = "vértices truncados";
		return false;
	}

	out.positions.resize(static_cast<size_t>(out.vertex_count) * 3u);
	const bool hasUv = (out.fvf & 0x400u) != 0 || (out.fvf & 0x100u) != 0; // TEX1+
	// Layout comum 0x142 (XYZ|DIFFUSE|TEX1) stride 24: uv @ offset 16.
	const size_t uvOff = (out.vertex_stride >= 24 && (out.fvf & 0x040u) != 0)
		? 16u
		: ((out.vertex_stride >= 20) ? 12u : 0u);
	if (hasUv && uvOff + 8u <= out.vertex_stride)
		out.uvs.resize(static_cast<size_t>(out.vertex_count) * 2u);

	for (uint32_t i = 0; i < out.vertex_count; ++i)
	{
		const uint8_t* base = vb.data() + static_cast<size_t>(i) * out.vertex_stride;
		const float* v = reinterpret_cast<const float*>(base);
		out.positions[i * 3 + 0] = v[0];
		out.positions[i * 3 + 1] = v[1];
		out.positions[i * 3 + 2] = v[2];
		if (!out.uvs.empty())
		{
			const float* uv = reinterpret_cast<const float*>(base + uvOff);
			out.uvs[i * 2 + 0] = uv[0];
			out.uvs[i * 2 + 1] = uv[1];
		}
	}

	out.min_x = out.max_x = out.positions[0];
	out.min_y = out.max_y = out.positions[1];
	out.min_z = out.max_z = out.positions[2];
	for (uint32_t i = 1; i < out.vertex_count; ++i)
	{
		const float x = out.positions[i * 3 + 0];
		const float y = out.positions[i * 3 + 1];
		const float z = out.positions[i * 3 + 2];
		out.min_x = std::min(out.min_x, x); out.max_x = std::max(out.max_x, x);
		out.min_y = std::min(out.min_y, y); out.max_y = std::max(out.max_y, y);
		out.min_z = std::min(out.min_z, z); out.max_z = std::max(out.max_z, z);
	}

	std::fprintf(stderr,
		"[WYDLINUX][msa] %s fvf=0x%x stride=%u verts=%u idx=%zu tex='%s'\n",
		abs.c_str(), out.fvf, out.vertex_stride, out.vertex_count,
		out.indices.size(), out.texture_hint.c_str());
	return true;
}
