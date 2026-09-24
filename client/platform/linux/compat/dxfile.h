#pragma once
// Stub limpo (o dxfile.h do DirectX SDK tem Ctrl+Z no fim e quebra o GCC).
#include <d3d9.h>
#include "win32_extras.h"

struct IDirectXFile;
struct IDirectXFileEnumObject;
struct IDirectXFileData;
struct IDirectXFileDataReference;
struct IDirectXFileBinary;
struct IDirectXFileObject;

using LPDIRECTXFILE = IDirectXFile*;
using LPDIRECTXFILEENUMOBJECT = IDirectXFileEnumObject*;
using LPDIRECTXFILEDATA = IDirectXFileData*;
using LPDIRECTXFILEOBJECT = IDirectXFileObject*;

#ifndef DXFILE_OK
#define DXFILE_OK 0
#endif

inline HRESULT WINAPI DirectXFileCreate(LPDIRECTXFILE*) { return E_NOTIMPL; }
