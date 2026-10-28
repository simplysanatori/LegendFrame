#pragma once
// Host stand-in: the session maths only sees these as opaque pointer types.
#include "windows.h"
struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct IDirect3DTexture9;
struct IDirect3DBaseTexture9;
struct D3DRECT { long x1, y1, x2, y2; };
struct D3DVIEWPORT9 { DWORD X, Y, Width, Height; float MinZ, MaxZ; };
