#pragma once
// Host (Linux/aarch64) stand-in for the few Windows names that the session
// maths and its headers use, so src/test/mathtest.sh can build vr_session.cpp
// natively. Not used by the real (mingw/MSVC) build.
#include <cstdint>
#include <cstddef>
#include <cwchar>

typedef unsigned long DWORD;
typedef unsigned long long ULONGLONG;
typedef unsigned int UINT;
typedef int BOOL;
typedef long HRESULT;
typedef void* HANDLE;
typedef DWORD D3DCOLOR;
struct RECT { long left, top, right, bottom; };
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAX_PATH 260
#define WINAPI

// SEH: the host test maps the game's addresses (mathtest.sh), so a read
// never faults; __except blocks are never taken.
#define EXCEPTION_EXECUTE_HANDLER 1
// libstdc++ defines its own __try; host_prelude.h (-include) pulls the
// library in first, and this wins afterwards.
#undef __try
#define __try if (true)
#define __except(filter) else

// Test-controlled clock (mathtest.cpp defines these).
DWORD GetTickCount();
ULONGLONG GetTickCount64();
UINT GetPrivateProfileIntW(const wchar_t* section, const wchar_t* key,
                           int fallback, const wchar_t* path);
