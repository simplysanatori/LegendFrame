# LegendFrame plan (single-pass stereo, SPS-VI)

Read DESIGN.md first. Source is TRLVR 1.1.0 (e3657d0) at `~/Games/LegendFrame/src`.
Build is `~/Games/LegendFrame/toolchain` (podman `legendframe-mingw`). Outputs go to
`~/Games/LegendFrame/build-*`. Rules:
- Lanes never edit the same file at the same time.
- Each lane works on its own branch off `steam-frame` and is merged by the overseer.
- Nothing is installed into the game directory except by the M5 lane.

## File ownership (exclusive while the lane is open)

| lane | files |
|---|---|
| M1-T toolchain | `toolchain/*`, `build-m1/*`, build patches only (no `src/` edits except an agreed `src/common/seh_compat.h`) |
| M2-A DXVK shader | `dxvk-trlvr/src/d3d9/d3d9_shader.cpp`, `d3d9_shader.h` |
| M2-B DXVK device | `dxvk-trlvr/src/d3d9/d3d9_vr.h`, `d3d9_vr.cpp`, `d3d9_device.h`, `d3d9_device.cpp`, `d3d9_state.h`, `d3d9_options.*`, `src/util/config/config.cpp` |
| M2-C harness | `src/test/stereotest.cpp` (new), `toolchain/stereotest.sh` (new) |
| M3-P proxy | `src/proxy/proxy_device.h`, `proxy_device.cpp`, `proxy_d3d9.cpp`, `src/vr/stereo.cpp/.h`, `src/vr/reflection_stereo.cpp/.h`, `src/common/config.h/.cpp` |
| M3-K hotkey/panel | `src/vr/tune.cpp/.h`, `src/proxy/overlay.cpp` (settings panel entry) |
| M4-M maths | `src/vr/vr_session.cpp/.h`, `src/vr/vr_math.*`, `src/test/mathtest.cpp` |
| M5-I install/validate | `~/Games/LegendFrame/install/*`, the game dir, `trlvr.ini`, logs |

Contract frozen at the **start** of M2-B, so the other lanes can code against it:
- `IDirect3DVRStereo9` in `d3d9_vr.h` (DESIGN §4);
- `D3D9StereoPushData` at VS push offset 20;
- the bank is 8 vec4 appended to the VS static CBV, L c0..c3 then R c0..c3;
- shader key suffix `_sps1`.

## M1: reproducible baseline (in progress, toolchain lane)

Accept when:
- the container builds 1.1.0 `d3d9_dxvk.dll`, `d3d9.dll` and `trlvr_launcher.exe` from
  `~/Games/LegendFrame/src`, using the 1.1.0 `build.bat` source list (overlay, xr/ovr/vr
  runtime, menu_*, tr7_menu_hook, finger_input, godmode/, physics/, third_party/openxr
  headers, `openxr_loader` delay-load or LoadLibrary-only);
- the `__try` shim compiles all 11 files that use it, or the proxy is built with clang-cl + xwin;
- `upstream-diff.txt` lists the fork's diff against upstream DXVK v3.1.0 (settles DESIGN
  §2 UNVERIFIED);
- a headset session with self-built 1.1.0 two-pass, `vr_runtime=openvr`, stays within ±5 %
  of 1.0.0 shipped fps at the same spot. Log lines to expect: `vr: using OpenVR / SteamVR`
  and `submit: first side-by-side frame accepted`.

Lanes:
- **M1-T** (Sonnet, high; already running): finish the proxy build.
- **M1-V** (Sonnet): `vulkaninfo` to `build-m1/vulkan-caps.txt`. Confirm `multiViewport`,
  `maxViewports`, `shaderOutputViewportIndex`, `VK_KHR_multiview` (+`multiviewGeometryShader`
  not needed), `VK_QCOM_multiview_per_view_viewports`, and `VK_EXT_vertex_attribute_divisor`.

## M2: DXVK single-pass core (needs only the DXVK build, which already works)

Accept when:
- with `stereotest.exe` under Proton 11 ARM64 on the Frame (DISPLAY=:1, no headset):
  - a VS reading c0..c3 renders a quad into a 2W x H RT, once with
    `SetStereoDraw(L, R, W, H)` and once as two-pass;
  - the readback images are identical (max abs diff 0) for: a static c0 shader, a
    relative-addressed (a0) shader, a shader with `call` subroutines, MSAA 2x, and the
    scissor test on;
  - mono draws after `ClearStereoDraw` are unchanged;
  - an instanced draw while armed is rejected and logged;
- `spirv-val` passes on dumped stereo shaders;
- DXVK log lines:
  - `D3D9VR: single-pass stereo enabled (viewport index, bank 8 vec4, key _sps1)`
  - `D3D9VR: stereo draw rejected (instanced) -- reported once`
  - `D3D9VR: stereo lowering failed for <key> -- compiled mono`
  - `D3D9VR: single-pass disabled by d3d9.trlvrSinglePass`

Lanes (parallel):
- **M2-B device/interface** (Opus): freeze the header first, then do arming, the bank
  upload in `UpdateShaderConstants`, the two viewports in `BindViewportAndScissor`,
  instanceCount, the push flag, Reset, stats, and the conf switch.
- **M2-A shader lowering** (Opus): `stereoMode` option and key, bank members in
  `setupStaticCbv`, selects in `lowerLegacyConstInput` (static + dynamic), and the
  ViewportIndex prologue.
- **M2-C harness** (Sonnet): `stereotest.cpp` loads `d3d9_dxvk.dll` directly, gets
  `IDirect3DVR9` via `Direct3DCreateVR9`, and QIs to `IDirect3DVRStereo9`. Add a run script
  using the SLR/Proton command from the MCC recipe.

## M3: proxy integration (needs M1 proxy build + M2 header)

Accept when:
- in the headset, `stereo_mode=singlepass` gives identical-looking images to two-pass
  across the DESIGN §8 checklist, with the Shift+F6 live toggle;
- the stats line shows >= 90 % single-pass among stereo draws in gameplay;
- fps is >= 50 at the test spot (1440, 72 Hz, cool start).

Config keys, `[vr]` in `trlvr.ini`:
- `stereo_mode = twopass|singlepass` (default `twopass` until M5 sign-off);
- `stereo_stats = 0|1`;
- `vr_runtime = openvr` (Frame default).

Proxy log lines:
- `stereo: single-pass (viewport index) enabled, DXVK stereo interface v1`
- `stereo: DXVK has no single-pass interface (E_NOINTERFACE) -- two-pass`
- `stereo: single-pass requested but EnableSinglePassStereo failed (0x%08X) -- two-pass`
- every 5 s with `stereo_stats=1`: `stereo stats: single %u, fallback %u (eye-textures %u, fixed-function %u, instanced %u, reflection %u), mono %u, arm calls %u, dxvk rejected %u`
- `stereo: live toggle -> single-pass|two-pass (Shift+F6)`

Lanes:
- **M3-P proxy** (Opus): `stereo_issue_single`, eligibility, sticky arm/clear,
  `hands_in_front`, the `reflection_stereo_draw_needs_eye_textures` predicate,
  `SetStreamSourceFreq` tracking, config, CreateDevice enable, and the debug hand sample.
- **M3-K hotkey/panel** (Sonnet): Shift+F6 toggle, plus an optional settings-panel item.
  Calls only `stereo_set_single_pass(bool)` exported by M3-P (agree the signature in the
  M3-P first commit).

## M4: shared per-eye maths (can start as soon as the M1 proxy build works; independent of M2/M3)

Accept when:
- `mathtest` shows the dual-eye function bit-identical to 2x single calls on >= 200
  captured c0 sets, covering pure projection, WVP, sky, UI, marker, PDA, Mission Prep,
  first-person hands and board hands. Captures come from a `vr_capture_c0=1` debug key
  writing `c0_capture.bin`;
- `perf cpu` "mod matrix math" is down >= 40 % in **both** two-pass and single-pass;
- there is no visual change.

New log line: `stereo: dual-eye substitution on (shared prefix, eye matrix cache, zoom EMA per frame)`.
Key: `[vr] stereo_shared_math = 1` (0 = legacy per-eye calls, for A/B).

Lane:
- **M4-M** (Opus): `vr_substitute_c0_stereo`, a per-frame `vr_eye_matrix` cache, the
  binocular_zoom EMA once per frame, the sky call counter +2, and lazy upload-time
  centre-eye substitution.

## M5: Frame validation, install, defaults

Accept when:
- three cool sessions in single-pass + M4 give a median fps >= 60 at the test spot and no
  regression in the checklist;
- the owner signs off on the look;
- defaults are flipped (`stereo_mode=singlepass`) and the install script is idempotent;
- the memory file is updated.

Lanes:
- **M5-I** (Sonnet): install/uninstall scripts and backups (rollback below), headset
  session log collection, A/B summary table.
- **M5-X** (Sonnet, optional, after sign-off): FEX knobs (`FEX_MAXINST=5000`,
  `FEX_PROFILESTATS=0`) and a Proton Experimental ARM64 A/B. Also, behind
  `[vr] submit_wait = 1|0`, the no-wait TransferSurface experiment. Revert anything that
  shows artefacts.
- **M5-F** (Opus, only if M3 stats show fixed-function fallback > 5 %): FF GLSL epilogue
  (`d3d9_fixed_function_vert.vert` + push flag).

## M6: optional research (not scheduled)

- Layered multiview (DESIGN §3.2/§6): dxbc-spirv `eViewIndex`, `vk11.multiview`,
  `viewMask`, 2-layer stereo surfaces, OpenXR `arraySize=2`, and the OpenVR array submit
  through Proton vrclient. Only if GPU time becomes the limit.
- OpenXR on Proton Experimental ARM64 (32-bit wineopenxr): `vr_runtime=openxr`,
  `openxr_loader.dll`, SteamVR OpenXR runtime; compare pacing with OpenVR.

## Rollback

1. Before the first install, M5-I copies the game dir's `d3d9.dll`, `d3d9_dxvk.dll`,
   `openvr_api.dll`, `trlvr_launcher.exe`, `trl_vr.exe` and `trlvr.ini` to
   `~/Games/LegendFrame/build-m1/shipped/1.0.0/`, with `SHA256SUMS`. These are the
   currently installed 1.0.0 files. The zip's sha256 `fd36ec16…a18` is in
   `~/Games/TRLVR/dl`, so the zip is a second source.
2. Every install writes `~/Games/LegendFrame/install/last-install.txt` (file, old sha, new
   sha) and keeps the previous set in `install/prev/`.
3. Three levels of fallback, fastest first:
   - Shift+F6 in the headset;
   - `[vr] stereo_mode=twopass`;
   - `d3d9.trlvrSinglePass = False` in `dxvk.conf` (in launch options via `DXVK_CONFIG`);
   - `install/rollback.sh`, which restores `shipped/1.0.0` and checks the sha256s.
4. Proxy and DXVK are always swapped together (the IID pairing). Never mix a 1.0.0 proxy
   with a 1.1.0 DXVK.
5. Delete DXVK's shader/state cache files for trl_vr in the game dir/prefix on every swap
   (key-suffix safety net).

## Immediate start

1. **M2-B DXVK device/interface (Opus)**: the header freeze is its first commit.
2. **M2-A DXVK shader lowering (Opus)**: works against the frozen contract above.

Both need only the DXVK container build, which works today (`build-m1/out/dist/d3d9_dxvk.dll`).
M2-C (Sonnet) can follow as soon as the header is frozen. M4-M starts when M1-T has a
working proxy build.
