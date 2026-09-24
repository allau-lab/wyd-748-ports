#include "input_switch.h"

#include <cmath>
#include <cstdio>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace
{
float NormAxis(Sint16 v)
{
	constexpr float dead = 0.18f;
	float n = static_cast<float>(v) / 32768.f;
	if (std::fabs(n) < dead)
		return 0.f;
	return n;
}

void MapSdlButton(WYD_SwitchPadState& st, SDL_GameControllerButton b, bool down)
{
	uint32_t bit = 0;
	switch (b)
	{
	case SDL_CONTROLLER_BUTTON_A: bit = WYD_SWITCH_BTN_A; break;
	case SDL_CONTROLLER_BUTTON_B: bit = WYD_SWITCH_BTN_B; break;
	case SDL_CONTROLLER_BUTTON_X: bit = WYD_SWITCH_BTN_X; break;
	case SDL_CONTROLLER_BUTTON_Y: bit = WYD_SWITCH_BTN_Y; break;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: bit = WYD_SWITCH_BTN_L; break;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: bit = WYD_SWITCH_BTN_R; break;
	case SDL_CONTROLLER_BUTTON_START: bit = WYD_SWITCH_BTN_PLUS; break;
	case SDL_CONTROLLER_BUTTON_BACK: bit = WYD_SWITCH_BTN_MINUS; break;
	case SDL_CONTROLLER_BUTTON_LEFTSTICK: bit = WYD_SWITCH_BTN_LSTICK; break;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK: bit = WYD_SWITCH_BTN_RSTICK; break;
	case SDL_CONTROLLER_BUTTON_DPAD_UP: bit = WYD_SWITCH_BTN_DUP; break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN: bit = WYD_SWITCH_BTN_DDOWN; break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT: bit = WYD_SWITCH_BTN_DLEFT; break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: bit = WYD_SWITCH_BTN_DRIGHT; break;
	default: break;
	}
	if (!bit)
		return;
	if (down)
		st.buttons |= bit;
	else
		st.buttons &= ~bit;
}
} // namespace

bool WYD_SwitchInputInit(WYD_SwitchInput& in)
{
	in = {};
#ifdef __SWITCH__
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	hidInitializeTouchScreen();
#endif
	for (int i = 0; i < SDL_NumJoysticks(); ++i)
	{
		if (SDL_IsGameController(i))
		{
			in.pad = SDL_GameControllerOpen(i);
			if (in.pad)
			{
				std::fprintf(stderr, "[WYD_SWITCH][input] pad=%s\n",
					SDL_GameControllerName(in.pad));
				break;
			}
		}
	}
	if (!in.pad)
		std::fprintf(stderr, "[WYD_SWITCH][input] sem GameController SDL (libnx pad ainda ok)\n");
	return true;
}

void WYD_SwitchInputShutdown(WYD_SwitchInput& in)
{
	if (in.pad)
	{
		SDL_GameControllerClose(in.pad);
		in.pad = nullptr;
	}
}

bool WYD_SwitchInputPoll(WYD_SwitchInput& in, int window_w, int window_h)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev))
	{
		switch (ev.type)
		{
		case SDL_QUIT:
			in.quit_requested = true;
			break;
		case SDL_CONTROLLERDEVICEADDED:
			if (!in.pad && SDL_IsGameController(ev.cdevice.which))
				in.pad = SDL_GameControllerOpen(ev.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			if (in.pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in.pad)) == ev.cdevice.which)
			{
				SDL_GameControllerClose(in.pad);
				in.pad = nullptr;
			}
			break;
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			MapSdlButton(in.state, static_cast<SDL_GameControllerButton>(ev.cbutton.button),
				ev.type == SDL_CONTROLLERBUTTONDOWN);
			break;
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
			in.state.touch_down = true;
			in.state.touch_x = static_cast<int>(ev.tfinger.x * static_cast<float>(window_w));
			in.state.touch_y = static_cast<int>(ev.tfinger.y * static_cast<float>(window_h));
			break;
		case SDL_FINGERUP:
			in.state.touch_down = false;
			in.state.touch_x = -1;
			in.state.touch_y = -1;
			break;
		default:
			break;
		}
	}

	if (in.pad)
	{
		in.state.lx = NormAxis(SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_LEFTX));
		in.state.ly = NormAxis(SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_LEFTY));
		in.state.rx = NormAxis(SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_RIGHTX));
		in.state.ry = NormAxis(SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_RIGHTY));
		if (SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)
			in.state.buttons |= WYD_SWITCH_BTN_ZL;
		else
			in.state.buttons &= ~WYD_SWITCH_BTN_ZL;
		if (SDL_GameControllerGetAxis(in.pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000)
			in.state.buttons |= WYD_SWITCH_BTN_ZR;
		else
			in.state.buttons &= ~WYD_SWITCH_BTN_ZR;
	}

#ifdef __SWITCH__
	// Fonte nativa (Joy-Con): reconstrói máscara a cada frame (sem sticky OR).
	static PadState pad{};
	static bool pad_ready = false;
	if (!pad_ready)
	{
		padInitializeDefault(&pad);
		pad_ready = true;
	}
	padUpdate(&pad);
	const u64 btn = padGetButtons(&pad);
	uint32_t native = 0;
	if (btn & HidNpadButton_A) native |= WYD_SWITCH_BTN_A;
	if (btn & HidNpadButton_B) native |= WYD_SWITCH_BTN_B;
	if (btn & HidNpadButton_X) native |= WYD_SWITCH_BTN_X;
	if (btn & HidNpadButton_Y) native |= WYD_SWITCH_BTN_Y;
	if (btn & HidNpadButton_Plus) native |= WYD_SWITCH_BTN_PLUS;
	if (btn & HidNpadButton_Minus) native |= WYD_SWITCH_BTN_MINUS;
	if (btn & HidNpadButton_L) native |= WYD_SWITCH_BTN_L;
	if (btn & HidNpadButton_R) native |= WYD_SWITCH_BTN_R;
	if (btn & HidNpadButton_ZL) native |= WYD_SWITCH_BTN_ZL;
	if (btn & HidNpadButton_ZR) native |= WYD_SWITCH_BTN_ZR;
	in.state.buttons = native;

	const HidAnalogStickState L = padGetStickPos(&pad, 0);
	const HidAnalogStickState R = padGetStickPos(&pad, 1);
	in.state.lx = NormAxis(static_cast<Sint16>(L.x));
	in.state.ly = NormAxis(static_cast<Sint16>(-L.y));
	in.state.rx = NormAxis(static_cast<Sint16>(R.x));
	in.state.ry = NormAxis(static_cast<Sint16>(-R.y));

	HidTouchScreenState touch{};
	if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0)
	{
		in.state.touch_down = true;
		in.state.touch_x = static_cast<int>(touch.touches[0].x);
		in.state.touch_y = static_cast<int>(touch.touches[0].y);
	}
	else if (!in.state.touch_down)
	{
		in.state.touch_x = -1;
		in.state.touch_y = -1;
	}
#endif

	if (in.state.buttons & WYD_SWITCH_BTN_PLUS)
		in.quit_requested = true;

	return !in.quit_requested;
}
