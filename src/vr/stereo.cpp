// Same-frame stereo: both eyes drawn in the same frame, in step.
//
// What alternating eyes cost, measured (FINDINGS section 20): the game holds a
// locked 90 fps with GPU time to spare, but each eye only refreshes at 45 Hz
// and the two halves of a stereo pair are taken a frame apart. Section 22
// showed that temporal disparity is far larger than the 6.4 cm of real
// parallax, which is why the eyes were so reluctant to fuse.
//
// Drawing the scene twice trades that frame rate for correctness: near 45 fps
// with both eyes **in step**, which is the better half of the bargain.
//
// The shape of it:
//
//   * The render surfaces the scene is drawn into become twice as wide, and
//     each eye gets half. That is cheaper than a render-target switch, which
//     would otherwise happen twice per draw across a few thousand draws.
//
//   * Every draw issued while the scene target is bound goes out twice, once
//     per viewport, with the matrix for that eye. This is why the c0 decision
//     had to move from constant-upload time to the draw: one upload can serve
//     many draws, but each pass needs its own matrix.
//
//   * At the end, the double-wide surface is handed to the compositor twice
//     with texture bounds selecting each half -- no copy, and no separate eye
//     targets at all.
//
// The known limitation is post-processing. Those passes read the scene target
// as a texture and would treat the two halves as one image, so the first
// version asks for Depth of Field and Fullscreen Effects to be off in the
// game's own Setup dialog. That is a real restriction, not a temporary one:
// lifting it means understanding each post pass well enough to run it per eye.

#include "stereo.h"
#include "perf_cpu.h"

#include "reflection_stereo.h"
#include "tune.h"
#include "vr_math.h"
#include "vr_session.h"

#include "../common/config.h"
#include "../common/log.h"

#include "d3d9_vr.h"

#include <cstring>

namespace trlvr
{
    namespace
    {
        unsigned g_eye_w = 0;
        unsigned g_eye_h = 0;

        bool g_saved_viewport = false;
        D3DVIEWPORT9 g_viewport{};

        bool g_patched_c0 = false;
        bool g_announced = false;

        // Two-pass with [vr] stereo_shared_math = 1: pass 0 makes both eyes'
        // c0 in one vr_substitute_c0_stereo call and pass 1 uses the right
        // eye's from here (nothing between the passes changes the inputs).
        bool g_pass_right_pending = false;
        bool g_pass_right_ok = false;
        float g_pass_right[16]{};

        // Single-pass stereo (SPS-VI). DXVK's interface, held from
        // CreateDevice until the device goes; null when single-pass was not
        // asked for or DXVK cannot do it.
        IDirect3DVRStereo9* g_sps = nullptr;
        IDirect3DDevice9* g_sps_device = nullptr;
        bool g_sps_on = false;          // live switch (stereo_mode, Shift+F6)

        // What DXVK is armed with (sticky until ClearStereoDraw / Reset).
        bool g_armed = false;
        float g_armed_c0[2][16]{};
        unsigned g_armed_w = 0;
        unsigned g_armed_h = 0;

        // c0..c3 on the device hold vr_draw_c0_restore() -- true after a
        // noted c0..c3 upload or a proxy restore, false after any other
        // write to c0..c3 or a Reset. Two-pass needs no such thing: it reads
        // its carry-over straight out of the register it overwrites.
        bool g_c0_is_uploaded = false;

        struct StereoCounts
        {
            unsigned single;
            unsigned eye_textures;
            unsigned fixed_function;
            unsigned instanced;
            unsigned reflection;
            unsigned mono;
            unsigned arm_calls;
        };
        StereoCounts g_counts{};

        // Cost of the arm call and of the whole single-pass begin, summed
        // over the 5 s interval (only timed while [vr] stereo_stats is on).
        // The begin figure excludes the matrix maths (perf cpu has that) but
        // includes the arm call.
        struct StereoTimes
        {
            double arm_ms;
            double begin_ms;
            unsigned arm_skipped;
            unsigned frames;    // EndScenes, taken as frames
        };
        StereoTimes g_times{};
        LARGE_INTEGER g_qpc_freq{};

        LONGLONG qpc_now()
        {
            LARGE_INTEGER t{};
            QueryPerformanceCounter(&t);
            return t.QuadPart;
        }

        double qpc_ms(LONGLONG from, LONGLONG to)
        {
            return double(to - from) * 1000.0 / double(g_qpc_freq.QuadPart);
        }

        // Times stereo_single_begin on every return path.
        struct BeginTimer
        {
            bool on;
            LONGLONG start = 0;
            double math_ms = 0.0;
            BeginTimer() : on(config().stereo_stats)
            {
                if (on)
                {
                    if (!g_qpc_freq.QuadPart)
                        QueryPerformanceFrequency(&g_qpc_freq);
                    start = qpc_now();
                }
            }
            ~BeginTimer()
            {
                if (on)
                    g_times.begin_ms += qpc_ms(start, qpc_now()) - math_ms;
            }
        };
        bool g_stats_started = false;
        DWORD g_stats_tick = 0;
        UINT g_dxvk_rejected_seen = 0;

        // One eye's pass viewport: that eye's half of the target.
        void set_pass_viewport(IDirect3DDevice9* device, int pass)
        {
            if (!g_saved_viewport)
            {
                device->GetViewport(&g_viewport);
                g_saved_viewport = true;
            }

            // Each eye gets its own half, keeping whatever depth range the game
            // had set -- that belongs to the pass, not to us.
            unsigned eye_w = g_eye_w;
            unsigned eye_h = g_eye_h;
            vr_stereo_target_eye_size(&eye_w, &eye_h);

            D3DVIEWPORT9 vp = g_viewport;
            vp.X = (pass == 0) ? 0 : eye_w;
            vp.Y = 0;
            vp.Width = eye_w;
            vp.Height = eye_h;
            device->SetViewport(&vp);
        }

        // One eye's pass c0: only uploaded if the substitution succeeded.
        void set_pass_c0(IDirect3DDevice9* device, bool substituted,
                         const float* regs)
        {
            if (substituted)
            {
                device->SetVertexShaderConstantF(0, regs, 4);
                g_patched_c0 = true;
            }
        }
    }

    void stereo_set_eye_size(unsigned width, unsigned height)
    {
        g_eye_w = width;
        g_eye_h = height;
    }

    unsigned stereo_eye_width()  { return g_eye_w; }
    unsigned stereo_eye_height() { return g_eye_h; }

    bool stereo_same_frame_active()
    {
        return config().stereo_same_frame && vr_ready() && g_eye_w != 0;
    }

    void stereo_widen(UINT* width, UINT height)
    {
        // Only surfaces exactly the size of one eye. Half-resolution post
        // targets and shadow maps are left alone -- they are not what the
        // scene is drawn into, and doubling them would break the passes that
        // sample them without helping anything.
        if (!width || !stereo_same_frame_active())
            return;
        if (*width == g_eye_w && height == g_eye_h)
            *width = g_eye_w * 2;
    }

    int stereo_passes()
    {
        if (!stereo_same_frame_active() || !vr_drawing_the_scene())
            return 1;
        if (!vr_have_game_c0())
            return 1;

        if (!g_announced)
        {
            g_announced = true;
            log("stereo: drawing both eyes in the same frame, %ux%u an eye",
                g_eye_w, g_eye_h);
            log("        Depth of Field and Fullscreen Effects need to be off "
                "in the game's Setup dialog -- the post passes read the scene "
                "target as one image and would treat both eyes as one.");
        }
        return 2;
    }

    void stereo_begin_pass(IDirect3DDevice9* device, int pass)
    {
        if (!device)
            return;

        set_pass_viewport(device, pass);

        const Eye eye = (pass == 0) ? EyeLeft : EyeRight;
        float regs[16];
        if (vr_stereo_shared_math() && (pass == 0 || g_pass_right_pending))
        {
            // M4: both eyes at once, bit-identical to the two single calls
            // (and their side effects, left then right).
            if (pass == 0)
            {
                perf_cpu_begin(PerfDrawMath);
                g_pass_right_ok = vr_substitute_c0_stereo(vr_game_c0(), regs,
                                                          g_pass_right);
                perf_cpu_end(PerfDrawMath);
                g_pass_right_pending = true;
                set_pass_c0(device, g_pass_right_ok, regs);
            }
            else
            {
                g_pass_right_pending = false;
                set_pass_c0(device, g_pass_right_ok, g_pass_right);
            }
            return;
        }
        perf_cpu_begin(PerfDrawMath);
        const bool substituted =
            vr_substitute_c0_for_eye(vr_game_c0(), regs, eye);
        perf_cpu_end(PerfDrawMath);
        set_pass_c0(device, substituted, regs);
    }

    void stereo_end_draw(IDirect3DDevice9* device)
    {
        if (!device)
            return;

        if (g_patched_c0)
        {
            // Back to whatever the single-eye path had uploaded, so anything
            // that does not go through here still sees what it expects.
            device->SetVertexShaderConstantF(0, vr_draw_c0_restore(), 4);
            g_patched_c0 = false;
            g_c0_is_uploaded = true;
        }
        g_pass_right_pending = false;

        if (g_saved_viewport)
        {
            device->SetViewport(&g_viewport);
            g_saved_viewport = false;
        }
    }

    // --- Single-pass stereo (SPS-VI) ---------------------------------------
    //
    // One draw for both eyes. DXVK is armed with both eyes' c0..c3 and the
    // eye size; it then draws each armed draw once with two instances, two
    // viewports (the two eye halves, the game's depth range) and the vertex
    // shader reading c0..c3 from a per-view bank. Same images as the two
    // passes, from one draw instead of two (docs/DESIGN.md 3.1).

    void stereo_device_created(IDirect3DDevice9* real_device)
    {
        // Enabled whatever stereo_mode says, so every vertex shader gets the
        // stereo variant and Shift+F6 can switch either way; stereo_mode
        // only picks the path the game starts on.
        if (!real_device || !vr_ready())
            return;
        if (!config().stereo_same_frame)
        {
            if (config().stereo_mode == 1)
                log("stereo: stereo_mode = singlepass ignored -- "
                    "stereo_same_frame = 0 (alternate eyes)");
            return;
        }
        stereo_device_closing(g_sps_device);

        typedef HRESULT(__stdcall* CreateVr9Fn)(IDirect3DDevice9*,
                                                IDirect3DVR9**);
        HMODULE backend = GetModuleHandleW(L"d3d9_dxvk.dll");
        CreateVr9Fn create = backend
            ? (CreateVr9Fn)GetProcAddress(backend, "Direct3DCreateVR9")
            : nullptr;
        IDirect3DVR9* vr = nullptr;
        HRESULT hr = create ? create(real_device, &vr) : E_NOINTERFACE;
        IDirect3DVRStereo9* sps = nullptr;
        if (SUCCEEDED(hr) && vr)
        {
            hr = vr->QueryInterface(__uuidof(IDirect3DVRStereo9),
                                    (void**)&sps);
            vr->Release();
        }
        else if (SUCCEEDED(hr))
        {
            hr = E_NOINTERFACE;
        }
        if (FAILED(hr) || !sps)
        {
            if (hr == E_NOINTERFACE || SUCCEEDED(hr))
                log("stereo: DXVK has no single-pass interface "
                    "(E_NOINTERFACE) -- two-pass");
            else
                log("stereo: DXVK has no single-pass interface (0x%08X) "
                    "-- two-pass", (unsigned)hr);
            return;
        }

        // Before the game creates any vertex shader: DXVK compiles every VS
        // made from now on with the stereo variant, and refuses once one
        // exists.
        D3D9_VR_STEREO_CAPS caps{};
        log_set_activity("DXVK EnableSinglePassStereo");
        hr = sps->EnableSinglePassStereo(D3D9_VR_STEREO_MODE_VIEWPORT_INDEX,
                                         &caps);
        log_set_activity(nullptr);
        if (FAILED(hr))
        {
            log("stereo: single-pass requested but EnableSinglePassStereo "
                "failed (0x%08X) -- two-pass", (unsigned)hr);
            sps->Release();
            return;
        }

        g_sps = sps;
        g_sps_device = real_device;
        g_sps_on = config().stereo_mode == 1;
        g_armed = false;
        g_c0_is_uploaded = false;
        log("stereo: single-pass (viewport index) enabled, DXVK stereo "
            "interface v%u", (unsigned)caps.Version);
        log(g_sps_on ? "stereo: starting in single-pass (stereo_mode=singlepass)"
                     : "stereo: starting in two-pass (stereo_mode=twopass)");
    }

    void stereo_device_closing(IDirect3DDevice9* real_device)
    {
        if (!g_sps || real_device != g_sps_device)
            return;
        g_sps->Release();
        g_sps = nullptr;
        g_sps_device = nullptr;
        g_sps_on = false;
        g_armed = false;
    }

    void stereo_device_reset()
    {
        // DXVK disarms on Reset and the constants go back to zero.
        g_armed = false;
        g_c0_is_uploaded = false;
    }

    void stereo_set_single_pass(bool on)
    {
        // Between frames: whatever is armed goes, so the next scene draw
        // re-arms (single-pass) or goes out disarmed (two-pass).
        stereo_disarm();
        if (on && !g_sps)
        {
            g_sps_on = false;
            log("stereo: live toggle -> two-pass (Shift+F6) -- single-pass "
                "is not available: it needs stereo_same_frame = 1 at start and "
                "the d3d9_dxvk.dll built with this d3d9.dll");
            return;
        }
        g_sps_on = on;
        log("stereo: live toggle -> %s (Shift+F6)",
            on ? "single-pass" : "two-pass");
    }

    bool stereo_single_pass_active()
    {
        return g_sps_on && g_sps;
    }

    bool stereo_single_pass_eligible(IDirect3DDevice9* device,
                                     bool vertex_shader_bound,
                                     bool stream_instancing)
    {
        // The right eye samples the reflection twin, or each eye its own
        // half of the WaterFX screen copy: a texture per eye.
        if (reflection_stereo_draw_needs_eye_textures(device))
        {
            ++g_counts.eye_textures;
            return false;
        }
        // Fixed-function: no vertex shader to read a per-view bank (DXVK
        // would draw it per eye itself; the proxy's two passes do the same).
        if (!vertex_shader_bound)
        {
            ++g_counts.fixed_function;
            return false;
        }
        // Stream instancing owns the instance index.
        if (stream_instancing)
        {
            ++g_counts.instanced;
            return false;
        }
        return true;
    }

    bool stereo_single_begin(IDirect3DDevice9* device, StereoSinglePass* draw)
    {
        // Both eyes, left first, as the two passes call it: the
        // substitution has per-call side effects (the sky-pinning learner
        // counts calls, the binocular zoom EMA steps per call).
        BeginTimer timer;
        const float* game = vr_game_c0();
        if (vr_stereo_shared_math())
        {
            // M4: one call for both eyes, bit-identical to the two below.
            // It never fails for one eye only, and on failure writes
            // neither output (the carry-over below fills both).
            const LONGLONG m0 = timer.on ? qpc_now() : 0;
            perf_cpu_begin(PerfDrawMath);
            const bool ok = vr_substitute_c0_stereo(game, draw->c0[0],
                                                    draw->c0[1]);
            perf_cpu_end(PerfDrawMath);
            if (timer.on)
                timer.math_ms += qpc_ms(m0, qpc_now());
            draw->substituted[0] = ok;
            draw->substituted[1] = ok;
        }
        else
        {
            const LONGLONG m0 = timer.on ? qpc_now() : 0;
            for (int e = 0; e < 2; ++e)
            {
                perf_cpu_begin(PerfDrawMath);
                draw->substituted[e] = vr_substitute_c0_for_eye(
                    game, draw->c0[e], e == 0 ? EyeLeft : EyeRight);
                perf_cpu_end(PerfDrawMath);
            }
            if (timer.on)
                timer.math_ms += qpc_ms(m0, qpc_now());
        }

        // The two passes only upload on success, so a failed eye draws with
        // whatever c0 held: the left eye with what was there before the
        // draw, the right eye with the left eye's.
        if (!draw->substituted[0])
        {
            const float* before = vr_draw_c0_restore();
            float regs[16];
            if (!g_c0_is_uploaded && device &&
                SUCCEEDED(device->GetVertexShaderConstantF(0, regs, 4)))
                before = regs;
            std::memcpy(draw->c0[0], before, sizeof(draw->c0[0]));
        }
        if (!draw->substituted[1])
            std::memcpy(draw->c0[1], draw->c0[0], sizeof(draw->c0[1]));

        if (!g_sps_on || !g_sps)
            return false;

        unsigned eye_w = g_eye_w;
        unsigned eye_h = g_eye_h;
        vr_stereo_target_eye_size(&eye_w, &eye_h);

        // Sticky: a run of draws with the same matrices (one object's
        // pieces, everything under one upload) arms once.
        if (!g_armed || eye_w != g_armed_w || eye_h != g_armed_h ||
            std::memcmp(g_armed_c0, draw->c0, sizeof(g_armed_c0)) != 0)
        {
            const LONGLONG a0 = timer.on ? qpc_now() : 0;
            const HRESULT hr = g_sps->SetStereoDraw(draw->c0[0], draw->c0[1],
                                                    eye_w, eye_h);
            if (timer.on)
                g_times.arm_ms += qpc_ms(a0, qpc_now());
            if (FAILED(hr))
            {
                stereo_disarm();
                g_sps_on = false;
                log("stereo: SetStereoDraw failed (0x%08X, eye %ux%u) -- "
                    "two-pass from here on", (unsigned)hr, eye_w, eye_h);
                return false;
            }
            std::memcpy(g_armed_c0, draw->c0, sizeof(g_armed_c0));
            g_armed_w = eye_w;
            g_armed_h = eye_h;
            g_armed = true;
            ++g_counts.arm_calls;
        }
        else
            ++g_times.arm_skipped;
        ++g_counts.single;
        return true;
    }

    void stereo_single_end(IDirect3DDevice9* device,
                           const StereoSinglePass& draw)
    {
        // The bank leaves the API c0..c3 alone, so nothing needs restoring.
        // Two-pass puts vr_draw_c0_restore() back after a draw with a
        // substituted eye, though, and the next draws must see the same
        // registers either way. Only differs after a c0 write the proxy did
        // not note, so this costs nothing normally.
        if ((draw.substituted[0] || draw.substituted[1]) &&
            !g_c0_is_uploaded && device)
        {
            device->SetVertexShaderConstantF(0, vr_draw_c0_restore(), 4);
            g_c0_is_uploaded = true;
        }
    }

    void stereo_begin_pass_precomputed(IDirect3DDevice9* device, int pass,
                                       const StereoSinglePass& draw)
    {
        // stereo_begin_pass with the substitution already done.
        if (!device)
            return;
        set_pass_viewport(device, pass);
        const int eye = (pass == 0) ? 0 : 1;
        set_pass_c0(device, draw.substituted[eye], draw.c0[eye]);
    }

    void stereo_disarm()
    {
        if (g_armed && g_sps)
            g_sps->ClearStereoDraw();
        g_armed = false;
    }

    void stereo_note_vs_constants(UINT start_register, UINT count,
                                  bool noted_upload)
    {
        if (count && start_register < 4)
            g_c0_is_uploaded = noted_upload;
    }

    void stereo_note_c0_restored()
    {
        g_c0_is_uploaded = true;
    }

    void stereo_note_mono_draw()
    {
        ++g_counts.mono;
    }

    void stereo_note_reflection_draw()
    {
        ++g_counts.reflection;
    }

    void stereo_end_scene()
    {
        stereo_disarm();

        if (!config().stereo_stats)
        {
            g_stats_started = false;
            return;
        }
        ++g_times.frames;
        const DWORD now = GetTickCount();
        if (!g_stats_started)
        {
            // Count from here, not from whenever the key was turned on.
            g_stats_started = true;
            g_stats_tick = now;
            g_counts = StereoCounts{};
            g_times = StereoTimes{};
            if (g_sps)
                g_sps->GetStereoStats(nullptr, &g_dxvk_rejected_seen);
            return;
        }
        if (now - g_stats_tick < 5000)
            return;
        g_stats_tick = now;

        // DXVK's count is a running total; the line is per 5 s like the rest.
        UINT rejected_total = g_dxvk_rejected_seen;
        if (g_sps)
            g_sps->GetStereoStats(nullptr, &rejected_total);
        const unsigned rejected =
            (unsigned)(rejected_total - g_dxvk_rejected_seen);
        g_dxvk_rejected_seen = rejected_total;

        const StereoCounts& c = g_counts;
        const double frames = g_times.frames ? double(g_times.frames) : 1.0;
        const unsigned fallback = c.eye_textures + c.fixed_function +
            c.instanced + c.reflection;
        log("stereo stats: single %u, fallback %u (eye-textures %u, "
            "fixed-function %u, instanced %u, reflection %u), mono %u, "
            "arm calls %u, dxvk rejected %u, arm %.2f ms/frame (%u calls, "
            "%u skipped), begin %.2f ms/frame", c.single, fallback,
            c.eye_textures, c.fixed_function, c.instanced, c.reflection,
            c.mono, c.arm_calls, rejected, g_times.arm_ms / frames,
            c.arm_calls, g_times.arm_skipped, g_times.begin_ms / frames);
        g_counts = StereoCounts{};
        g_times = StereoTimes{};
    }
}
