# Investigation: god-mode carry leaves vertices behind ("stretch")

Status: read-only analysis, 2026-10-28, tree `steam-frame` d517a08. No build, no launch.
Report: LegendFrame 0.1.1, single-pass, OpenVR. Picking Lara up in god mode and moving her
leaves some vertices where they were, stretched to the carried model. Session log
(trlvr.log, 09:13-09:18): picked up by the head at 259.5 s and 267.2 s ("dangling from her
right wrist (segment 82)"), released at 265.3 s and 276.9 s. Stats over the carries: single
~265k per 5 s, fallback 0, mono 0, dxvk rejected 0.

## Verdict
Single-pass is very unlikely to be the cause. Lara is skinned **on the CPU** by the game, so
every vertex of her draw goes through one c0..c3 (a pure projection). A wrong bank moves the
whole draw. It cannot pull a subset of vertices back to where she was picked up. That needs
wrong vertex *data*, and the vertex data path is the same in two-pass and single-pass. Expect
the owner's two-pass A/B to show the same stretch. The ranked causes below are all in the
1.1.0 carry code (upstream), except E.

## (1) How god mode moves her
- **Game memory only.** `carry_apply` (src/vr/camera_head.cpp:11496-11756) rewrites her
  instance matrices (`instance+0x80`, 109 bones + 112 blended) around the real
  `DRAW_DrawInstance` call, and `carry_restore` (:11758) copies the saved set back. The call
  order is in `detour_draw_instance_body` (:15386-15411): carry, then (when not carried) wall
  touch and push, then soft parts, rear, the game's draw, restore, then
  `third_person_draw_hands` on the **restored** matrices.
- Posed path (Lara, held by the head or an arm): `xform *= body` on all real segments,
  limb pendulums, then `pose_commit` (:9152), which recomputes the blended joints. Every
  blended joint in trlvr_lara_skeleton.txt references bones 0-108, so none is skipped.
  Other paths: a palm stand or linked gear multiply every matrix by one rigid matrix
  (:11580-11589, :11738-11746).
- Physics: `rear_soft_apply` writes the model's **source vertex shorts** for the draw and
  puts them back afterwards (:13996, :14078; shared by every instance of the model).
  `third_person_soft_frame` (:12679) writes bones 54-57 and keeps them for the frame. The
  push uses `PoseWork` (:14789). `src/vr/godmode/*` and `src/vr/physics/*` are pure maths
  with no device calls.
- **No VS constants, no Lock, no ProcessVertices** anywhere in god mode or physics. The only
  c0..c3 writers are the proxy paths (proxy_device.cpp:213/222, proxy_device.h:390/397/482/487,
  stereo.cpp:131/227/476).
- GPU side: the game skins on the CPU at 0x00408CC0 into `D3D_GetVB` allocations
  (BONEVERTEX, view space). The character VS applies c0..c3 as a pure projection
  (camera_head.cpp:5738-5754, :13153-13161). The D3D draw is issued later from the retail
  render list, which is why the drawable tagging hooks exist (`D3D_SetIndicesPtr`).

## Ranked causes (A-D apply to both modes)
**A. The game's draw rewrites part of her matrices after `carry_apply` (most likely).**
The prime suspect is the ponytail: bones 31-39 carry 304 vertices, and there is a
zero-vertex twin chain at 43-52 that looks like a simulation chain. If `DRAW_DrawInstance`
rebuilds those matrices from world-space state before it skins, they stay at her real
position, and the faces between head and ponytail stretch back to the pickup spot. This
fits "stay in position and stretch to where the model is" exactly.
*Test (log, needs a build):* in `detour_draw_instance_body`, keep a copy of what
`carry_apply` wrote. After `g_draw_instance` and before `carry_restore`, compare it with
the matrices and log once per segment: `carry: segment N rewritten inside the draw`.
*Fix:* post-multiply `body` onto the segments the game rewrites, by hooking the builder
that writes them. Alternatively, carry the rigid part in view space by premultiplying c0
for Lara's tagged drawables, and keep only the pose edits in the matrices.

**B. Nested draws are not carried.** A single `static CarrySave` means "a draw nested
inside this one is not carried" (:15388-15393). If her draw draws linked gear inside
itself, that gear stays behind. It would be rigid, not stretched, unless the gear is
skinned partly against her bones. *Test:* log `carry: nested draw of <object name> not
carried`. *Fix:* use a small stack of saves (as `push_apply` does, :14944-14960).

**C. The body's skinned data is overwritten by the un-carried hand copies.**
`third_person_draw_hands` (:10799) skins her model again after `carry_restore`. If its
`D3D_GetVB` allocation overlaps the body's before the render list is drawn, the hand and
forearm vertices of the body show the un-carried pose. *Test:* the allocation log in
`detour_get_bone_vb` (:828-852) only runs while ledge hanging. Enable it during a carry
and compare base/count for the body and hand draws. *Fix:* give the hand draws their own
allocation, or make sure the ring does not wrap within a frame.

**D. Physics springs see the pickup as motion.** The rear springs (`rear_frame`, :13783)
are capped at cm scale and reset when she jumps more than 0.5 m. Soft parts are capped at
12 degrees. That can make a ripple, but not a metre-long stretch. *Test:* `[vr] body_rear=0`,
then `body_soft=0`, then `body_push=0`, one at a time. *Fix:* reset the springs when
`pluck_drawn` changes.

**E. Single-pass only: `def` c0..c3 in a statically indexed VS (low).** The static path
replaces c0..c3 with the bank without checking `isShaderDefined`
(dxvk-trlvr/src/d3d9/d3d9_shader.cpp:689-697). The dynamic path masks those registers
(:501-521). In two-pass, a `def` beats the API value. This would affect a whole draw, not a
subset of its vertices. *Fix:* apply the same mask in the static path.

## (2) Single-pass interactions checked (no defect found)
- The bank is recomputed for every draw from `vr_game_c0()` (stereo.cpp:389-463). The sticky
  test compares both eyes' matrices plus the eye size (:440-460). A draw can only see a stale
  bank if it bypasses `dispatch_draw` while the device is armed. The proxy's own FF draws
  (vr_submit) would be rejected and counted, and the rejected count is 0. ImGui disarms first
  (proxy_device.h:499). The reflection branch disarms (:361).
- ProcessVertices and SWVP pass straight through the proxy (proxy_device.h:512, :194). DXVK
  never arms ProcessVertices (d3d9_device.cpp:7052-7057), so it uses the API c0, which is the
  same value in both modes. Lara is not pre-transformed through ProcessVertices: the game
  skins her itself, and the VS reading c0..c3 as a projection is the intended substitution.
- (3) Lara has no GPU skinning, so no bone palette at c0. Relative reads into c0..c3 hit the
  bank exactly as they hit the API registers in two-pass (d3d9_shader.cpp:739-758). The
  only behaviour difference is E.
- (4) Two-pass puts `vr_draw_c0_restore()` back after each draw (stereo.cpp:218-235).
  Single-pass leaves the API c0 alone and restores only when `g_c0_is_uploaded` is false
  (:465-479). These differ only after a c0..c3 write that bypasses the proxy, such as a
  state block `Apply` (CreateStateBlock passes through, proxy_device.h:177-179). Game reads
  through `GetVertexShaderConstantF` see the same value in both modes. God mode never reads c0.
- (5) The depth range is not part of the sticky key, and it does not need to be. DXVK
  builds both eye viewports from `m_state.viewport`, which `SetViewport` dirties
  (d3d9_device.cpp:7010-7031), so `hands_in_front` (proxy_device.h:420-433) takes effect.
  An eye-size change re-arms (stereo.cpp:441) and re-dirties the viewports
  (d3d9_device.cpp:7206-7211).
- "ui: HUD matrix was not a pure projection; using mono fallback" was logged once at 95.7 s,
  long before the carries (vr_session.cpp:1510). It is the same in both modes. Not relevant.

## Cheap tests for the owner (in order)
1. Panel checkbox or Shift+F6 to two-pass, then pick her up again. If it still stretches,
   it is not single-pass (expected).
2. Note **what** stretches: the ponytail (points to A), gear or holsters (B), hands or
   forearms (C), rear or chest (D).
3. Compare held by the head (posed path) with standing on a palm (rigid path). If it
   happens in both, the cause is the game's matrix rebuild (A), not the posing code.
4. Set `[vr] body_rear=0`, then `body_soft=0`, then `body_push=0`, one at a time (D).
5. Next build: add the A, B and C log lines (one-shot each, cheap) and repeat one carry.
