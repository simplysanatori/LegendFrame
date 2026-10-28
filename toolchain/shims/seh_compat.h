// seh_compat.h -- MSVC __try/__except(EXCEPTION_EXECUTE_HANDLER) for i686 mingw-w64.
//
// GCC has no __try/__except on 32-bit Windows. The TRL VR mod only uses the
// catch-everything form, with no filter expression use, no __finally, and no
// GetExceptionCode. This header maps that form onto a hand-registered x86 SEH
// frame (fs:[0] chain) + __builtin_setjmp/longjmp, so nesting with foreign SEH
// frames (the game's, the CRT's) keeps its order, exactly like MSVC's.
//
//   __try { A } __except(EXCEPTION_EXECUTE_HANDLER) { B }
// becomes
//   if (Scope s; (s.arm(), __builtin_setjmp(s.jb)) == 0) { A } else { B }
//
// Injected with -include so the mod's sources stay unmodified. Not applicable
// to x86_64 (native SEH there). Hardware faults only (not C++ throws).
#pragma once
#if defined(__MINGW32__) && defined(__i386__) && !defined(TRLVR_SEH_COMPAT_DONE)
#define TRLVR_SEH_COMPAT_DONE 1
// libstdc++ defines its own __try/__catch macros (bits/exception_defines.h) and
// would clobber ours if first included later; pull the whole library in now.
#ifndef MINGW_HAS_SECURE_API
#define MINGW_HAS_SECURE_API 1   // swprintf_s and friends, as MSVC provides
#endif
#ifdef __cplusplus
#include <bits/stdc++.h>
#endif
#include <windows.h>
#include <setjmp.h>

namespace seh_compat {
// Raw x86 SEH frame on the FS:[0] chain (what MSVC's __try does). On a fault the
// handler redirects the thread into resume(), which __builtin_longjmp's back to the
// guard and runs the else-branch. No C++ EH, no unwinding through ntdll frames.
// Design and code from the probe-clang lane (probe-clang/seh_compat.h).
struct Scope {
    Scope* prev;                       // == EXCEPTION_REGISTRATION_RECORD layout
    void*  handler;
    void*  jb[5];                      // __builtin_setjmp buffer
    bool   armed;
    Scope() : armed(false) {}
    ~Scope() { disarm(); }
    void arm();
    void disarm() { if (armed) { asm volatile("movl %0, %%fs:0" :: "r"(prev) : "memory"); armed = false; } }
};
__attribute__((noreturn, noinline, used)) static void __cdecl resume(Scope* s) {
    s->disarm();
    __builtin_longjmp(s->jb, 1);
}
static EXCEPTION_DISPOSITION __cdecl handler(EXCEPTION_RECORD* rec, void* est, CONTEXT* ctx, void*) {
    if (rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
    DWORD* sp = reinterpret_cast<DWORD*>(ctx->Esp) - 2;       // fake cdecl call: [ret][arg]
    sp[0] = 0; sp[1] = reinterpret_cast<DWORD>(est);
    ctx->Esp = reinterpret_cast<DWORD>(sp);
    ctx->Eip = reinterpret_cast<DWORD>(&resume);
    return ExceptionContinueExecution;
}
__attribute__((noinline)) inline void Scope::arm() {
    asm volatile("movl %%fs:0, %0" : "=r"(prev));
    handler = reinterpret_cast<void*>(&seh_compat::handler);
    asm volatile("movl %0, %%fs:0" :: "r"(this) : "memory");
    armed = true;
}
}

#include <intrin.h>
// MSVC intrinsic; mingw's <intrin.h> declares it without a definition.
#undef _ReturnAddress
#define _ReturnAddress() __builtin_return_address(0)

#undef __try
#undef __except
// The asm barrier matters: without it GCC -O2 schedules __builtin_setjmp's save of ESP into jb[]
// AFTER the first guarded load, so a fault on that first load longjmps with a garbage ESP.
#define __try if (::seh_compat::Scope seh_scope_; (seh_scope_.arm(), __builtin_setjmp(seh_scope_.jb)) == 0 && ({ __asm__ __volatile__("" ::: "memory"); 1; }))
#define __except(x) else

// ---------------------------------------------------------------------------
// GCC/MSVC thiscall mismatch for virtuals that return a struct in memory:
// MSVC passes `this` in ECX and the hidden return pointer first on the stack;
// GCC puts the hidden pointer in ECX and `this` on the stack. OpenVR's
// openvr_api.dll (MSVC ABI) therefore needs these called by hand.
// container-build.sh rewrites `obj->GetEyeToHeadTransform(a)` (and the other
// struct-returning OpenVR methods) to
//   trlvr_abi::call_sret(obj, &decltype(*obj)::GetEyeToHeadTransform, a)
// on its working copy and saves the result as abi-fix.diff.
#if __has_include(<openvr.h>)
#include <openvr.h>
#include <type_traits>
namespace trlvr_abi
{
    template <class R, class C, class... A>
    inline R call_sret(C* obj, R (C::*pmf)(A...), A... args)
    {
        static_assert(sizeof(pmf) == 2 * sizeof(void*), "unexpected pmf layout");
        size_t words[2];
        __builtin_memcpy(words, &pmf, sizeof(words));
        // Itanium-style PMF to a virtual: first word = vtable byte offset + 1.
        const size_t slot = (words[0] - 1) / sizeof(void*);
        void** vtbl = *reinterpret_cast<void***>(obj);
        typedef void (__attribute__((thiscall)) *Fn)(void*, R*, A...);
        R out{};
        reinterpret_cast<Fn>(vtbl[slot])(obj, &out, args...);
        return out;
    }
}
#endif
#endif
