#pragma once
// Pure helpers for drawing Lara held in the god hands (docs/GOD_HANDS_PLAN.md
// 4.2 / 4.4) and for the test aid that tells where she is in the head frame.
// No config(), no game memory, no Windows calls: mathtest links it.

#include "../vr_math.h"

namespace trlvr
{
    // translate(-p) * R * translate(p) (row vectors): turns things about p.
    Mat4 rotation_about_point(const Mat4& rotation, const float p[3]);

    // bones[i] = bones[i] * m for i in first..last (clipped to the array).
    void bones_transform_range(Mat4* bones, int bone_count, int first, int last,
                               const Mat4& m);

    // The procedural hang (4.4): the wrist target that raises one arm
    // straight up. wrist = shoulder + up * reach (reach is the arm's length
    // times a little slack, chosen by the caller). Axes as first-person
    // hands (AUDIT 17.3): x row = the fingers (+up on chain A, -up on B),
    // y row = down of the hand frame, z row = palm * right, where the hand's
    // right axis is the horizontal direction toward `toward` (her head) for a
    // left arm and away from it for a right arm, so the palm faces her head.
    // `up` is a unit vector; `toward` any non-parallel vector. Returns false
    // on degenerate input.
    bool hang_arm_target(const float shoulder[3], const float up[3], float reach,
                         const float toward[3], bool left_arm, bool chain_a,
                         Mat4* target);

    // A game-world point in the head frame the hand script uses (OpenXR
    // axes in metres: x right, y up, -z forward). camera: the rig's camera
    // before the head (rows right, down, forward, position, as
    // god_hand_grip_to_world takes it); head_view: vr_head_to_game_view;
    // units_per_metre: world_scale * the view's factor. The exact inverse of
    // placing a hand at that offset from the head. False if the camera does
    // not invert.
    // How many of her instance's matrices a carried draw must move: every
    // matrix any draw of the instance can read. A draw may pass a model
    // whose own bones + blended joints (`drawn_total`) are fewer than her
    // own model's (`hers_total`) while its vertices still use her layout;
    // moving only the drawn model's range left the rest at the pick-up
    // spot. The larger of the two, clipped to `limit`; 0 if neither is
    // usable.
    int carry_matrix_range(int drawn_total, int hers_total, int limit);

    // Segments (4x4 float matrices, 16 floats each) whose `now` differs from
    // `written` (bit for bit), of `total`. Up to `max_first` of their indices
    // in `first` (may be null). The carry's check that the game did not
    // rewrite part of her matrices inside its own draw.
    int carry_count_rewritten(const float* written, const float* now, int total,
                              int* first, int max_first);

    // The highest segment index in a model's source vertices (16 bytes each,
    // the segment a short at byte 10), -1 if none. Which matrices a draw of
    // that model reads.
    int carry_highest_vertex_segment(const unsigned char* vertices,
                                     int vertex_count);

    bool world_to_head_frame(const Mat4& camera, const Mat4& head_view,
                             float units_per_metre, const float world[3],
                             float out[3]);
}
