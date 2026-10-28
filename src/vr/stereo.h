#pragma once
// Same-frame stereo: both eyes drawn in the same frame, in step.
//
// Alternating eyes gave real depth cheaply, but each eye only refreshes at
// half the frame rate and the two are taken a frame apart. See stereo.cpp.

#include <windows.h>
#include <d3d9.h>

namespace trlvr
{
    // On, and everything it needs is in place.
    bool stereo_same_frame_active();

    // The size of one eye's half of the target. The surface itself is twice
    // this wide.
    void stereo_set_eye_size(unsigned width, unsigned height);
    unsigned stereo_eye_width();
    unsigned stereo_eye_height();

    // Double the width of a surface the game is asking for, if it is one the
    // scene is drawn into.
    void stereo_widen(UINT* width, UINT height);

    // How many times the draw about to be issued should be sent: two while the
    // scene is being drawn, one otherwise.
    int stereo_passes();

    // Point the next draw at one eye's half of the target.
    void stereo_begin_pass(IDirect3DDevice9* device, int pass);

    // Put the viewport and the constants back.
    void stereo_end_draw(IDirect3DDevice9* device);

    // --- Single-pass stereo (LegendFrame SPS-VI, docs/DESIGN.md 3.1, 5.2) ---
    //
    // Live switch between single-pass and two-pass ([vr] stereo_mode is the
    // start value). Call on the render thread between frames (the Shift+F6
    // hotkey and the settings panel, tune.cpp / overlay.cpp). It logs
    // "stereo: live toggle -> single-pass|two-pass (Shift+F6)" itself.
    // Turning it on only takes if DXVK enabled single-pass at CreateDevice
    // ([vr] stereo_mode = singlepass at start); otherwise it stays two-pass
    // and says so in the log.
    void stereo_set_single_pass(bool on);
    // Scene draws are currently going out single-pass where eligible.
    bool stereo_single_pass_active();

    // CreateDevice, with the real (DXVK) device, before the game has created
    // any shader: with [vr] stereo_mode = singlepass, asks DXVK for its
    // IDirect3DVRStereo9 and enables single-pass. Logs the outcome.
    void stereo_device_created(IDirect3DDevice9* real_device);
    // The real device is about to be released.
    void stereo_device_closing(IDirect3DDevice9* real_device);
    // After a device Reset (DXVK drops the armed state and the constants).
    void stereo_device_reset();

    // A scene draw (stereo_passes() == 2, not the water reflection pass)
    // that single-pass could take: no per-eye texture swap, a programmable
    // vertex shader, no stream instancing. Only call while
    // stereo_single_pass_active(); a draw that fails is counted by reason
    // and goes out two-pass.
    bool stereo_single_pass_eligible(IDirect3DDevice9* device,
                                     bool vertex_shader_bound,
                                     bool stream_instancing);

    // One single-pass scene draw. stereo_single_begin computes both eyes'
    // c0..c3 exactly as the two passes would (left then right, and an eye
    // whose substitution fails keeps what the previous pass left in c0) and
    // arms DXVK with them, skipping the call when nothing changed since the
    // last arm (sticky). Then the caller issues the draw once and calls
    // stereo_single_end. If arming fails, single-pass is switched off and
    // false comes back; the caller then draws two-pass with
    // stereo_begin_pass_precomputed, so nothing is substituted twice.
    struct StereoSinglePass
    {
        float c0[2][16];        // what each eye's draw uses
        bool substituted[2];    // vr_substitute_c0_for_eye succeeded
    };
    bool stereo_single_begin(IDirect3DDevice9* device, StereoSinglePass* draw);
    void stereo_single_end(IDirect3DDevice9* device,
                           const StereoSinglePass& draw);
    void stereo_begin_pass_precomputed(IDirect3DDevice9* device, int pass,
                                       const StereoSinglePass& draw);

    // Back to mono draws: disarms DXVK if a single-pass draw armed it. Every
    // draw that is not single-pass goes out disarmed. Free when not armed.
    void stereo_disarm();

    // Bookkeeping for the c0 carry-over: after a game write to any of
    // c0..c3 (noted_upload = it went through vr_note_c0, so c0..c3 now hold
    // vr_draw_c0_restore()), and after the proxy put vr_draw_c0_restore()
    // back itself.
    void stereo_note_vs_constants(UINT start_register, UINT count,
                                  bool noted_upload);
    void stereo_note_c0_restored();

    // Statistics ([vr] stereo_stats): draws by path.
    void stereo_note_mono_draw();
    void stereo_note_reflection_draw();

    // EndScene: the scene's draws are over. Disarms, and logs the
    // "stereo stats:" line every 5 s.
    void stereo_end_scene();
}
