#pragma once

#include <cstdint>
#include <SDL.h>

struct WYD_SwitchPadState
{
	float lx = 0.f;
	float ly = 0.f;
	float rx = 0.f;
	float ry = 0.f;
	uint32_t buttons = 0;
	int touch_x = -1;
	int touch_y = -1;
	bool touch_down = false;
};

enum : uint32_t
{
	WYD_SWITCH_BTN_A = 1u << 0,
	WYD_SWITCH_BTN_B = 1u << 1,
	WYD_SWITCH_BTN_X = 1u << 2,
	WYD_SWITCH_BTN_Y = 1u << 3,
	WYD_SWITCH_BTN_L = 1u << 4,
	WYD_SWITCH_BTN_R = 1u << 5,
	WYD_SWITCH_BTN_ZL = 1u << 6,
	WYD_SWITCH_BTN_ZR = 1u << 7,
	WYD_SWITCH_BTN_PLUS = 1u << 8,
	WYD_SWITCH_BTN_MINUS = 1u << 9,
	WYD_SWITCH_BTN_LSTICK = 1u << 10,
	WYD_SWITCH_BTN_RSTICK = 1u << 11,
	WYD_SWITCH_BTN_DUP = 1u << 12,
	WYD_SWITCH_BTN_DDOWN = 1u << 13,
	WYD_SWITCH_BTN_DLEFT = 1u << 14,
	WYD_SWITCH_BTN_DRIGHT = 1u << 15,
};

struct WYD_SwitchInput
{
	SDL_GameController* pad = nullptr;
	WYD_SwitchPadState state{};
	bool quit_requested = false;
};

bool WYD_SwitchInputInit(WYD_SwitchInput& in);
void WYD_SwitchInputShutdown(WYD_SwitchInput& in);
bool WYD_SwitchInputPoll(WYD_SwitchInput& in, int window_w, int window_h);
