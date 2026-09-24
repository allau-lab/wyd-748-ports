#pragma once

// Skeleton .bon + clip .ani + avaliação de palette (bind * combined) — Fase O.

#include <cstdint>
#include <string>
#include <vector>

struct WYDBonHierarchy
{
	uint32_t bone_count = 0;
	std::vector<uint32_t> parent; // 0xFFFFFFFF = root orphan → acopla em root virtual
	std::vector<uint32_t> id;
	std::string path;
	std::string error;
};

struct WYDAniClip
{
	uint32_t ticks = 0;
	uint32_t bones = 0;
	std::vector<float> mats; // ticks * bones * 16
	std::string path;
	std::string error;
};

bool WYD_LoadBonHierarchy(const char* relativeOrAbs, WYDBonHierarchy& out);
bool WYD_LoadAniClip(const char* relativeOrAbs, WYDAniClip& out);

// outMats: palette * 16 floats (column-major D3D) = bindPose[i] * combined[boneNames[i]]
bool WYD_EvalBonePalette(const WYDBonHierarchy& bon, const WYDAniClip& ani,
	uint32_t tick,
	const float* bindPose, const uint32_t* boneNames, uint32_t palette,
	float* outMats);
