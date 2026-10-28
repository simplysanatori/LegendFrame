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
}
