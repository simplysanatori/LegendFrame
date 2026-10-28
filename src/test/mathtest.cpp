// Tests for the projection maths, against the matrix the game actually uses.
//
// The registers below are a real capture from Bolivia, not a made-up example.
// If the conventions in vr_math.h are right, feeding the game's own field of
// view back through the tangent path has to reproduce them exactly -- that is
// the test that matters, because everything else builds on it.

// ---------------------------------------------------------------------------
// M4 session suite (host only, src/test/mathtest.sh session).
//
// vr_session.cpp is built several times, each copy in its own namespace
// (-Dtrlvr=trlvr_<copy>) with its own statics, against the stubs below, which
// read the game state from one shared scenario. Every copy is fed the same
// call sequence, so their outputs can be compared bit for bit:
//   ref  the steam-frame original (git), vr_substitute_c0_for_eye L then R
//   s    this tree, single calls, shared maths and eye cache off
//   sc   this tree, single calls, eye cache on (what a two-pass caller gets)
//   dn   this tree, vr_substitute_c0_stereo, eye cache off
//   dc   this tree, vr_substitute_c0_stereo, eye cache on (the default)
//   lg   this tree, vr_substitute_c0_stereo with stereo_shared_math = 0
// ---------------------------------------------------------------------------
#if defined(MATHTEST_SESSION) || defined(MATHTEST_SESSION_STUBS)
#include <string>
#include <vector>

// The game state the session reads through its dependencies.
struct MathtestScenario
{
    // [vr] settings
    bool head_tracking = true, hmd_drives_camera = true;
    bool hmd_drives_position = true, head_rotation = true;
    bool head_position = true, ui_passthrough = true;
    bool pause_world_locked = true, menu_world_locked = true;
    float world_scale = 291.0f, ui_scale = 0.6f, hud_follow = 0.85f;
    float head_position_scale = 1.0f, head_rotation_scale = 1.0f;
    float hud_offset_x = 0.0f, hud_offset_y = 0.0f;
    // interface
    bool interface = false, screen_wipe = false, world_marker = false;
    bool world_locked_hud = false, menu = false, pause = false;
    short root_screen = -1;
    bool binocular = false;
    // camera
    bool first_person = false, accurate_aim = false;
    int gpu_hand = -1;
    float world_scale_factor = 1.0f, stereo_scale = 1.0f;
    float theatre_scale = 1.0f, marker_depth = 600.0f;
    float marker_distance = 1.5f;
    bool camera_ok = true;
    float retail[3] = {}, eye[3] = {}, focus[3] = {};
    bool water = false;
    unsigned long long tick = 100000;
    const wchar_t* exe_dir = L"/tmp/";
    int shared_math_ini = 1, capture_ini = 0;
};
extern MathtestScenario g_scn;

// One copy's entry points (plain function pointers: the copies' own types
// live in their renamed namespaces).
struct SessionApi
{
    const char* name;
    bool (*init)();
    bool (*for_eye)(const float* in, float* out, int eye);
    bool (*stereo)(const float* in, float* left, float* right); // null: ref
    void (*set_shared)(bool shared, bool cache);                // null: ref
    void (*capture)(bool on);                                   // null: ref
    void (*cache_stats)(unsigned* hits, unsigned* misses);      // null: ref
    void (*set_eye_views)(const float views[2][16]);
    void (*set_head_pose)(const float pose34[12], bool valid);
    void (*frame)();
    void (*set_scene_size)(unsigned w, unsigned h);
    void (*set_rt_size)(unsigned w, unsigned h);
    int (*sky_hits)();
    void (*sync)();                 // copy the scenario's settings in
    void (*recenter)();
    void (*note_main_camera_head)();
    std::vector<std::string>* log;
};
#endif

#if defined(MATHTEST_SESSION_STUBS)
#include "../vr/vr_session.h"
#include "../vr/camera_head.h"
#include "../vr/stereo.h"
#include "../vr/reflection_stereo.h"
#include "../common/config.h"
#include "../common/log.h"
#include "../vr/tune.h"
#include "../vr/ui_space.h"
#include "../vr/water_stereo.h"
#include "../vr/hud_capture.h"
#include "../vr/xr_runtime.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define MT_STR2(a) #a
#define MT_STR(a) MT_STR2(a)

namespace trlvr
{
    extern int g_sky_hits_this_count;          // vr_session.cpp

    std::vector<std::string> g_mathtest_log;
    void log(const char* fmt, ...)
    {
        char line[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        g_mathtest_log.push_back(line);
    }
    void log_set_activity(const char*) {}
    const wchar_t* exe_dir() { return g_scn.exe_dir; }

    // Synced from the scenario before each call (SessionApi::sync), so that
    // config() costs what the real one does: a reference.
    Config g_mathtest_config;
    const Config& config() { return g_mathtest_config; }
    void mathtest_sync_config()
    {
        Config& c = g_mathtest_config;
        c.head_tracking = g_scn.head_tracking;
        c.hmd_drives_camera = g_scn.hmd_drives_camera;
        c.hmd_drives_position = g_scn.hmd_drives_position;
        c.head_rotation = g_scn.head_rotation;
        c.head_position = g_scn.head_position;
        c.ui_passthrough = g_scn.ui_passthrough;
        c.pause_world_locked = g_scn.pause_world_locked;
        c.menu_world_locked = g_scn.menu_world_locked;
        c.world_scale = g_scn.world_scale;
        c.ui_scale = g_scn.ui_scale;
        c.hud_follow = g_scn.hud_follow;
        c.head_position_scale = g_scn.head_position_scale;
        c.head_rotation_scale = g_scn.head_rotation_scale;
        c.hud_offset_x = g_scn.hud_offset_x;
        c.hud_offset_y = g_scn.hud_offset_y;
    }
    void config_set_diorama_size(float, float, float, bool) {}
    void config_set_theatre_size(float, float, bool) {}

    // camera_head
    bool camera_first_person_active() { return g_scn.first_person; }
    bool camera_accurate_aim_active() { return g_scn.accurate_aim; }
    float camera_stereo_scale() { return g_scn.stereo_scale; }
    float camera_theatre_scale() { return g_scn.theatre_scale; }
    float camera_marker_depth() { return g_scn.marker_depth; }
    float camera_world_scale_factor() { return g_scn.world_scale_factor; }
    int camera_first_person_current_gpu_hand() { return g_scn.gpu_hand; }
    bool camera_cinematic_playing() { return false; }
    int camera_third_person_mode() { return 0; }
    bool camera_view_first_person() { return g_scn.first_person; }
    bool camera_hand_index_tip_head(bool, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        return false;
    }
    bool camera_frame_positions(float retail[3], float eye[3], float focus[3])
    {
        if (!g_scn.camera_ok)
            return false;
        memcpy(retail, g_scn.retail, 3 * sizeof(float));
        memcpy(eye, g_scn.eye, 3 * sizeof(float));
        memcpy(focus, g_scn.focus, 3 * sizeof(float));
        return true;
    }

    // ui_space / hud_capture, derived from the root screen as the real ones
    bool ui_drawing_interface() { return g_scn.interface || g_scn.screen_wipe; }
    bool ui_drawing_screen_wipe() { return g_scn.screen_wipe; }
    bool ui_drawing_world_marker() { return g_scn.world_marker; }
    bool ui_interface_world_locked()
    {
        return g_scn.world_locked_hud && !g_scn.screen_wipe;
    }
    bool ui_menu_active() { return g_scn.menu; }
    short ui_root_screen_id() { return g_scn.menu ? g_scn.root_screen : -1; }
    bool ui_loading_screen_active()
    {
        const short r = ui_root_screen_id();
        return r >= 2000 && r < 3000;
    }
    bool ui_mission_prep_active()
    {
        const short r = ui_root_screen_id();
        return r >= 3000 && r < 4000;
    }
    bool ui_pda_active() { return ui_root_screen_id() == 6; }
    bool ui_pause_menu_active() { return g_scn.menu && g_scn.pause; }
    bool hud_binocular_view_active() { return g_scn.binocular; }

    // tune
    float tune_world_scale()
    {
        return config().world_scale * camera_world_scale_factor();
    }
    float tune_ipd_scale() { return 1.0f; }
    float tune_ui_scale() { return config().ui_scale; }
    float tune_hud_follow() { return config().hud_follow; }
    float tune_move_scale() { return config().head_position_scale; }
    float tune_turn_scale() { return config().head_rotation_scale; }
    float tune_marker_distance() { return g_scn.marker_distance; }
    unsigned tune_ledge_visibility_marker() { return 0; }
    bool tune_grip_aim(float* p, float* y) { *p = *y = 0.0f; return false; }
    bool tune_set_grip_aim(float, float) { return true; }
    void tune_grip_hand(float pos[3], float rot[3])
    {
        for (int i = 0; i < 3; ++i)
            pos[i] = rot[i] = 0.0f;
    }
    void tune_aim_offset(float* p, float* y) { *p = *y = 0.0f; }
    void tune_board_offset(float out[3]) { out[0] = out[1] = out[2] = 0.0f; }
    void tune_set_board_offset(const float*) {}
    void tune_theatre_offset(float out[3]) { out[0] = out[1] = out[2] = 0.0f; }
    void tune_set_theatre_offset(const float*) {}
    bool tune_hand_correction(bool, float out[12])
    {
        for (int i = 0; i < 12; ++i)
            out[i] = 0.0f;
        return false;
    }
    bool tune_set_hand_correction(bool, const float*) { return true; }

    // runtime, stereo, water
    bool xr_instance_init() { return true; }
    void xr_instance_shutdown() {}
    const char* xr_last_error() { return ""; }
    const char* xr_system_name() { return "mathtest host"; }
    bool xr_recommended_size(unsigned* w, unsigned* h)
    {
        *w = 1920;
        *h = 1920;
        return true;
    }
    bool stereo_same_frame_active() { return true; }
    bool water_stereo_active() { return g_scn.water; }
    bool reflection_stereo_pass_active() { return false; }
}

namespace
{
    bool api_init() { return trlvr::vr_init(); }
    bool api_for_eye(const float* in, float* out, int eye)
    {
        return trlvr::vr_substitute_c0_for_eye(in, out, (trlvr::Eye)eye);
    }
#if !defined(MATHTEST_REFERENCE)
    bool api_stereo(const float* in, float* left, float* right)
    {
        return trlvr::vr_substitute_c0_stereo(in, left, right);
    }
    void api_set_shared(bool shared, bool cache)
    {
        trlvr::vr_set_stereo_shared_math(shared, cache);
    }
    void api_capture(bool on) { trlvr::vr_c0_capture_enable(on); }
    void api_cache_stats(unsigned* hits, unsigned* misses)
    {
        trlvr::vr_eye_cache_stats(hits, misses);
    }
#endif
    void api_set_eye_views(const float views[2][16])
    {
        trlvr::XrEyeView eyes[2]{};
        for (int i = 0; i < 2; ++i)
        {
            eyes[i].tan_left = views[i][0];
            eyes[i].tan_right = views[i][1];
            eyes[i].tan_top = views[i][2];
            eyes[i].tan_bottom = views[i][3];
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                    eyes[i].eye_to_head.m[row][col] = views[i][4 + row * 4 + col];
        }
        trlvr::vr_set_eye_views(eyes);
    }
    void api_set_head_pose(const float pose34[12], bool valid)
    {
        trlvr::TrackedPose poses[trlvr::kPoseCount]{};
        memcpy(&poses[trlvr::kPoseHmd].mDeviceToAbsoluteTracking.m[0][0],
               pose34, 12 * sizeof(float));
        poses[trlvr::kPoseHmd].bPoseIsValid = valid;
        poses[trlvr::kPoseHmd].bDeviceIsConnected = true;
        for (unsigned i = 1; i < trlvr::kPoseCount; ++i)
        {
            poses[i].mDeviceToAbsoluteTracking.m[0][0] = 1.0f;
            poses[i].mDeviceToAbsoluteTracking.m[1][1] = 1.0f;
            poses[i].mDeviceToAbsoluteTracking.m[2][2] = 1.0f;
        }
        trlvr::vr_set_render_poses(poses, trlvr::kPoseCount);
    }
    void api_frame() { trlvr::vr_update_pose(); }
    void api_set_scene_size(unsigned w, unsigned h)
    {
        trlvr::vr_set_scene_size(w, h);
    }
    void api_set_rt_size(unsigned w, unsigned h)
    {
        trlvr::vr_set_render_target_size(w, h);
    }
    int api_sky_hits() { return trlvr::g_sky_hits_this_count; }
    void api_recenter() { trlvr::vr_recenter(); }
    void api_note_main_camera_head()
    {
        trlvr::vr_note_main_camera_head_applied();
    }
}

extern "C" const SessionApi* MT_CAT(mathtest_session_api_, MATHTEST_COPY)()
{
    static const SessionApi api = {
        MT_STR(MATHTEST_COPY), api_init, api_for_eye,
#if defined(MATHTEST_REFERENCE)
        nullptr, nullptr, nullptr, nullptr,
#else
        api_stereo, api_set_shared, api_capture, api_cache_stats,
#endif
        api_set_eye_views, api_set_head_pose, api_frame, api_set_scene_size,
        api_set_rt_size, api_sky_hits, trlvr::mathtest_sync_config,
        api_recenter, api_note_main_camera_head,
        &trlvr::g_mathtest_log,
    };
    return &api;
}

#else  // !MATHTEST_SESSION_STUBS: the test program itself

#include "../vr/vr_math.h"
#include "../vr/vr_gesture.h"
#if defined(MATHTEST_SESSION)
#include "../vr/vr_session.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace trlvr;

namespace
{
    int g_failures = 0;

    void check(bool ok, const char* what)
    {
        printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            ++g_failures;
    }

    bool close(float a, float b, float eps = 1e-4f)
    {
        return fabsf(a - b) <= eps;
    }

    // For the god-mode test files (*_tests.inc): quiet, only failures print.
    int g_quiet_checks = 0;

    void quiet_check(bool ok, const char* what, const char* file, int line)
    {
        ++g_quiet_checks;
        if (!ok)
        {
            ++g_failures;
            printf("  [FAIL] %s  (%s:%d)\n", what, file, line);
        }
    }
#define CHECK(cond) quiet_check((cond), #cond, __FILE__, __LINE__)

    bool near(float a, float b, float eps = 1e-4f)
    {
        return fabsf(a - b) <= eps * (1.0f + fabsf(b));
    }

    void print_regs(const char* label, const float* r)
    {
        printf("    %s\n", label);
        for (int i = 0; i < 4; ++i)
            printf("      c%d = [ %12.5f %12.5f %12.5f %12.5f ]\n",
                   i, r[i * 4 + 0], r[i * 4 + 1], r[i * 4 + 2], r[i * 4 + 3]);
    }

    // The conversion as it was before the Y-flip was added: S = diag(1,1,-1).
    // Kept only so the tests can state what changed. In the headset this gave
    // correct yaw with pitch and roll both inverted.
    Mat4 legacy_view_from_pose(const float* pose34)
    {
        float R[3][3], t[3];
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
                R[row][col] = pose34[row * 4 + col];
            t[row] = pose34[row * 4 + 3];
        }
        R[0][2] = -R[0][2];
        R[1][2] = -R[1][2];
        R[2][0] = -R[2][0];
        R[2][1] = -R[2][1];
        t[2] = -t[2];

        Mat4 v = Mat4::identity();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                v.m[i][j] = R[i][j];
        for (int j = 0; j < 3; ++j)
        {
            float acc = 0.0f;
            for (int i = 0; i < 3; ++i)
                acc += t[i] * R[i][j];
            v.m[3][j] = -acc;
        }
        return v;
    }

    // Captured from trlvr.log during normal play.
    const float kGameRegs[16] = {
        2.0000f,  0.0000f,  0.0000f,   0.0000f,   // c0
        0.0000f, -2.2857f,  0.0000f,   0.0000f,   // c1
        0.0000f,  0.0000f,  1.0001f, -16.0020f,   // c2
        0.0000f,  0.0000f,  1.0000f,   0.0000f,   // c3
    };
}

// ---------------------------------------------------------------------------
// M4: the shared world-locked prefix (vr_math) against the one-eye mapping.
// ---------------------------------------------------------------------------
namespace
{
    unsigned g_rng = 0x9E3779B9u;
    unsigned rng_next()
    {
        g_rng ^= g_rng << 13;
        g_rng ^= g_rng >> 17;
        g_rng ^= g_rng << 5;
        return g_rng;
    }
    float rng_unit() { return (rng_next() >> 8) * (1.0f / 16777216.0f); }
    float rng_range(float lo, float hi) { return lo + (hi - lo) * rng_unit(); }
    bool rng_chance(float p) { return rng_unit() < p; }

    bool same_bits(const Mat4& a, const Mat4& b)
    {
        return memcmp(&a, &b, sizeof(Mat4)) == 0;
    }

    Mat4 random_rotation(float yaw, float pitch, float roll)
    {
        Mat4 y = yaw_rotation(yaw);
        Mat4 p = Mat4::identity();
        p.m[1][1] = cosf(pitch); p.m[1][2] = sinf(pitch);
        p.m[2][1] = -sinf(pitch); p.m[2][2] = cosf(pitch);
        Mat4 r = Mat4::identity();
        r.m[0][0] = cosf(roll); r.m[0][1] = sinf(roll);
        r.m[1][0] = -sinf(roll); r.m[1][1] = cosf(roll);
        return r * p * y;
    }

    void test_world_locked_prefix()
    {
        printf("\nshared world-locked prefix (M4)\n");
        int exact = 0, total = 0;
        for (int n = 0; n < 400; ++n)
        {
            const float nz = rng_range(0.5f, 12.0f);
            const Mat4 source = projection_from_tangents(
                -rng_range(0.8f, 1.3f), rng_range(0.8f, 1.3f),
                -rng_range(0.8f, 1.3f), rng_range(0.8f, 1.3f), nz,
                rng_range(1000.0f, 50000.0f), rng_chance(0.7f));
            Mat4 reference = projection_from_tangents(
                -1.2f, 1.1f, -1.15f, 1.18f, nz, 20000.0f, true);
            if (n % 50 == 49)
                reference = Mat4{};          // singular: the frustum fallback
            const Mat4 rotation = random_rotation(rng_range(-3.f, 3.f),
                rng_range(-1.f, 1.f), rng_range(-0.3f, 0.3f));
            const float scale = rng_range(0.4f, 1.2f);
            const float bx = rng_chance(0.5f) ? rng_range(-0.3f, 0.3f) : 0.0f;
            const float by = rng_chance(0.5f) ? rng_range(-0.3f, 0.3f) : 0.0f;
            WorldLockedPrefix prefix;
            for (int eye = 0; eye < 2; ++eye)
            {
                const Mat4 target = projection_from_tangents(
                    eye ? -1.05f : -1.40f, eye ? 1.40f : 1.05f,
                    -1.2f, 1.15f, nz > 1.0f ? 1.0f : nz, 20000.0f, true);
                const Mat4 a = reproject_ndc_to_world_locked(
                    source, reference, target, rotation, scale, bx, by);
                const Mat4 b = reproject_ndc_to_world_locked_eye(
                    &prefix, source, reference, target, rotation, scale,
                    bx, by);
                ++total;
                if (same_bits(a, b))
                    ++exact;
            }
        }
        char what[128];
        snprintf(what, sizeof(what), "shared prefix bit-identical to "
                 "reproject_ndc_to_world_locked (%d/%d eyes)", exact, total);
        check(exact == total, what);
    }
}

namespace
{
    void test_exact_fast_products()
    {
        printf("\nexact identity shortcut (M4)\n");
        int exact = 0, total = 0;
        const float specials[] = { -0.0f, 0.0f, 1e-30f, -1e30f, INFINITY,
                                   -INFINITY, NAN };
        for (int n = 0; n < 3000; ++n)
        {
            Mat4 a = random_rotation(rng_range(-3.f, 3.f), rng_range(-1.f, 1.f),
                                     rng_range(-1.f, 1.f));
            a.m[3][0] = rng_range(-5000.f, 5000.f);
            a.m[3][2] = rng_range(-5000.f, 5000.f);
            if (n % 3 == 0)
                a.m[0][3] = rng_range(-2.f, 2.f);      // projective
            const int special_count = n % 5 == 0 ? 1 + (int)(rng_next() % 4)
                                                 : 0;
            for (int k = 0; k < special_count; ++k)
                (&a.m[0][0])[rng_next() % 16] =
                    specials[rng_next() % (n % 10 == 0 ? 7 : 4)];
            const Mat4 id = Mat4::identity();
            const Mat4 product_right = a * id;
            const Mat4 product_left = id * a;
            Mat4 shortcut;
            const bool finite = times_identity(a, &shortcut);
            bool all_finite = true;
            for (int i = 0; i < 16; ++i)
                all_finite = all_finite && std::isfinite((&a.m[0][0])[i]);
            total += 2;
            if (finite == all_finite &&
                (!finite || memcmp(&shortcut, &product_right, sizeof(Mat4)) == 0))
                ++exact;
            if (finite == all_finite &&
                (!finite || memcmp(&shortcut, &product_left, sizeof(Mat4)) == 0))
                ++exact;
        }
        Mat4 signed_zero = Mat4::identity();
        signed_zero.m[0][1] = -0.0f;
        check(is_identity(Mat4::identity()) && !is_identity(signed_zero),
              "is_identity is exact (a -0 entry is not the identity)");
        char what[160];
        snprintf(what, sizeof(what), "times_identity bit-identical to "
                 "m * I and I * m (%d/%d, signed zeros; inf and NaN refused)",
                 exact, total);
        check(exact == total, what);
    }
}

#if defined(MATHTEST_SESSION)
// ---------------------------------------------------------------------------
// M4: vr_substitute_c0_stereo against vr_substitute_c0_for_eye, bit for bit.
// ---------------------------------------------------------------------------
#include <sys/mman.h>
#include <chrono>
#include <algorithm>

MathtestScenario g_scn;

DWORD GetTickCount() { return (DWORD)g_scn.tick; }
ULONGLONG GetTickCount64() { return g_scn.tick; }
UINT GetPrivateProfileIntW(const wchar_t*, const wchar_t* key, int fallback,
                           const wchar_t*)
{
    if (!wcscmp(key, L"stereo_shared_math"))
        return g_scn.shared_math_ini;
    if (!wcscmp(key, L"vr_capture_c0"))
        return g_scn.capture_ini;
    return fallback;
}

extern "C" const SessionApi* mathtest_session_api_ref();
extern "C" const SessionApi* mathtest_session_api_s();
extern "C" const SessionApi* mathtest_session_api_sc();
extern "C" const SessionApi* mathtest_session_api_dn();
extern "C" const SessionApi* mathtest_session_api_dc();
extern "C" const SessionApi* mathtest_session_api_lg();

namespace
{
    // The game addresses the session reads (movie handle, main camera),
    // mapped where the game has them.
    const uintptr_t kGameBase = 0x01000000, kGameSize = 0x00200000;
    const uintptr_t kMovieHandle = 0x0100257C;
    const uintptr_t kMainCameraWc = 0x010FC660 + 0x120;

    bool map_game_memory()
    {
        void* p = mmap(reinterpret_cast<void*>(kGameBase), kGameSize,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                       -1, 0);
        return p == reinterpret_cast<void*>(kGameBase);
    }
    void set_movie(bool on)
    {
        const uint64_t v = on ? 0x00ABCDEF : 0;
        memcpy(reinterpret_cast<void*>(kMovieHandle), &v, sizeof(v));
    }
    void set_main_wc(const Mat4& v)
    {
        memcpy(reinterpret_cast<void*>(kMainCameraWc), &v, sizeof(v));
    }

    enum Copy { CRef, CS, CSc, CDn, CDc, CLg, CopyCount };
    const SessionApi* g_api[CopyCount];

    enum Category
    {
        CatPure, CatWvp, CatSky, CatHud, CatMenu, CatMarker, CatPda,
        CatPrep, CatFpHands, CatBoardHands, CatPauseHands, CatBinocular,
        CatAccurateAim, CatPaused, CatMovie, CatLoading, CatWipe, CatOrtho,
        CatNoScene, CatHeadView, CategoryCount
    };
    const char* const kCategoryNames[CategoryCount] = {
        "pure projection", "WVP", "sky", "HUD", "menu", "marker", "PDA",
        "Mission Prep", "first-person hands", "board hands", "paused hands",
        "binocular", "accurate aim", "paused scene", "movie", "loading",
        "screen wipe", "ortho/2D", "not the scene", "head view composed"
    };

    struct SuiteStats
    {
        int pairs = 0, centres = 0, mismatches = 0, substituted = 0;
        int sky_asymmetric = 0, sky_pairs = 0;
        int per_category[CategoryCount] = {};
    } g_suite;

    void all_frame()
    {
        for (const SessionApi* a : g_api)
        {
            a->sync();
            a->frame();
        }
    }
    void all_eye_views(const float v[2][16])
    {
        for (const SessionApi* a : g_api) a->set_eye_views(v);
    }
    void all_head_pose(const float p[12], bool valid)
    {
        for (const SessionApi* a : g_api)
        {
            a->sync();
            a->set_head_pose(p, valid);
        }
    }

    void report_mismatch(const char* copy, const char* what, int category,
                         const float* c0)
    {
        ++g_suite.mismatches;
        if (g_suite.mismatches <= 12)
        {
            printf("  [FAIL] %s: %s differs from the reference (%s)\n",
                   copy, what, kCategoryNames[category]);
            print_regs("game c0", c0);
        }
    }

    // One draw: optionally the upload-time centre call, then both eyes --
    // two single calls (ref, s, sc) or one dual call (dn, dc, lg).
    void run_draw(const float* c0, int category, bool centre)
    {
        float out[CopyCount][3][16];
        bool ok[CopyCount][3];
        for (int c = 0; c < CopyCount; ++c)
        {
            g_api[c]->sync();
            for (int e = 0; e < 3; ++e)
                for (int i = 0; i < 16; ++i)
                    out[c][e][i] = -7777.0f - (float)i;   // untouched marker
            ok[c][2] = centre ? g_api[c]->for_eye(c0, out[c][2], 2) : false;
            if (c == CRef || c == CS || c == CSc)
            {
                const int before = g_api[c]->sky_hits();
                ok[c][0] = g_api[c]->for_eye(c0, out[c][0], 0);
                const int middle = g_api[c]->sky_hits();
                ok[c][1] = g_api[c]->for_eye(c0, out[c][1], 1);
                const int after = g_api[c]->sky_hits();
                if (c == CRef && category == CatSky)
                {
                    ++g_suite.sky_pairs;
                    if ((middle > before) != (after > middle))
                        ++g_suite.sky_asymmetric;
                }
            }
            else
            {
                ok[c][0] = ok[c][1] = g_api[c]->stereo(c0, out[c][0], out[c][1]);
            }
        }
        ++g_suite.pairs;
        ++g_suite.per_category[category];
        if (centre)
            ++g_suite.centres;
        if (ok[CRef][0])
            ++g_suite.substituted;
        if (ok[CRef][0] != ok[CRef][1])
            report_mismatch("ref", "left/right success", category, c0);
        for (int c = 1; c < CopyCount; ++c)
        {
            static const char* const what[3] = { "left eye", "right eye",
                                                 "centre eye" };
            for (int e = 0; e < 3; ++e)
            {
                if (e == 2 && !centre)
                    continue;
                if (ok[c][e] != ok[CRef][e] ||
                    memcmp(out[c][e], out[CRef][e], sizeof(out[c][e])) != 0)
                {
                    report_mismatch(g_api[c]->name, what[e], category, c0);
                    if (g_suite.mismatches <= 2)
                    {
                        printf("    ok %d / reference %d; scenario: menu %d "
                               "interface %d wipe %d marker %d follow %.2f "
                               "binocular %d first person %d\n", ok[c][e],
                               ok[CRef][e], g_scn.menu, g_scn.interface,
                               g_scn.screen_wipe, g_scn.world_marker,
                               g_scn.hud_follow, g_scn.binocular,
                               g_scn.first_person);
                        print_regs("this copy", out[c][e]);
                        print_regs("reference", out[CRef][e]);
                    }
                }
            }
        }
    }

    // ---- synthetic game ----------------------------------------------------
    GameProjection make_projection(float sx, float near_z, float far_z)
    {
        GameProjection gp{};
        gp.sx = sx;
        gp.sy = -sx * (8.0f / 7.0f);
        gp.ox = 0.0f;
        gp.oy = 0.0f;
        gp.q = far_z / (far_z - near_z);
        gp.near_z = near_z;
        gp.far_z = far_z;
        gp.valid = true;
        return gp;
    }
    void regs_of(const Mat4& m, float* out) { registers_from_matrix(m, out); }

    struct World
    {
        GameProjection proj;
        float cam_pos[3];
        float cam_yaw, cam_pitch;
        float focus_offset[3];
        float sky_offset[3];
        float decoy_offset[3];      // close camera-following draw (rejected)
        float drift_offset[3];      // a sky whose offset creeps (kept only
                                    // through last frame's hits, < 40 calls)
        float head_yaw, head_pitch, head_roll, head_pos[3];
        float ipd;
        float cant;
        int frame;
        float base_sx, base_near, base_far;
    } g_world;

    Mat4 camera_view()
    {
        // camera-to-world, then its inverse (row vectors).
        Mat4 pose = random_rotation(g_world.cam_yaw, g_world.cam_pitch, 0.0f);
        pose.m[3][0] = g_world.cam_pos[0];
        pose.m[3][1] = g_world.cam_pos[1];
        pose.m[3][2] = g_world.cam_pos[2];
        return rigid_inverse(pose);
    }

    void eye_views_now(float v[2][16])
    {
        for (int i = 0; i < 2; ++i)
        {
            const float sign = i == 0 ? -1.0f : 1.0f;
            memset(v[i], 0, sizeof(v[i]));
            // Frame-like asymmetric frusta, mirrored.
            v[i][0] = i == 0 ? -1.3970f : -1.0510f;
            v[i][1] = i == 0 ?  1.0510f :  1.3970f;
            v[i][2] = -1.2120f;
            v[i][3] = 1.1590f + 0.001f * (float)(g_world.frame % 7);
            const float c = cosf(g_world.cant * sign);
            const float s = sinf(g_world.cant * sign);
            // rotation about y (row-major 3x3 in a 3x4)
            v[i][4 + 0] = c;  v[i][4 + 2] = s;
            v[i][4 + 5] = 1.0f;
            v[i][4 + 8] = -s; v[i][4 + 10] = c;
            v[i][4 + 3] = sign * g_world.ipd * 0.5f;
            v[i][4 + 7] = 0.0005f * sign;
            v[i][4 + 11] = -0.0012f;
        }
    }

    void head_pose_now(float p[12])
    {
        const Mat4 r = random_rotation(g_world.head_yaw, g_world.head_pitch,
                                       g_world.head_roll);
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
                p[row * 4 + col] = r.m[col][row];
            p[row * 4 + 3] = g_world.head_pos[row];
        }
    }

    // A world-space draw at `where` (world units): World * View * P.
    void wvp_regs(const float where[3], float yaw, float scale,
                  const GameProjection& gp, float* out)
    {
        Mat4 world = yaw_rotation(yaw);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                world.m[i][j] *= scale;
        world.m[3][0] = where[0];
        world.m[3][1] = where[1];
        world.m[3][2] = where[2];
        regs_of(world * camera_view() * projection_from_game(gp), out);
    }

    void reset_state_flags()
    {
        g_scn.interface = g_scn.screen_wipe = g_scn.world_marker = false;
        g_scn.world_locked_hud = false;
        g_scn.gpu_hand = -1;
        g_scn.accurate_aim = false;
    }

    enum FrameMode
    {
        ModeThirdPerson, ModeFirstPerson, ModeBoard, ModeMainMenu, ModePause,
        ModePda, ModePrep, ModeMovie, ModeLoading, ModeBinocular, ModeAim,
        ModeCount
    };

    void synth_frame(int f)
    {
        g_world.frame = f;
        // Camera and focus move every frame (sky learning needs both).
        g_world.cam_pos[0] += rng_range(12.0f, 40.0f);
        g_world.cam_pos[2] += rng_range(-25.0f, 25.0f);
        g_world.cam_yaw += rng_range(-0.05f, 0.05f);
        g_world.focus_offset[0] = rng_range(-200.0f, 200.0f);
        g_world.focus_offset[2] = rng_range(300.0f, 700.0f);
        g_world.head_yaw += rng_range(-0.08f, 0.08f);
        g_world.head_pitch = rng_range(-0.5f, 0.5f);
        g_world.head_roll = rng_range(-0.1f, 0.1f);
        for (int k = 0; k < 3; ++k)
            g_world.head_pos[k] = rng_range(-0.2f, 0.2f);
        if (f % 23 == 11)
            g_world.ipd = rng_range(0.058f, 0.070f);
        if (f % 31 == 17)
            g_world.cant = rng_chance(0.5f) ? 0.0f : rng_range(0.0f, 0.17f);
        for (int k = 0; k < 3; ++k)
        {
            g_scn.retail[k] = g_world.cam_pos[k];
            g_scn.eye[k] = g_world.cam_pos[k] + (k == 1 ? -12.0f : 3.0f);
            g_scn.focus[k] = g_world.cam_pos[k] + g_world.focus_offset[k];
        }
        g_scn.tick += 11;

        all_frame();
        float views[2][16];
        eye_views_now(views);
        all_eye_views(views);
        float pose[12];
        head_pose_now(pose);
        all_head_pose(pose, !rng_chance(0.03f));
        if (rng_chance(0.15f))
            for (const SessionApi* a : g_api) a->note_main_camera_head();
        if (f % 41 == 40)
            for (const SessionApi* a : g_api) a->recenter();
        set_main_wc(camera_view());

        const FrameMode mode = (FrameMode)(f < 6 ? ModeThirdPerson
                                           : (int)(rng_next() % ModeCount));
        g_scn.first_person = mode == ModeFirstPerson;
        g_scn.world_scale_factor = mode == ModeBoard ? rng_range(4.0f, 20.0f)
                                                     : 1.0f;
        g_scn.menu = mode == ModeMainMenu || mode == ModePause ||
                     mode == ModePda || mode == ModePrep ||
                     mode == ModeLoading;
        g_scn.pause = mode == ModePause;
        g_scn.root_screen = mode == ModePda ? 6 : mode == ModePrep ? 3001
                          : mode == ModeLoading ? 2003 : mode == ModePause
                          ? 13 : 1;
        g_scn.binocular = mode == ModeBinocular;
        g_scn.hmd_drives_camera = !rng_chance(0.2f);
        g_scn.pause_world_locked = !rng_chance(0.3f);
        g_scn.hud_follow = rng_chance(0.3f) ? 1.0f : 0.85f;
        g_scn.hud_offset_x = rng_chance(0.3f) ? 0.05f : 0.0f;
        g_scn.marker_depth = rng_range(200.0f, 900.0f);
        g_scn.stereo_scale = rng_chance(0.1f) ? 0.5f : 1.0f;
        g_scn.camera_ok = !rng_chance(0.03f);
        set_movie(mode == ModeMovie);

        // The game's projection for this frame (first person: a wide one,
        // near can be negative while hanging).
        // A game keeps its projection for a while (sky learning needs the
        // cached one to match the world draws); first person and the
        // binoculars have their own.
        if (f % 25 == 0)
        {
            g_world.base_sx = rng_range(1.10f, 1.30f);
            g_world.base_near = rng_range(8.0f, 20.0f);
            g_world.base_far = rng_range(20000.0f, 60000.0f);
        }
        const float fov_sx = mode == ModeBinocular ? 4.6f : g_world.base_sx;
        const GameProjection gp = make_projection(
            fov_sx, mode == ModeFirstPerson ? rng_range(-3.0f, 6.0f)
                                            : g_world.base_near,
            g_world.base_far);
        if (f > 8)
            g_world.drift_offset[0] += 5.0f;
        float sky_where[3], decoy_where[3], drift_where[3];
        for (int k = 0; k < 3; ++k)
        {
            sky_where[k] = g_scn.retail[k] + g_world.sky_offset[k];
            decoy_where[k] = g_scn.retail[k] + g_world.decoy_offset[k];
            drift_where[k] = g_scn.retail[k] + g_world.drift_offset[k];
        }

        const int draws = 18 + (int)(rng_next() % 30);
        const int sky_at = (int)(rng_next() % 24);
        const int drift_at = f < 9 ? 1 : 6 + (int)(rng_next() % 20);
        for (int d = 0; d < draws; ++d)
        {
            reset_state_flags();
            if (rng_chance(0.04f))
            {
                g_scn.world_scale += rng_chance(0.5f) ? 1.0f : -1.0f;
            }
            if (rng_chance(0.02f))
            {
                // A runtime frustum change mid-frame (IPD wheel).
                g_world.ipd += 0.0002f;
                eye_views_now(views);
                all_eye_views(views);
            }
            if (rng_chance(0.01f))
            {
                head_pose_now(pose);
                pose[3] += 0.01f;
                all_head_pose(pose, true);
            }
            const bool centre = rng_chance(0.6f);
            float c0[16];
            const int pick = (int)(rng_next() % 100);
            int category = CatWvp;
            if (d == sky_at || d == sky_at + 1 || d == sky_at + 7)
            {
                wvp_regs(sky_where, 0.0f, 1.0f, gp, c0);
                category = CatSky;
            }
            else if (d == sky_at + 3 && rng_chance(0.5f))
            {
                wvp_regs(decoy_where, 0.3f, 1.0f, gp, c0);
                category = CatSky;
            }
            else if (d == drift_at || d == drift_at + 2)
            {
                wvp_regs(drift_where, 0.0f, 1.0f, gp, c0);
                category = CatSky;
            }
            else if (pick < 22)
            {
                regs_of(projection_from_game(gp), c0);
                category = CatPure;
                if (mode == ModeFirstPerson && rng_chance(0.5f))
                {
                    g_scn.gpu_hand = (int)(rng_next() % 2);
                    category = CatFpHands;
                }
                else if (mode == ModeBoard && rng_chance(0.5f))
                {
                    g_scn.gpu_hand = 2 + (int)(rng_next() % 2);
                    category = CatBoardHands;
                }
                else if (mode == ModePause && rng_chance(0.4f))
                {
                    g_scn.gpu_hand = (int)(rng_next() % 4);
                    category = CatPauseHands;
                }
                else if (mode == ModeAim)
                {
                    g_scn.accurate_aim = true;
                    category = CatAccurateAim;
                }
                else if (mode == ModeBinocular)
                    category = CatBinocular;
            }
            else if (pick < 70)
            {
                const float where[3] = {
                    g_scn.retail[0] + rng_range(-3000.0f, 3000.0f),
                    g_scn.retail[1] + rng_range(-400.0f, 400.0f),
                    g_scn.retail[2] + rng_range(200.0f, 6000.0f) };
                GameProjection p = gp;
                if (mode == ModeAim && rng_chance(0.6f))
                {
                    // The accurate-aim camera's own depth range.
                    p = make_projection(gp.sx, rng_range(2.0f, 6.0f), gp.far_z);
                    g_scn.accurate_aim = true;
                }
                wvp_regs(where, rng_range(-3.0f, 3.0f), rng_range(0.5f, 2.0f),
                         p, c0);
                category = g_scn.accurate_aim ? CatAccurateAim
                         : mode == ModePause ? CatPaused
                         : mode == ModeBinocular ? CatBinocular
                         : mode == ModePda ? CatPda
                         : mode == ModePrep ? CatPrep : CatWvp;
            }
            else if (pick < 90)
            {
                // Interface: the game's HUD projection (pure), near 1..10.
                regs_of(projection_from_game(make_projection(
                    rng_range(0.9f, 1.4f), rng_range(0.5f, 10.0f), 5000.0f)),
                    c0);
                g_scn.interface = true;
                category = CatHud;
                if (g_scn.menu)
                {
                    g_scn.world_locked_hud = rng_chance(0.8f);
                    category = mode == ModePda ? CatPda
                             : mode == ModePrep ? CatPrep
                             : mode == ModeLoading ? CatLoading : CatMenu;
                }
                else if (mode == ModeMovie)
                    category = CatMovie;
                else if (rng_chance(0.3f))
                {
                    g_scn.world_marker = true;
                    category = CatMarker;
                }
                else if (rng_chance(0.15f))
                {
                    g_scn.interface = false;
                    g_scn.screen_wipe = true;
                    category = CatWipe;
                }
                else if (mode == ModeBinocular)
                    category = CatBinocular;
            }
            else if (pick < 96)
            {
                // A 2D pass: w constant (orthographic), scene or interface.
                float ortho[16] = {
                    0.0015f, 0.0f, 0.0f, -1.0f,
                    0.0f, -0.0026f, 0.0f, 1.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f };
                memcpy(c0, ortho, sizeof(ortho));
                g_scn.interface = rng_chance(0.5f);
                category = g_scn.menu && mode == ModePrep ? CatPrep : CatOrtho;
            }
            else
            {
                // Not the scene target (a shadow map).
                for (const SessionApi* a : g_api) a->set_rt_size(512, 512);
                regs_of(projection_from_game(gp), c0);
                run_draw(c0, CatNoScene, centre);
                for (const SessionApi* a : g_api) a->set_rt_size(3840, 1920);
                continue;
            }
            if (!g_scn.hmd_drives_camera && category == CatWvp)
                category = CatHeadView;
            run_draw(c0, category, centre);
        }
    }

    // The sky test's windows, deterministically: an offset learned over two
    // frames, then moved 5 units, so only last frame's hits (draw calls
    // below 40) keep it -- and placed so that the left eye is call 39 and
    // the right eye call 40. Two single calls pin only the left eye there;
    // the dual call must do exactly the same.
    void sky_edge_frames()
    {
        reset_state_flags();
        g_scn.menu = g_scn.pause = g_scn.binocular = false;
        g_scn.first_person = false;
        g_scn.world_scale_factor = 1.0f;
        g_scn.camera_ok = true;
        set_movie(false);
        const GameProjection gp = make_projection(g_world.base_sx,
                                                  g_world.base_near,
                                                  g_world.base_far);
        float offset[3] = { 2100.0f, -300.0f, 1900.0f };
        for (int f = 0; f < 3; ++f)
        {
            g_world.cam_pos[0] += 30.0f;
            g_world.focus_offset[0] = f * 60.0f;
            for (int k = 0; k < 3; ++k)
            {
                g_scn.retail[k] = g_world.cam_pos[k];
                g_scn.eye[k] = g_world.cam_pos[k];
                g_scn.focus[k] = g_world.cam_pos[k] + g_world.focus_offset[k];
            }
            all_frame();
            set_main_wc(camera_view());
            float c0[16];
            regs_of(projection_from_game(gp), c0);   // refresh the cached P
            run_draw(c0, CatPure, true);
            if (f == 2)
            {
                offset[0] += 5.0f;
                // 13 world draws with their upload call: calls 0..38.
                for (int d = 0; d < 13; ++d)
                {
                    const float where[3] = { g_scn.retail[0] + 100.0f * d,
                                             g_scn.retail[1],
                                             g_scn.retail[2] + 900.0f };
                    wvp_regs(where, 0.2f * d, 1.0f, gp, c0);
                    run_draw(c0, CatWvp, true);
                }
            }
            float where[3];
            for (int k = 0; k < 3; ++k)
                where[k] = g_scn.retail[k] + offset[k];
            wvp_regs(where, 0.0f, 1.0f, gp, c0);
            run_draw(c0, CatSky, false);              // calls 39 and 40
        }
    }

    // ---- replay of c0_capture.bin -----------------------------------------
    int replay_capture(const char* path, int* frames_out)
    {
        FILE* f = fopen(path, "rb");
        if (!f)
            return -1;
        trlvr::C0CaptureRecord rec;
        int records = 0, frames = 0;
        unsigned last_frame = ~0u;
        float last_pose[12] = {}, last_views[2][16] = {};
        bool have_views = false;
        while (fread(&rec, sizeof(rec), 1, f) == 1)
        {
            if (memcmp(rec.magic, "C0R1", 4) != 0)
                break;
            ++records;
            if (rec.eye == 1)
                continue;                    // the right half of a legacy pair
            if (rec.frame != last_frame)
            {
                last_frame = rec.frame;
                ++frames;
                all_frame();
            }
            const unsigned fl = rec.flags;
            g_scn.ui_passthrough = (fl & trlvr::C0CapUiPassthrough) != 0;
            g_scn.interface = (fl & trlvr::C0CapInterface) != 0;
            g_scn.screen_wipe = (fl & trlvr::C0CapScreenWipe) != 0;
            set_movie((fl & trlvr::C0CapMovie) != 0);
            g_scn.menu = (fl & trlvr::C0CapMenu) != 0;
            g_scn.pause = (fl & trlvr::C0CapPause) != 0;
            g_scn.root_screen = (short)rec.root_screen;
            g_scn.world_locked_hud = (fl & trlvr::C0CapWorldLocked) != 0;
            g_scn.world_marker = (fl & trlvr::C0CapMarker) != 0;
            g_scn.first_person = (fl & trlvr::C0CapFirstPerson) != 0;
            g_scn.accurate_aim = (fl & trlvr::C0CapAccurateAim) != 0;
            g_scn.binocular = (fl & trlvr::C0CapBinocular) != 0;
            g_scn.hmd_drives_camera = (fl & trlvr::C0CapHmdDrivesCamera) != 0;
            g_scn.pause_world_locked = (fl & trlvr::C0CapPauseWorldLocked) != 0;
            g_scn.menu_world_locked = (fl & trlvr::C0CapMenuWorldLocked) != 0;
            g_scn.head_tracking = (fl & trlvr::C0CapHeadTracking) != 0;
            g_scn.camera_ok = (fl & trlvr::C0CapCameraOk) != 0;
            g_scn.gpu_hand = rec.gpu_hand;
            memcpy(g_scn.retail, &rec.camera[0], sizeof(g_scn.retail));
            memcpy(g_scn.eye, &rec.camera[3], sizeof(g_scn.eye));
            memcpy(g_scn.focus, &rec.camera[6], sizeof(g_scn.focus));
            g_scn.ui_scale = rec.scalars[trlvr::C0CapUiScale];
            g_scn.hud_follow = rec.scalars[trlvr::C0CapHudFollow];
            g_scn.world_scale = rec.scalars[trlvr::C0CapWorldScale];
            g_scn.world_scale_factor = rec.scalars[trlvr::C0CapWorldScaleFactor];
            g_scn.stereo_scale = rec.scalars[trlvr::C0CapStereoScale];
            g_scn.theatre_scale = rec.scalars[trlvr::C0CapTheatreScale];
            g_scn.marker_depth = rec.scalars[trlvr::C0CapMarkerDepth];
            g_scn.marker_distance = rec.scalars[trlvr::C0CapMarkerDistance];
            g_scn.hud_offset_x = rec.scalars[trlvr::C0CapHudOffsetX];
            g_scn.hud_offset_y = rec.scalars[trlvr::C0CapHudOffsetY];
            g_scn.head_rotation_scale = rec.scalars[trlvr::C0CapTurnScale];
            Mat4 wc;
            memcpy(&wc, rec.main_wc, sizeof(wc));
            set_main_wc(wc);
            if (!have_views || memcmp(last_views, rec.eye_views,
                                      sizeof(last_views)) != 0)
            {
                memcpy(last_views, rec.eye_views, sizeof(last_views));
                have_views = true;
                all_eye_views(last_views);
            }
            if (memcmp(last_pose, rec.hmd_pose, sizeof(last_pose)) != 0)
            {
                memcpy(last_pose, rec.hmd_pose, sizeof(last_pose));
                all_head_pose(last_pose,
                              (fl & trlvr::C0CapPoseValid) != 0);
            }
            const int category =
                g_scn.screen_wipe ? CatWipe
                : (fl & trlvr::C0CapMovie) ? CatMovie
                : g_scn.world_marker && g_scn.interface ? CatMarker
                : g_scn.interface ? (g_scn.menu ? CatMenu : CatHud)
                : rec.root_screen == 6 && g_scn.menu ? CatPda
                : rec.root_screen >= 3000 && rec.root_screen < 4000 &&
                  g_scn.menu ? CatPrep
                : rec.gpu_hand >= 0 ? (g_scn.first_person ? CatFpHands
                                                          : CatBoardHands)
                : projection_from_registers(rec.c0).valid ? CatPure : CatWvp;
            run_draw(rec.c0, category, rec.eye == 2);
        }
        fclose(f);
        if (frames_out)
            *frames_out = frames;
        return records;
    }

    // ---- timing ------------------------------------------------------------
    struct BenchDraw { float c0[16]; bool interface; bool sky; };

    double bench(Copy copy, bool dual, const std::vector<BenchDraw>& draws,
                 int frames)
    {
        const SessionApi* a = g_api[copy];
        a->sync();
        float l[16], r[16];
        volatile float sink = 0.0f;
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < frames; ++f)
        {
            a->frame();
            for (const BenchDraw& d : draws)
            {
                g_scn.interface = d.interface;
                if (dual)
                    a->stereo(d.c0, l, r);
                else
                {
                    a->for_eye(d.c0, l, 0);
                    a->for_eye(d.c0, r, 1);
                }
                sink = sink + l[0] + r[0];
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        g_scn.interface = false;
        return std::chrono::duration<double, std::nano>(t1 - t0).count() /
               (double)(frames * (int)draws.size());
    }

    void run_session_suite()
    {
        printf("\nper-eye substitution: dual-eye vs single calls (M4)\n");
        if (!map_game_memory())
        {
            check(false, "map the game's addresses (0x01000000) for the "
                         "session copies");
            return;
        }
        g_api[CRef] = mathtest_session_api_ref();
        g_api[CS] = mathtest_session_api_s();
        g_api[CSc] = mathtest_session_api_sc();
        g_api[CDn] = mathtest_session_api_dn();
        g_api[CDc] = mathtest_session_api_dc();
        g_api[CLg] = mathtest_session_api_lg();
        const char* out_dir = getenv("MATHTEST_OUT");
        static wchar_t wide_dir[512];
        mbstowcs(wide_dir, out_dir ? out_dir : "/tmp/", 500);
        g_scn.exe_dir = wide_dir;

        bool inited = true;
        for (const SessionApi* a : g_api)
        {
            a->sync();
            inited = a->init() && inited;
        }
        check(inited, "all session copies initialise (stub runtime)");
        g_api[CS]->set_shared(false, false);
        g_api[CSc]->set_shared(true, true);
        g_api[CDn]->set_shared(true, false);
        g_api[CDc]->set_shared(true, true);
        g_api[CLg]->set_shared(false, false);
        for (const SessionApi* a : g_api)
        {
            a->set_scene_size(3840, 1920);
            a->log->clear();
        }

        g_rng = 0x2545F491u;
        // MATHTEST_BENCH_ONLY=<ref|s|sc|dn|dc>: only the timing loop of that copy
        // (for valgrind --tool=callgrind --toggle-collect='*bench*').
        const char* bench_only = getenv("MATHTEST_BENCH_ONLY");
        if (bench_only)
        {
            g_world = World{};
            g_world.ipd = 0.063f;
            for (int f = 0; f < 3; ++f)
                synth_frame(f);
        }

        // c0_capture.bin from the default copy, replayed afterwards.
        char capture_path[600];
        snprintf(capture_path, sizeof(capture_path), "%sc0_capture.bin",
                 out_dir ? out_dir : "/tmp/");
        remove(capture_path);
        if (!bench_only)
            g_api[CDc]->capture(true);

        g_world = World{};
        g_world.ipd = 0.063f;
        g_world.sky_offset[0] = 350.0f;
        g_world.sky_offset[1] = -900.0f;
        g_world.sky_offset[2] = 2800.0f;
        g_world.decoy_offset[0] = -130.0f;
        g_world.decoy_offset[1] = -118.0f;
        g_world.decoy_offset[2] = 1500.0f;
        g_world.drift_offset[0] = -1700.0f;
        g_world.drift_offset[1] = -600.0f;
        g_world.drift_offset[2] = -2600.0f;
        if (!bench_only)
        {
        for (int f = 0; f < 260; ++f)
            synth_frame(f);
        sky_edge_frames();
        g_api[CDc]->capture(false);

        const int synthetic_pairs = g_suite.pairs;
        char what[256];
        snprintf(what, sizeof(what), "synthetic: %d draws (%d with the "
                 "upload-time centre call), %d substituted, 0 mismatches "
                 "across 5 copies", g_suite.pairs, g_suite.centres,
                 g_suite.substituted);
        check(g_suite.mismatches == 0, what);
        printf("    by kind:");
        for (int c = 0; c < CategoryCount; ++c)
            printf("%s %s %d", c ? "," : "", kCategoryNames[c],
                   g_suite.per_category[c]);
        printf("\n");
        bool covered = synthetic_pairs >= 200;
        for (int c = 0; c < CategoryCount; ++c)
            covered = covered && g_suite.per_category[c] > 0;
        check(covered, ">= 200 draws, every kind covered");
        snprintf(what, sizeof(what), "sky draws where only one eye was "
                 "pinned (learning window edge) were exercised: %d of %d",
                 g_suite.sky_asymmetric, g_suite.sky_pairs);
        check(g_suite.sky_asymmetric > 0, what);

        // Side effects as log lines: identical order for the single-call
        // copies, the same lines for the dual ones.
        auto filtered = [](const std::vector<std::string>& in) {
            std::vector<std::string> out;
            for (const std::string& s : in)
                if (s.rfind("c0 capture", 0) != 0 &&
                    s.rfind("stereo: eye matrix cache", 0) != 0)
                    out.push_back(s);
            return out;
        };
        const std::vector<std::string> ref_log = filtered(*g_api[CRef]->log);
        if (getenv("MATHTEST_SHOW_LOG"))
            for (const std::string& line : ref_log)
                printf("    ref log: %s\n", line.c_str());
        bool logs_same = true;
        for (int c = 1; c < CopyCount; ++c)
        {
            std::vector<std::string> l = filtered(*g_api[c]->log);
            std::vector<std::string> r = ref_log;
            if (c == CDn || c == CDc || c == CLg)
            {
                std::sort(l.begin(), l.end());
                std::sort(r.begin(), r.end());
            }
            if (l != r)
            {
                logs_same = false;
                printf("    log of %s differs (%zu lines, reference %zu)\n",
                       g_api[c]->name, l.size(), r.size());
            }
        }
        snprintf(what, sizeof(what), "logged side effects match the "
                 "reference (%zu lines)", ref_log.size());
        check(logs_same, what);

        unsigned hits = 0, misses = 0;
        g_api[CDc]->cache_stats(&hits, &misses);
        printf("    eye matrix cache (dc): %u hits, %u misses\n", hits, misses);

        // Replay what the dc copy captured, then any capture from the game.
        const int before = g_suite.pairs, mismatches = g_suite.mismatches;
        int frames = 0;
        const int records = replay_capture(capture_path, &frames);
        snprintf(what, sizeof(what), "c0_capture.bin written and replayed: "
                 "%d records, %d frames, %d draws, 0 mismatches", records,
                 frames, g_suite.pairs - before);
        check(records > 0 && g_suite.mismatches == mismatches, what);
        if (const char* game_capture = getenv("C0_CAPTURE"))
        {
            const int b2 = g_suite.pairs, m2 = g_suite.mismatches;
            const int n = replay_capture(game_capture, &frames);
            snprintf(what, sizeof(what), "game capture %s: %d records, %d "
                     "draws, 0 mismatches", game_capture, n,
                     g_suite.pairs - b2);
            check(n > 0 && g_suite.mismatches == m2, what);
        }

        }   // !bench_only

        g_rng = 0x6A09E667u;
        // Timing: one gameplay frame's worth of draws (about 1500, mostly
        // world geometry), as the perf cpu "mod matrix math" bucket sees it.
        std::vector<BenchDraw> draws;
        reset_state_flags();
        g_scn.menu = g_scn.pause = g_scn.binocular = g_scn.first_person = false;
        g_scn.hmd_drives_camera = true;
        g_scn.camera_ok = true;
        g_scn.world_scale_factor = 1.0f;
        set_movie(false);
        const GameProjection gp = make_projection(1.2f, 10.0f, 40000.0f);
        for (int i = 0; i < 1500; ++i)
        {
            BenchDraw d{};
            const int pick = i % 20;
            if (pick < 4)
                regs_of(projection_from_game(gp), d.c0);
            else if (pick == 4 && i < 40)
            {
                const float where[3] = { g_scn.retail[0] + 350.0f,
                                         g_scn.retail[1] - 900.0f,
                                         g_scn.retail[2] + 2800.0f };
                wvp_regs(where, 0.0f, 1.0f, gp, d.c0);
                d.sky = true;
            }
            else if (pick == 19)
            {
                regs_of(projection_from_game(make_projection(1.1f, 1.0f,
                                                             5000.0f)), d.c0);
                d.interface = true;
            }
            else
            {
                const float where[3] = { g_scn.retail[0] + (float)(i % 37) * 90.0f,
                                         g_scn.retail[1],
                                         g_scn.retail[2] + 500.0f + (float)i };
                wvp_regs(where, (float)i * 0.1f, 1.0f, gp, d.c0);
            }
            draws.push_back(d);
        }
        const int frames_bench = getenv("MATHTEST_BENCH_FRAMES")
                               ? atoi(getenv("MATHTEST_BENCH_FRAMES")) : 60;
        if (bench_only)
        {
            const std::string which = bench_only;
            const Copy copy = which == "sc" ? CSc : which == "dn" ? CDn
                            : which == "dc" ? CDc : which == "ref" ? CRef
                            : CS;
            const double ns = bench(copy, copy == CDn || copy == CDc, draws,
                                    frames_bench);
            printf("    bench %s: %.0f ns per stereo draw\n", which.c_str(), ns);
            return;
        }
        // warm-up
        bench(CS, false, draws, 3);
        bench(CSc, false, draws, 3);
        bench(CDc, true, draws, 3);
        const double original = bench(CRef, false, draws, frames_bench);
        const double legacy = bench(CS, false, draws, frames_bench);
        const double cached = bench(CSc, false, draws, frames_bench);
        const double dual_nc = bench(CDn, true, draws, frames_bench);
        const double dual = bench(CDc, true, draws, frames_bench);
        printf("    timing (host, ns per stereo draw): reference 2 calls %.0f, "
               "dual + eye cache -%.0f%%\n", original,
               100.0 * (1.0 - dual / original));
        printf("    timing (host, ns per stereo draw): legacy 2 calls %.0f, "
               "2 calls + eye cache %.0f (-%.0f%%), dual %.0f (-%.0f%%), "
               "dual + eye cache %.0f (-%.0f%%)\n", legacy, cached,
               100.0 * (1.0 - cached / legacy), dual_nc,
               100.0 * (1.0 - dual_nc / legacy), dual,
               100.0 * (1.0 - dual / legacy));
    }
}
#endif  // MATHTEST_SESSION

// God-mode modules ported from the Underworld mod: one file of tests each.
#include "body_push_tests.inc"
#include "body_volumes_tests.inc"
#include "god_carry_tests.inc"
#include "god_pluck_tests.inc"
#include "soft_body_tests.inc"
#include "rear_jiggle_tests.inc"
#include "rear_shape_tests.inc"
#include "board_leash_tests.inc"
#include "menu_panel_tests.inc"
#include "finger_input_tests.inc"

int main()
{
    printf("projection maths tests\n\n");

    test_body_push();
    test_body_volumes();
    test_body_volumes_legend();
    test_god_carry();
    test_god_pluck();
    test_soft_body();
    test_rear_jiggle();
    test_rear_shape();
    test_board_leash();
    test_menu_panel();
    test_finger_input();
    printf("  %d god-mode checks run quietly\n\n", g_quiet_checks);

    // --- reading the game's projection back out of its registers ----------
    GameProjection gp = projection_from_registers(kGameRegs);
    check(gp.valid, "recognised the game's registers as a projection");
    check(close(gp.sx, 2.0000f), "recovered x scale 2.0000");
    check(close(gp.sy, -2.2857f), "recovered y scale -2.2857 (renders flipped)");
    check(close(gp.ox, 0.0f) && close(gp.oy, 0.0f), "frustum is symmetric");
    check(close(gp.near_z, 16.0004f, 1e-3f), "recovered near plane 16");
    printf("    near=%.4f far=%.1f q=%.6f\n", gp.near_z, gp.far_z, gp.q);

    // --- rebuilding it must give back exactly what we started with --------
    float rebuilt[16];
    registers_from_matrix(projection_from_game(gp), rebuilt);
    bool same = true;
    for (int i = 0; i < 16; ++i)
        if (!close(rebuilt[i], kGameRegs[i]))
            same = false;
    check(same, "rebuilding from the recovered values reproduces the registers");
    if (!same)
    {
        print_regs("expected:", kGameRegs);
        print_regs("got:", rebuilt);
    }

    // --- the tangent path must agree with the game's own convention -------
    // Symmetric frustum with the game's field of view: sx = 2/(r-l) with
    // r = -l gives r = 1/sx, and likewise for y.
    const float r = 1.0f / gp.sx;
    const float b = 1.0f / fabsf(gp.sy);
    Mat4 fromTangents = projection_from_tangents(-r, r, -b, b,
                                                 gp.near_z, gp.far_z,
                                                 /*flip_y*/ gp.sy < 0.0f);
    float tang[16];
    registers_from_matrix(fromTangents, tang);
    same = true;
    for (int i = 0; i < 16; ++i)
        if (!close(tang[i], kGameRegs[i], 1e-3f))
            same = false;
    check(same, "building from tangents reproduces the game's matrix");
    if (!same)
    {
        print_regs("expected:", kGameRegs);
        print_regs("got:", tang);
    }

    // --- an eye offset is a translation in view space ---------------------
    // Moving the camera right by d is moving the world left by d, so the left
    // eye translates the world by +d and the right eye by -d.
    const float ipd_half = 0.032f;      // metres; scaled to game units later
    Mat4 eye = translation(-ipd_half, 0.0f, 0.0f) * projection_from_game(gp);
    float eyeRegs[16];
    registers_from_matrix(eye, eyeRegs);

    // Only c0's w should move, by tx * sx, and nothing else may change.
    check(close(eyeRegs[0 * 4 + 3], -ipd_half * gp.sx),
          "eye offset lands in c0.w as tx * sx");
    bool untouched = true;
    for (int i = 0; i < 16; ++i)
    {
        if (i == 0 * 4 + 3)
            continue;
        if (!close(eyeRegs[i], kGameRegs[i]))
            untouched = false;
    }
    check(untouched, "a lateral eye offset disturbs nothing else");
    if (!untouched)
        print_regs("eye matrix:", eyeRegs);

    // --- an off-centre frustum is what makes stereo correct ---------------
    //
    // Checking these for "non-zero" was not enough: the offsets were the right
    // magnitude with the wrong sign for a long time, which shifts each eye the
    // wrong way and stops the two fusing while each still looks fine alone.
    //
    // The real requirement is the definition of the frustum: a ray along the
    // left tangent must land on the left edge of the screen, and so on. Those
    // are the tangents from a real Reverb G2, strongly asymmetric, which is
    // where a sign error actually shows.
    {
        const float L = -1.3000f, R = 0.8862f, T = -1.0497f, B = 1.0560f;
        Mat4 f = projection_from_tangents(L, R, T, B, gp.near_z, gp.far_z, false);

        // Row-vector: clip = v * M, and for a point at x/z = tangent the NDC
        // works out as tangent * scale + offset.
        const float sx = f.m[0][0], ox = f.m[2][0];
        const float sy = f.m[1][1], oy = f.m[2][1];

        check(close(L * sx + ox, -1.0f, 1e-3f),
              "a ray along the left tangent lands on the left edge");
        check(close(R * sx + ox, 1.0f, 1e-3f),
              "a ray along the right tangent lands on the right edge");
        check(close(T * sy + oy, -1.0f, 1e-3f),
              "a ray along the raw top tangent lands on the bottom edge");
        check(close(B * sy + oy, 1.0f, 1e-3f),
              "a ray along the raw bottom tangent lands on the top edge");

        // The centre of an asymmetric frustum is not the centre of the screen,
        // and the offset has to point the right way.
        check(ox > 0.0f,
              "an eye whose frustum reaches further left offsets to the right");

        float offRegs[16];
        registers_from_matrix(f, offRegs);
        check(close(offRegs[3 * 4 + 2], 1.0f), "w is still z_view");
    }

    // --- vertical asymmetry in the game's y-down view ----------------------
    // OpenVR's raw "bottom" is the tangent to the UP edge (Valve driver docs).
    // The game's view is y-down (sy < 0), so a ray at view y/z = -B points up
    // and must reach the top of the screen (NDC +1). A Quest-like frustum with
    // much more room below than above shows the sign; a G2's near-symmetric
    // one cannot.
    {
        const float T = -1.20f, B = 0.95f;   // 50 degrees down, 43.5 up
        Mat4 f = projection_from_tangents(-1.0f, 1.0f, T, B,
                                          gp.near_z, gp.far_z, true);
        const float sy = f.m[1][1], oy = f.m[2][1];
        check(close(-B * sy + oy, 1.0f, 1e-3f),
              "y-down: a ray along the up edge (raw bottom) lands at the top");
        check(close(-T * sy + oy, -1.0f, 1e-3f),
              "y-down: a ray along the down edge (raw top) lands at the bottom");
        Mat4 g = projection_from_tangents(-1.0f, 1.0f, T, B,
                                          gp.near_z, gp.far_z, false);
        check(close(B * g.m[1][1] + g.m[2][1], 1.0f, 1e-3f),
              "y-up: the raw bottom tangent is the top edge");
    }

    // --- a mono HUD still needs each lens's asymmetric projection ---------
    // Copying identical NDC coordinates to the two eye textures does not copy
    // identical rays: the Reverb G2's optical centres are about 19% off the
    // texture centre in opposite directions. Map the mono image through each
    // frustum while deliberately omitting positional eye separation.
    {
        const float T = -1.0497f, B = 1.0560f;
        Mat4 centre = projection_from_tangents(-1.0931f, 1.0931f, T, B,
                                               gp.near_z, gp.far_z, false);
        Mat4 left = projection_from_tangents(-1.3000f, 0.8862f, T, B,
                                             gp.near_z, gp.far_z, false);
        Mat4 right = projection_from_tangents(-0.8862f, 1.3000f, T, B,
                                              gp.near_z, gp.far_z, false);
        const float ui_scale = 0.6f;
        Mat4 source = projection_from_game(gp);
        Mat4 left_ui = reproject_ndc_to_frustum(source, centre, left, ui_scale);
        Mat4 right_ui = reproject_ndc_to_frustum(source, centre, right, ui_scale);

        const float source_tangent = 0.2f;
        const float source_ndc = source_tangent * source.m[0][0] + source.m[2][0];
        const float wanted_ray = (ui_scale * source_ndc - centre.m[2][0])
                               / centre.m[0][0];
        const float left_ndc = source_tangent * left_ui.m[0][0] + left_ui.m[2][0];
        const float right_ndc = source_tangent * right_ui.m[0][0] + right_ui.m[2][0];
        const float left_ray = (left_ndc - left.m[2][0]) / left.m[0][0];
        const float right_ray = (right_ndc - right.m[2][0]) / right.m[0][0];

        check(close(left_ray, wanted_ray, 1e-4f) &&
              close(right_ray, wanted_ray, 1e-4f),
              "HUD coordinates become the same physical ray in both eyes");
        check(close(left_ui.m[2][0], left.m[2][0], 1e-4f) &&
              close(right_ui.m[2][0], right.m[2][0], 1e-4f),
              "lens-centre correction is not accidentally reduced by UI scale");

        bool depth_untouched = true;
        for (int row = 0; row < 4; ++row)
            if (!close(left_ui.m[row][2], source.m[row][2]) ||
                !close(left_ui.m[row][3], source.m[row][3]))
                depth_untouched = false;
        check(depth_untouched,
              "HUD lens correction leaves clip W and depth untouched");

        // World locking rotates a real perspective plane. Give its target
        // projection a close overlay near plane so the source plane (which
        // starts at the game's near depth) is not clipped as it rotates.
        const Mat4 left_overlay = projection_from_tangents(
            -1.3000f, 0.8862f, T, B, 1.0f, gp.far_z, false);
        const Mat4 locked_identity = reproject_ndc_to_world_locked(
            source, centre, left_overlay, Mat4::identity(), ui_scale);
        const float identity_x = source_tangent * locked_identity.m[0][0]
                               + locked_identity.m[2][0];
        const float identity_w = source_tangent * locked_identity.m[0][3]
                               + locked_identity.m[2][3];
        const float identity_ray = (identity_x / identity_w - left.m[2][0]) /
                                   left.m[0][0];
        check(close(identity_ray, wanted_ray, 1e-4f),
              "a newly anchored menu keeps the head-locked HUD ray");

        const float menu_yaw = 0.25f;
        const Mat4 locked_yaw = reproject_ndc_to_world_locked(
            source, centre, left_overlay, yaw_rotation(menu_yaw), ui_scale);
        const float clip_x = locked_yaw.m[2][0] + locked_yaw.m[3][0];
        const float clip_w = locked_yaw.m[2][3] + locked_yaw.m[3][3];
        const float locked_ndc = clip_x / clip_w;
        const float locked_ray = (locked_ndc - left.m[2][0]) / left.m[0][0];
        check(close(locked_ray, tanf(menu_yaw), 1e-3f),
              "turning the head moves a world-locked menu by the same angle");

        const Mat4 partial_yaw = rotation_fraction(yaw_rotation(menu_yaw), 0.15f);
        check(close(yaw_of(partial_yaw),
                    0.15f * yaw_of(yaw_rotation(menu_yaw)), 1e-4f),
              "gameplay HUD drag retains the requested fraction of head yaw");
        const Mat4 no_drag = rotation_fraction(yaw_rotation(menu_yaw), 0.0f);
        check(close(no_drag.m[0][0], 1.0f) && close(no_drag.m[2][2], 1.0f) &&
              close(no_drag.m[0][2], 0.0f) && close(no_drag.m[2][0], 0.0f),
              "full HUD follow reduces the residual rotation to identity");

        Mat4 pitch = Mat4::identity();
        pitch.m[1][1] = cosf(0.30f);  pitch.m[1][2] = -sinf(0.30f);
        pitch.m[2][1] = sinf(0.30f);  pitch.m[2][2] =  cosf(0.30f);
        Mat4 roll = Mat4::identity();
        roll.m[0][0] = cosf(-0.20f); roll.m[0][1] = -sinf(-0.20f);
        roll.m[1][0] = sinf(-0.20f); roll.m[1][1] =  cosf(-0.20f);
        const Mat4 combined = yaw_rotation(menu_yaw) * pitch * roll;
        const Mat4 half = rotation_fraction(combined, 0.5f);
        const Mat4 rebuilt_rotation = half * half;
        bool full_rotation = true;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                if (!close(rebuilt_rotation.m[row][column],
                           combined.m[row][column], 1e-4f))
                    full_rotation = false;
        check(full_rotation,
              "HUD drag scales combined yaw, pitch and roll on one rigid arc");

        const float menu_z = gp.near_z;
        const float rotated_clip_z = menu_z * locked_yaw.m[2][2]
                                   + locked_yaw.m[3][2];
        const float rotated_clip_w = menu_z * locked_yaw.m[2][3]
                                   + locked_yaw.m[3][3];
        check(rotated_clip_z >= 0.0f &&
              rotated_clip_z <= rotated_clip_w,
              "overlay near plane keeps a rotated menu inside clip depth");
    }

    // --- refuse to touch registers that are not a projection --------------
    float affine[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        5, 6, 7, 1,
    };
    check(!projection_from_registers(affine).valid,
          "an affine matrix is rejected, not mistaken for a projection");

    float lights[16] = {
        -33480.15f, 3862.18f, 54620.36f, 1250.0f,
        0.0001f, 0.0001f, 0.0001f, 0.0f,
        -33921.61f, 4197.66f, 54792.84f, 500.0f,
        0.0002f, 0.0002f, 0.0002f, 0.0f,
    };
    check(!projection_from_registers(lights).valid,
          "the light block at c16 is rejected too");

    // --- decomposing a world-view-projection -------------------------------
    // Not all geometry arrives in view space. World geometry gets a full
    // world-view-projection in c0..c3, and it has to be taken apart against the
    // game's own projection so a different one can be put back.
    {
        Mat4 P = projection_from_game(gp);
        Mat4 Pinv{};
        check(invert(P, &Pinv), "the game's projection is invertible");

        Mat4 should_be_id = P * Pinv;
        bool id = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!close(should_be_id.m[i][j], i == j ? 1.0f : 0.0f, 1e-3f))
                    id = false;
        check(id, "projection times its inverse is the identity");

        // A world-view transform, rotation and translation, of the kind the
        // engine would fold in front of the projection.
        Mat4 X = Mat4::identity();
        X.m[0][0] =  0.6f; X.m[0][2] = 0.8f;
        X.m[2][0] = -0.8f; X.m[2][2] = 0.6f;
        X.m[3][0] = 120.0f; X.m[3][1] = -40.0f; X.m[3][2] = 900.0f;

        const Mat4 M = X * P;
        const Mat4 recovered = M * Pinv;
        bool same_x = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!close(recovered.m[i][j], X.m[i][j], 1e-2f))
                    same_x = false;
        check(same_x, "M * inverse(P) recovers the world-view transform exactly");

        // Accurate aim changes the camera depth range independently of some
        // pure-projection uploads. Recovering an actual near=50 WVP with a
        // cached near=16 projection used to leave a projective shear in X.
        GameProjection depth_changed = gp;
        depth_changed.near_z = 50.0f;
        depth_changed.q = depth_changed.far_z /
                          (depth_changed.far_z - depth_changed.near_z);
        const Mat4 changed_m = X * projection_from_game(depth_changed);
        GameProjection recovered_depth{};
        check(projection_depth_from_wvp(changed_m, gp, &recovered_depth),
              "world-view-projection exposes its own depth range");
        check(close(recovered_depth.near_z, depth_changed.near_z, 0.05f) &&
              close(recovered_depth.far_z, depth_changed.far_z, 300.0f),
              "recovered WVP near/far are independent of the cached projection");
        Mat4 changed_inv{};
        bool recovered_x = invert(projection_from_game(recovered_depth),
                                  &changed_inv);
        const Mat4 changed_x = changed_m * changed_inv;
        for (int i = 0; recovered_x && i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!close(changed_x.m[i][j], X.m[i][j], 2.0e-3f))
                    recovered_x = false;
        check(recovered_x,
              "per-draw WVP depth recovery removes the camera transform cleanly");

        // And such a matrix must be recognised as perspective, while a 2D pass
        // must not be.
        float wvp[16];
        registers_from_matrix(M, wvp);
        check(is_perspective_registers(wvp),
              "a world-view-projection is recognised as perspective");

        const float ortho[16] = {
            2, 0, 0, 0,
            0, 2, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,          // c3 = (0,0,0,1): w is constant
        };
        check(!is_perspective_registers(ortho),
              "a 2D pass is not mistaken for perspective");
    }

    // --- pose to view -----------------------------------------------------
    // OpenVR poses are 3x4 row-major, right-handed, Y up, -Z forward, metres.
    {
        const float identity_pose[12] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
        };
        Mat4 v = view_from_pose(identity_pose, 100.0f);
        bool is_id = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (!close(v.m[i][j], i == j ? 1.0f : 0.0f))
                    is_id = false;
        check(is_id, "an identity pose gives an identity view");

        // Head one metre to the right. The world must move a metre left, in
        // game units.
        const float right_pose[12] = {
            1, 0, 0, 1.0f,
            0, 1, 0, 0,
            0, 0, 1, 0,
        };
        v = view_from_pose(right_pose, 100.0f);
        check(close(v.m[3][0], -100.0f), "stepping right moves the world left, in game units");
        check(close(v.m[3][1], 0.0f) && close(v.m[3][2], 0.0f),
              "a sideways step moves nothing else");

        // Forward is -Z in OpenVR and +Z here, so a step forward has to come
        // out as -Z in the engine's space.
        const float fwd_pose[12] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, -1.0f,
        };
        v = view_from_pose(fwd_pose, 100.0f);
        check(close(v.m[3][2], -100.0f),
              "stepping forward (-Z in OpenVR) moves the world to -Z here");

        // 90 degrees of yaw, head turning left: a positive rotation about Y in
        // OpenVR's right-handed frame.
        const float cs = 0.0f, sn = 1.0f;
        const float yaw_pose[12] = {
             cs, 0, sn, 0,
              0, 1,  0, 0,
            -sn, 0, cs, 0,
        };
        v = view_from_pose(yaw_pose, 100.0f);

        // A point straight ahead in engine space is +Z. Turn the head left and
        // it should swing round to the right, +X.
        const float pz = 1.0f;
        const float ox = pz * v.m[2][0];
        const float oz = pz * v.m[2][2];
        check(close(ox, 1.0f) && close(oz, 0.0f),
              "turning the head left swings what was ahead round to the right");

        // Pitch and roll are what a missing Y-flip gets wrong while leaving yaw
        // untouched. Rather than argue the absolute direction of each from
        // first principles -- which is easy to talk yourself into backwards --
        // these pin the relationship that was actually observed in the headset:
        // with the old diag(1,1,-1) conversion yaw was correct and pitch and
        // roll were both inverted. So the current conversion must agree with
        // the old one on yaw and oppose it on the other two.
        {
            // 90 degrees about each axis, right-handed, as OpenVR reports them.
            const float yaw_p[12]   = {  cs,0,sn,0,   0,1,0,0,   -sn,0,cs,0 };
            const float pitch_p[12] = { 1,0,0,0,   0,cs,-sn,0,   0,sn,cs,0 };
            const float roll_p[12]  = { cs,-sn,0,0,   sn,cs,0,0,   0,0,1,0 };

            struct Case { const char* name; const float* pose; bool same_as_old; };
            const Case cases[3] = {
                { "yaw is unchanged by the Y-flip",        yaw_p,   true  },
                { "pitch is reversed by the Y-flip",       pitch_p, false },
                { "roll is reversed by the Y-flip",        roll_p,  false },
            };

            for (const Case& c : cases)
            {
                const Mat4 now = view_from_pose(c.pose, 1.0f);
                const Mat4 old = legacy_view_from_pose(c.pose);

                bool identical = true;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        if (!close(now.m[i][j], old.m[i][j]))
                            identical = false;

                check(identical == c.same_as_old, c.name);
            }
        }

        // Standing up moves the world down the screen, which in a Y-down space
        // is +Y.
        const float up_pose[12] = {
            1, 0, 0, 0,
            0, 1, 0, 1.0f,
            0, 0, 1, 0,
        };
        v = view_from_pose(up_pose, 100.0f);
        check(close(v.m[3][1], 100.0f),
              "standing up moves the world down the screen (+Y here)");

        // If this drifts the whole world shears.
        bool ortho = true;
        for (int i = 0; i < 3; ++i)
        {
            float len = 0.0f;
            for (int j = 0; j < 3; ++j)
                len += v.m[i][j] * v.m[i][j];
            if (!close(len, 1.0f))
                ortho = false;
        }
        check(ortho, "the view rotation stays orthonormal");
    }

    // -- the heading, and why it must come from the forward column --
    //
    // yaw_of was first written against row 2 of the view matrix. For a pose
    // carrying nothing but yaw that is merely the negation, which reads as a
    // sign to flip and hides the fault entirely. Add pitch and it stops
    // being a heading at all: in the headset that showed up as the yaw
    // inverting on the way round to facing backwards, and a sloping horizon.
    //
    // So these assert the definition -- the heading of a pose built with a
    // known yaw is that yaw, whatever pitch is present -- rather than that
    // it is non-zero, which the broken version satisfied perfectly.
    {
        const float yaw = 0.6f;      // about 34 degrees
        const float pitch = 0.4f;    // enough to matter

        // An OpenVR pose, right-handed with -Z forward: yaw about Y then
        // pitch about X, written as the 3x4 the runtime hands over.
        const float cy = cosf(yaw),   sy = sinf(yaw);
        const float cp = cosf(pitch), sp = sinf(pitch);
        float pose[12] = {
              cy,  sy * sp,  sy * cp, 0.0f,
            0.0f,       cp,      -sp, 0.0f,
             -sy,  cy * sp,  cy * cp, 0.0f,
        };
        float flat[12] = {
              cy,     0.0f,       sy, 0.0f,
            0.0f,     1.0f,     0.0f, 0.0f,
             -sy,     0.0f,       cy, 0.0f,
        };

        const Mat4 v = view_from_pose(pose, 100.0f);
        const float got = yaw_of(v);
        const float got_flat = yaw_of(view_from_pose(flat, 100.0f));

        check(close(got, got_flat, 1e-3f),
              "the heading is the same with pitch as without it");
        check(fabsf(got) > 0.1f, "and it is a real angle, not zero");

        // Taking the heading back out has to leave none behind: the property
        // the runtime leans on when the camera is carrying the yaw. Two
        // candidates were tried at first and neither worked with pitch in
        // the pose -- that failure is why remove_yaw exists.
        const Mat4 residual = remove_yaw(v, got);
        check(fabsf(yaw_of(residual)) < 1e-3f,
              "removing the heading leaves none of it behind");

        // And it has to leave the pitch alone, or it is not removing a
        // heading, it is just finding some rotation with no heading in it.
        const float pitch_before = asinf(v.m[1][2]);
        const float pitch_after  = asinf(residual.m[1][2]);
        check(close(pitch_before, pitch_after, 1e-3f),
              "and it leaves the pitch untouched");

        // set_yaw is what splits the head rotation between the camera and
        // the projection. The camera carries whatever it was last built
        // with; the projection owes the difference. Getting this wrong does
        // not look like a maths error in the headset -- the view just stops
        // following the head whenever the engine skips a camera rebuild.
        for (int i = 0; i < 5; ++i)
        {
            const float wanted = -1.2f + 0.6f * (float)i;
            const Mat4 aimed = set_yaw(v, wanted);
            check(fabsf(wrap_pi(yaw_of(aimed) - wanted)) < 1e-3f,
                  "set_yaw lands on the heading it was asked for");
            check(close(asinf(v.m[1][2]), asinf(aimed.m[1][2]), 1e-3f),
                  "  and keeps the pitch while doing it");
        }

        // The two cases the runtime actually hits: the camera fully up to
        // date, and the camera frozen behind a menu.
        check(fabsf(yaw_of(set_yaw(v, 0.0f))) < 1e-3f,
              "a camera holding all the yaw leaves the projection none");
        check(fabsf(wrap_pi(yaw_of(set_yaw(v, got)) - got)) < 1e-3f,
              "a frozen camera leaves the projection all of it");
    }

    // -- the first-person heading looking steeply down or up --
    //
    // Lara faces and walks along the headset heading. yaw_of reads it from
    // the forward column alone, which looking at the feet is nearly
    // vertical: a 10 degree head turn there read as 45 and swung Lara with
    // it (review 2026-10-02). steady_yaw_of has to agree with yaw_of for any
    // plain yaw-and-pitch pose, and stay near 1:1 for a turn of the head.
    {
        // OpenVR rotation: yaw about Y, then pitch about X, then a turn of
        // the head about its own up axis.
        auto pose_of = [](float yaw, float pitch, float turn, float* pose) {
            const float cy = cosf(yaw), sy = sinf(yaw);
            const float cp = cosf(pitch), sp = sinf(pitch);
            const float ct = cosf(turn), st = sinf(turn);
            const float Y[3][3] = { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } };
            const float P[3][3] = { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } };
            const float T[3][3] = { { ct, 0, st }, { 0, 1, 0 }, { -st, 0, ct } };
            float YP[3][3], R[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    YP[i][j] = 0.0f;
                    for (int k = 0; k < 3; ++k)
                        YP[i][j] += Y[i][k] * P[k][j];
                }
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                {
                    R[i][j] = 0.0f;
                    for (int k = 0; k < 3; ++k)
                        R[i][j] += YP[i][k] * T[k][j];
                }
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                    pose[i * 4 + j] = R[i][j];
                pose[i * 4 + 3] = 0.0f;
            }
        };

        bool agrees = true;
        for (int y = -3; y <= 3; ++y)
            for (int p = -8; p <= 8; ++p)
            {
                float pose[12];
                pose_of(0.9f * (float)y, 0.17f * (float)p, 0.0f, pose);
                const Mat4 v = view_from_pose(pose, 100.0f);
                agrees = agrees &&
                    fabsf(wrap_pi(steady_yaw_of(v) - yaw_of(v))) < 1e-3f;
            }
        check(agrees, "steady_yaw_of equals yaw_of for yaw and pitch alone");

        float level[12], down[12], up[12];
        pose_of(0.5f, -1.5690f, 0.0f, down);   // 89.9 degrees down
        pose_of(0.5f, 1.5690f, 0.0f, up);
        pose_of(0.5f, 0.0f, 0.0f, level);
        const float heading = yaw_of(view_from_pose(level, 100.0f));
        check(fabsf(wrap_pi(steady_yaw_of(view_from_pose(down, 100.0f)) -
                            heading)) < 1e-2f &&
              fabsf(wrap_pi(steady_yaw_of(view_from_pose(up, 100.0f)) -
                            heading)) < 1e-2f,
              "  and keeps the heading looking straight down or up");

        // 80 degrees down at the feet, head turned 10 degrees.
        float turned[12];
        const float turn = 0.1745f;
        pose_of(0.5f, -1.3963f, turn, turned);
        const Mat4 t = view_from_pose(turned, 100.0f);
        const float steady = fabsf(wrap_pi(steady_yaw_of(t) - heading));
        const float plain = fabsf(wrap_pi(yaw_of(t) - heading));
        printf("    80 deg down, 10 deg head turn: yaw_of moves %.1f deg, "
               "steady_yaw_of %.1f\n", plain * 57.29578f, steady * 57.29578f);
        check(plain > 0.5f, "yaw_of swings with a small turn at the feet");
        check(steady < 1.3f * turn,
              "  steady_yaw_of follows it at most about 1:1");
        float level_turned[12];
        pose_of(0.5f, 0.0f, turn, level_turned);
        check(fabsf(wrap_pi(steady_yaw_of(view_from_pose(level_turned,
                                                          100.0f)) -
                            heading) - turn) < 1e-3f ||
              fabsf(wrap_pi(steady_yaw_of(view_from_pose(level_turned,
                                                          100.0f)) -
                            heading) + turn) < 1e-3f,
              "  and a level head turn moves it exactly");
    }

    // -- a heading applied to a vector must match the matrix --
    //
    // The camera takes a heading as a matrix and the head displacement takes
    // it as a vector. If those two ever disagree, walking forward comes out
    // sideways, which is exactly what it did.
    {
        const float angle = 0.7f;
        float v[3] = { 1.0f, 2.0f, 3.0f };
        const Mat4 r = yaw_rotation(angle);
        const float mx = v[0] * r.m[0][0] + v[1] * r.m[1][0] + v[2] * r.m[2][0];
        const float mz = v[0] * r.m[0][2] + v[1] * r.m[1][2] + v[2] * r.m[2][2];

        rotate_yaw(v, angle);
        check(close(v[0], mx, 1e-4f) && close(v[2], mz, 1e-4f),
              "a heading rotates a vector the same way it rotates a matrix");
        check(close(v[1], 2.0f),
              "and leaves the vertical alone");

        // Undoing it has to return the vector, or the correction that takes
        // the head yaw back out of a room-space displacement is lossy.
        rotate_yaw(v, -angle);
        check(close(v[0], 1.0f, 1e-4f) && close(v[2], 3.0f, 1e-4f),
              "and the opposite heading puts it back");
    }

    // -- the rigid inverse --
    //
    // The camera rewrite rotates the world-to-camera matrix and then has to
    // rebuild its opposite, because the engine only recomputes that on one of
    // the several paths that reach the transform. If these two disagree, the
    // culling frustum is built from a camera that is not where the picture is
    // drawn from -- which looks like everything and nothing.
    {
        Mat4 m = yaw_rotation(0.9f);
        m.m[3][0] = 120.0f;
        m.m[3][1] = -45.0f;
        m.m[3][2] = 300.0f;

        const Mat4 back = rigid_inverse(m) * m;
        bool identity = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                identity = identity &&
                    close(back.m[i][j], (i == j) ? 1.0f : 0.0f, 1e-3f);
        check(identity, "a rigid transform times its inverse is the identity");

        // The other order too: a camera-to-world built this way has to undo
        // the world-to-camera it came from, not merely resemble it.
        const Mat4 fwd = m * rigid_inverse(m);
        identity = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                identity = identity &&
                    close(fwd.m[i][j], (i == j) ? 1.0f : 0.0f, 1e-3f);
        check(identity, "and the other way round as well");

        // The isolated-hand draw moves a wrist and every finger by the same
        // rigid delta. The wrist must reach the controller while the fingers
        // keep their local pose; otherwise the glove would stretch apart.
        Mat4 target = yaw_rotation(-0.4f);
        target.m[3][0] = -30.0f;
        target.m[3][1] = 55.0f;
        target.m[3][2] = 470.0f;
        const Mat4 finger = translation(12.0f, 0.0f, 4.0f) * m;
        const Mat4 delta = rigid_inverse(m) * target;
        const Mat4 moved_wrist = m * delta;
        const Mat4 moved_finger = finger * delta;
        bool wrist_matches = true;
        bool finger_matches = true;
        const Mat4 expected_finger = translation(12.0f, 0.0f, 4.0f) * target;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                wrist_matches = wrist_matches &&
                    close(moved_wrist.m[i][j], target.m[i][j], 1e-3f);
                finger_matches = finger_matches &&
                    close(moved_finger.m[i][j], expected_finger.m[i][j], 1e-3f);
            }
        check(wrist_matches, "isolated wrist reaches the controller pose");
        check(finger_matches, "finger pose stays rigid beside the wrist");

        const Mat4 zero_alignment = hand_alignment_rotation(0, 0, 0);
        bool zero_is_identity = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                zero_is_identity = zero_is_identity &&
                    close(zero_alignment.m[i][j],
                          i == j ? 1.0f : 0.0f, 1e-5f);
        check(zero_is_identity,
              "zero live hand rotation preserves the existing wrist pose");
        const Mat4 x_turn = hand_alignment_rotation(90, 0, 0);
        const Mat4 y_turn = hand_alignment_rotation(0, 90, 0);
        const Mat4 z_turn = hand_alignment_rotation(0, 0, 90);
        check(close(x_turn.m[1][2], 1.0f, 1e-4f) &&
              close(y_turn.m[2][0], 1.0f, 1e-4f) &&
              close(z_turn.m[0][1], 1.0f, 1e-4f),
              "hand X/Y/Z tuning turns around the named local axis");

        // A position correction should not make a wrist orbit when the
        // physical controller rotates without moving its tracked origin.
        Mat4 controller_zero = Mat4::identity();
        controller_zero.m[3][0] = 40.0f;
        controller_zero.m[3][1] = -30.0f;
        controller_zero.m[3][2] = 110.0f;
        Mat4 controller_turned = hand_alignment_rotation(63, -41, 27);
        for (int axis = 0; axis < 3; ++axis)
            controller_turned.m[3][axis] = controller_zero.m[3][axis];
        const Mat4 wrist_zero = hand_pose_with_head_offset(
            controller_zero, 12.0f, -20.0f, 8.0f);
        const Mat4 wrist_turned = hand_pose_with_head_offset(
            controller_turned, 12.0f, -20.0f, 8.0f);
        bool pivot_still = true;
        bool rotation_follows = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            pivot_still = pivot_still &&
                close(wrist_zero.m[3][axis], wrist_turned.m[3][axis], 1e-4f);
            for (int other = 0; other < 3; ++other)
                rotation_follows = rotation_follows &&
                    close(wrist_turned.m[axis][other],
                          controller_turned.m[axis][other], 1e-4f);
        }
        check(pivot_still, "hand wrist stays put when controller rotates in place");
        check(rotation_follows, "hand rotation still follows controller rotation");
    }

    // -- residual motion for a camera that stops rebuilding while paused --
    //
    // A frozen world matrix already contains the head pose from the last
    // camera rebuild. Removing that anchor and composing the current pose must
    // be exactly the same as if the game had rebuilt its camera normally.
    {
        Mat4 base = yaw_rotation(-0.35f);
        base.m[3][0] = 80.0f;
        base.m[3][1] = -25.0f;
        base.m[3][2] = 410.0f;

        Mat4 anchor = yaw_rotation(0.2f);
        anchor.m[3][0] = -3.0f;
        anchor.m[3][1] = 1.5f;
        anchor.m[3][2] = 2.0f;

        Mat4 current = yaw_rotation(-0.65f);
        current.m[3][0] = 4.0f;
        current.m[3][1] = -2.5f;
        current.m[3][2] = -1.0f;

        const Mat4 frozen = base * anchor;
        const Mat4 residual = rigid_inverse(anchor) * current;
        const Mat4 updated = frozen * residual;
        const Mat4 expected_current = base * current;

        bool residual_matches = true;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                residual_matches = residual_matches &&
                    close(updated.m[i][j], expected_current.m[i][j], 1e-3f);
        check(residual_matches,
              "pause residual advances a frozen camera to the current head pose");
    }

    // -- wrapping --
    {
        const float two_pi = 6.28318531f;
        check(close(wrap_pi(0.5f), 0.5f),
              "an angle already in range is left alone");
        check(close(wrap_pi(6.0f), 6.0f - two_pi),
              "an angle past pi comes back as the short way round");
        check(close(wrap_pi(-6.0f), two_pi - 6.0f),
              "and the same going the other way");
        check(fabsf(wrap_pi(9.4247779f)) <= 3.14159266f,
              "several turns still land inside the range");
    }

    // -- immersive controller gesture zones --
    // Coordinates are metres relative to the player's yaw-only body frame.
    // The classifier should accept a natural upper-chest reach but reject
    // normal play positions at the side, face and lap.
    {
        check(vr_gesture_in_chest_zone(-0.13f, -0.25f, 0.03f),
              "immersive light accepts the centre of the chest zone");
        check(vr_gesture_in_chest_zone(-0.18f, -0.28f, 0.06f),
              "immersive light accepts a natural left-hand chest reach");
        check(!vr_gesture_in_chest_zone(0.13f, -0.25f, 0.03f),
              "immersive light rejects the medipack pouch on the right");
        check(!vr_gesture_in_chest_zone(-0.60f, -0.35f, 0.10f),
              "immersive light rejects a hand held out at the side");
        check(!vr_gesture_in_chest_zone(-0.10f, -0.78f, 0.10f),
              "immersive light rejects a controller resting in the lap");
        check(!vr_gesture_in_chest_zone(-0.24f, -0.50f, 0.08f),
              "immersive light rejects a reach toward the binocular belt");
        check(!vr_gesture_in_chest_zone(-0.10f, -0.08f, 0.02f),
              "immersive light rejects a controller raised near the face");
        check(vr_gesture_in_holster_zone(true, -0.259f, -0.63f, -0.08f) &&
              vr_gesture_in_holster_zone(false, 0.22f, -0.62f, -0.06f),
              "both pistol holsters accept their own hip reach");
        check(!vr_gesture_in_holster_zone(true, -0.21f, -0.41f, 0.12f) &&
              !vr_gesture_in_holster_zone(false, 0.20f, -0.41f, -0.04f),
              "ordinary high hand positions fall outside the smaller zones");
        VrHolsterZone custom = vr_default_holster_zone();
        custom.left_x = -0.50f;
        check(vr_gesture_in_holster_zone(true, -0.50f, custom.left_y,
                                        custom.left_z, &custom) &&
              !vr_gesture_in_holster_zone(true, -0.21f, -0.59f,
                                          0.12f, &custom),
              "developer zone centre changes the tested grab position");
        VrHolsterZone placed = vr_default_holster_zone();
        placed.right_y = -0.40f;
        placed.right_z = 0.20f;
        check(vr_gesture_in_holster_zone(false, placed.right_x, -0.40f,
                                         0.20f, &placed) &&
              !vr_gesture_in_holster_zone(false, placed.right_x,
                                          placed.left_y, placed.left_z,
                                          &placed) &&
              vr_gesture_in_holster_zone(true, placed.left_x,
                                         placed.left_y, placed.left_z,
                                         &placed),
              "a tuned holster moves only its own side");
        const float light_centre[3] = { 0.10f, -0.20f, 0.15f };
        check(vr_gesture_in_chest_zone(0.10f, -0.20f, 0.15f, light_centre) &&
              !vr_gesture_in_chest_zone(-0.16f, -0.34f, 0.09f,
                                        light_centre),
              "a tuned light zone replaces the default chest zone");
        check(!vr_gesture_in_holster_zone(true, 0.25f, -0.76f, 0.04f) &&
              !vr_gesture_in_holster_zone(false, -0.25f, -0.76f, 0.04f),
              "a pistol holster rejects the opposite hip");
        check(!vr_gesture_in_holster_zone(true, -0.12f, -0.38f, 0.12f),
              "the flashlight chest gesture cannot draw a pistol");
        check(vr_gesture_binoculars_at_face(-0.10f, -0.07f, 0.18f),
              "held binoculars reach the left eye");
        check(!vr_gesture_binoculars_at_face(-0.24f, -0.38f, 0.08f) &&
              !vr_gesture_binoculars_at_face(-0.24f, -0.63f, -0.07f),
              "chest and hip reaches do not open binocular view");
        check(vr_gesture_grapple_throw(0.42f, 0.31f, 1.6f),
              "a fast forward release throws the grapple");
        check(!vr_gesture_grapple_throw(0.42f, 0.31f, 0.5f) &&
              !vr_gesture_grapple_throw(0.10f, 0.31f, 1.6f) &&
              !vr_gesture_grapple_throw(0.42f, -0.12f, 1.6f),
              "slow, short and rearward releases do not throw the grapple");
        check(vr_gesture_grapple_pull(0.30f, 0.25f) &&
              vr_gesture_grapple_pull(0.16f, 1.2f),
              "a full pull or shorter fast yank tugs an attached grapple");
        check(!vr_gesture_grapple_pull(0.10f, 1.5f) &&
              !vr_gesture_grapple_pull(0.20f, 0.3f) &&
              !vr_gesture_grapple_pull(-0.25f, 2.0f),
              "short, slow and forward motions do not tug the grapple");
    }

    // -- first-person arm geometry --
    {
        const float x[3] = { 1.0f, 0.0f, 0.0f };
        const float y[3] = { 0.0f, 1.0f, 0.0f };
        const float negx[3] = { -1.0f, 0.0f, 0.0f };
        const Mat4 quarter = rotation_between(x, y);
        const Mat4 half = rotation_between(x, negx);
        check(close(quarter.m[0][0], 0.0f) &&
              close(quarter.m[0][1], 1.0f) &&
              close(half.m[0][0], -1.0f),
              "arm rotations map directions in the engine's row-vector order");

        const float shoulder[3] = { 0.0f, 0.0f, 0.0f };
        const float elbow[3] = { 100.0f, 0.0f, 0.0f };
        const float wrist[3] = { 100.0f, 100.0f, 0.0f };
        const float target[3] = { 0.0f, 100.0f, 0.0f };
        float solved_elbow[3], solved_wrist[3];
        const bool valid = solve_two_bone_arm(shoulder, elbow, wrist, target,
                                               solved_elbow, solved_wrist);
        const float upper_length = sqrtf(
            solved_elbow[0]*solved_elbow[0] +
            solved_elbow[1]*solved_elbow[1] +
            solved_elbow[2]*solved_elbow[2]);
        const float lower_length = sqrtf(
            (solved_wrist[0]-solved_elbow[0])*
            (solved_wrist[0]-solved_elbow[0]) +
            (solved_wrist[1]-solved_elbow[1])*
            (solved_wrist[1]-solved_elbow[1]) +
            (solved_wrist[2]-solved_elbow[2])*
            (solved_wrist[2]-solved_elbow[2]));
        check(valid && close(upper_length, 100.0f) &&
              close(lower_length, 100.0f) &&
              close(solved_wrist[0], target[0]) &&
              close(solved_wrist[1], target[1]),
              "arm solve reaches the controller without stretching either bone");

        const float old_upper[3] = {
            elbow[0]-shoulder[0], elbow[1]-shoulder[1], 0.0f
        };
        const float new_upper[3] = {
            solved_elbow[0]-shoulder[0],
            solved_elbow[1]-shoulder[1], 0.0f
        };
        const Mat4 upper_turn = rotation_between(old_upper, new_upper);
        float wrist_after_upper[3] = {
            wrist[0]*upper_turn.m[0][0] + wrist[1]*upper_turn.m[1][0],
            wrist[0]*upper_turn.m[0][1] + wrist[1]*upper_turn.m[1][1],
            wrist[0]*upper_turn.m[0][2] + wrist[1]*upper_turn.m[1][2]
        };
        const float old_lower[3] = {
            wrist_after_upper[0]-solved_elbow[0],
            wrist_after_upper[1]-solved_elbow[1],
            wrist_after_upper[2]-solved_elbow[2]
        };
        const float new_lower[3] = {
            solved_wrist[0]-solved_elbow[0],
            solved_wrist[1]-solved_elbow[1],
            solved_wrist[2]-solved_elbow[2]
        };
        const Mat4 lower_turn = rotation_between(old_lower, new_lower);
        float wrist_after_both[3] = { solved_elbow[0], solved_elbow[1],
                                      solved_elbow[2] };
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i)
                wrist_after_both[j] += old_lower[i]*lower_turn.m[i][j];
        check(close(wrist_after_both[0], solved_wrist[0], 1e-3f) &&
              close(wrist_after_both[1], solved_wrist[1], 1e-3f) &&
              close(wrist_after_both[2], solved_wrist[2], 1e-3f),
              "upper and lower arm rotations put the wrist at the solved point");

        const float distant[3] = { 1000.0f, 0.0f, 0.0f };
        check(solve_two_bone_arm(shoulder, elbow, wrist, distant,
                                 solved_elbow, solved_wrist) &&
              solved_wrist[0] < 200.0f &&
              solved_wrist[0] > 190.0f,
              "arm solve bounds an unreachable controller to full reach");
    }


    test_world_locked_prefix();
    test_exact_fast_products();
#if defined(MATHTEST_SESSION)
    run_session_suite();
#endif

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

#endif  // !MATHTEST_SESSION_STUBS
