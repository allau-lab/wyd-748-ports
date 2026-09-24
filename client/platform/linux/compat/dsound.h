#pragma once
// Tipos DirectSound usados pelo client — buffers reais vivem em dsutil_linux (SDL).
#include <d3d9.h>
#include "win32_extras.h"
#include "mmreg.h"

#ifndef DSBPLAY_LOOPING
#define DSBPLAY_LOOPING 0x00000001
#endif
#ifndef DSBCAPS_CTRLVOLUME
#define DSBCAPS_CTRLVOLUME 0x00000080
#endif
#ifndef DSBCAPS_CTRL3D
#define DSBCAPS_CTRL3D 0x00000010
#endif
#ifndef DS_OK
#define DS_OK 0
#endif
#ifndef DSSCL_PRIORITY
#define DSSCL_PRIORITY 2
#endif
#ifndef DSBVOLUME_MIN
#define DSBVOLUME_MIN (-10000)
#define DSBVOLUME_MAX 0
#endif

struct DS3DVECTOR3 {
	FLOAT x {}, y {}, z {};
};
struct DS3DBUFFER {
	DWORD dwSize {};
	DS3DVECTOR3 vPosition {};
	DS3DVECTOR3 vVelocity {};
	DWORD dwInsideConeAngle {};
	DWORD dwOutsideConeAngle {};
	DS3DVECTOR3 vConeOrientation {};
	LONG lConeOutsideVolume {};
	FLOAT flMinDistance {};
	FLOAT flMaxDistance {};
	DWORD dwMode {};
};
using LPDS3DBUFFER = DS3DBUFFER*;

struct IDirectSoundBuffer {
	virtual ~IDirectSoundBuffer() = default;
	virtual HRESULT SetVolume(LONG) = 0;
	virtual HRESULT GetStatus(DWORD*) = 0;
	virtual HRESULT Play(DWORD, DWORD, DWORD) = 0;
	virtual HRESULT Stop() = 0;
	virtual ULONG AddRef() { return 1; }
	virtual ULONG Release() { delete this; return 0; }
};
using LPDIRECTSOUNDBUFFER = IDirectSoundBuffer*;
using LPDIRECTSOUND8 = void*;
using LPDIRECTSOUND = void*;
using LPDIRECTSOUND3DLISTENER = void*;
using LPDIRECTSOUND3DBUFFER = void*;

using LPDSENUMCALLBACK = BOOL(CALLBACK*)(LPGUID, LPCSTR, LPCSTR, LPVOID);
using LPDSENUMCALLBACKA = LPDSENUMCALLBACK;
using LPDSENUMCALLBACKW = LPDSENUMCALLBACK;

HRESULT DirectSoundCreate8(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
inline HRESULT DirectSoundCreate(LPCGUID g, LPDIRECTSOUND* o, LPUNKNOWN u)
{
	return DirectSoundCreate8(g, reinterpret_cast<LPDIRECTSOUND8*>(o), u);
}
inline HRESULT DirectSoundEnumerateA(LPDSENUMCALLBACK, LPVOID) { return DS_OK; }
#define DirectSoundEnumerate DirectSoundEnumerateA
