#pragma once
#include "strmif.h"
#include "win32_extras.h"

using OAHWND = HWND;

struct IVideoWindow {
	virtual ~IVideoWindow() = default;
	virtual HRESULT NotifyOwnerMessage(OAHWND, UINT, WPARAM, LPARAM) { return S_OK; }
	virtual HRESULT put_Owner(OAHWND) { return S_OK; }
	virtual HRESULT put_MessageDrain(OAHWND) { return S_OK; }
	virtual ULONG AddRef() { return 1; }
	virtual ULONG Release() { return 1; }
};

struct IBasicVideo {
	virtual ~IBasicVideo() = default;
	virtual ULONG AddRef() { return 1; }
	virtual ULONG Release() { return 1; }
};

struct IMediaControl {
	virtual ~IMediaControl() = default;
	virtual HRESULT Run() { return S_OK; }
	virtual HRESULT Stop() { return S_OK; }
	virtual HRESULT Pause() { return S_OK; }
	virtual ULONG AddRef() { return 1; }
	virtual ULONG Release() { return 1; }
};

struct IMediaEventEx {
	virtual ~IMediaEventEx() = default;
	virtual ULONG AddRef() { return 1; }
	virtual ULONG Release() { return 1; }
};

struct IGraphBuilder {
	virtual ~IGraphBuilder() = default;
	virtual ULONG AddRef() { return 1; }
	virtual ULONG Release() { return 1; }
};
