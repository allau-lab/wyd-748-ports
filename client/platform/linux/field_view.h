#pragma once

// Vista top-down do campo com mobs 0x364 — Fase F+.
#include "field_map.h"
#include "field_entities.h"
#include "login_session.h"
#include "wyt_decode.h"

#include <cstdint>

struct WYDFieldView
{
	WYDWytImage image {};
	float local_x = 32.f;
	float local_y = 32.f;
	bool dirty = true;
};

void WYD_FieldViewResetFromLogin(WYDFieldView& view, const WYDLoginSession& login);

void WYD_FieldViewMove(WYDFieldView& view, float dx, float dy);

bool WYD_FieldViewBuild(WYDFieldView& view, const WYDFieldMap& map,
	const WYDLoginSession& login, const WYDFieldWorld& world,
	uint32_t width, uint32_t height);

void WYD_FieldViewDestroy(WYDFieldView& view);
