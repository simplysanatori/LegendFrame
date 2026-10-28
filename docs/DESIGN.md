# LegendFrame: single-pass stereo for TRLVR on the Steam Frame (design)

Status: design, 2026-10-28. Target source: RototRobot TRLVR **1.1.0** (commit e3657d0),
read at `~/Games/LegendFrame/src` (branch `steam-frame`, = e3657d0 + scaffolding aabf06d).
All `src/...` citations are 1.1.0 paths in that tree; `dxvk-trlvr/...` is the DXVK fork in
the same tree. Nothing here was built or run. Things I could not verify are marked
**UNVERIFIED**, with the probe that settles them.

## 0. Summary

- Measured (owner sessions 3 and 5, logs in `~/Games/TRLVR/logs`): two-pass stereo runs at
  about 40 fps with "GPU scene" (SteamVR `m_flPreSubmitGpuMs`) at 17 ms. Alternate-eye runs at
  68 fps with GPU scene at 5 ms. The `perf cpu:` lines show where the time goes: proxy draw
  calls take **5.9-10.5 ms per frame in two-pass, and 2.9-5.2 ms of that is the mod's per-eye
  matrix maths**, against **0.5-1.6 ms and 0.0 ms in alternate-eye**. The submit wait
  (TransferSurface waiting for the GPU) is 6-8 ms. The time goes into the per-draw
  submission chain, which runs once per eye, and into the mod's own per-eye maths. The GPU
  itself is not the limit.
- **Chosen architecture: SPS-VI (single-pass stereo by viewport index).** Each scene draw
  from the game becomes **one** D3D9 draw in DXVK and **one** `vkCmdDraw*`, with
  `instanceCount x2`. A vertex-shader epilogue in DXVK writes
  `ViewportIndex = InstanceIndex & 1`. The VS reads `c0..c3` from a per-view bank
  (left/right) instead of the API registers. Two viewports cover the two halves of the
  **existing double-wide render target**. The images, textures, StretchRect copies, MSAA
  resolve, overlays and both submit backends stay exactly as they are.
- Layered multiview (`VK_KHR_multiview`, `viewMask=0b11`, 2-layer RT) was the brief's
  suggestion. I **defer** it to an optional M6: §3.2 explains why. In short, the
  double-wide image is consumed by about 25 mod overlay passes, the water half-res split, the
  reflection twin and screen-copy split, the MSAA resolve and both submit paths. Multiview
  would mean rewriting all of them, and it does not save any more of the CPU time we are
  actually short of.
- The per-view transform reproduces the current two-pass maths exactly. The proxy already
  computes a **complete replacement c0..c3 per eye** (it does not post-multiply clip space),
  so we ship both replacements to the GPU as a bank and select on the view. That is
  bit-identical to today's two-pass images.

## 1. How two-pass stereo works today (1.1.0)

### 1.1 Surfaces and sizing
- `CreateDevice` asks the runtime for the per-eye size (`vr_render_target_size`,
  `src/vr/vr_session.cpp:3639`, now via `xr_recommended_size`). It records the eye size
  (`stereo_set_eye_size`, `src/proxy/proxy_d3d9.cpp:238`) and then **doubles the back-buffer
  width** (2W x H, same block). `Reset` does the same (`src/proxy/proxy_device.cpp`
  ~107-151).
- Every RT/depth surface that is exactly one eye in size is widened to 2W
  (`stereo_widen`, `src/vr/stereo.cpp:71-81`, called from `CreateTexture` with
  RENDERTARGET, `CreateRenderTarget` and `CreateDepthStencilSurface`,
  `src/proxy/proxy_device.h:115,120,121`). Shadow maps and half-res post targets are left
  alone.
- MSAA is capped by `[vr] msaa` (`msaa_clamp`, `src/proxy/msaa_cap.h`; CreateDevice
  `proxy_d3d9.cpp:154`).
- The device is forced to `D3DCREATE_MULTITHREADED` (`proxy_d3d9.cpp:250-258`), so **every**
  D3D9 call takes DXVK's device lock. Each extra call per draw therefore also costs a lock
  round trip under FEX.

### 1.2 Scene classification
`drawing_the_scene()` (`src/vr/vr_session.cpp:46`) returns true when:
- RT0 is exactly the scene size (2W x H). RT0 size is tracked from `SetRenderTarget`,
  `proxy_device.cpp:~214`.
- or the water-reflection pass is active (`reflection_stereo_pass_active`).
- or the PCWaterEffect half-res mask is bound (RT = W x H/2 while
  `water_stereo_active()`, hook at 0x0061AF30, `src/vr/water_stereo.cpp:21`).

Shadow maps, render-to-texture and post targets of other sizes are **mono**: one draw with
the game's matrices. Culling is never done per eye. The game's CPU culling runs once per
frame against a widened frustum: `cull_fov`, and in first person the **union of both eye
frusta** plus offsets (`volume_half_angles`, `src/vr/cull.cpp` ~176-215). Single-pass
changes nothing here.

### 1.3 Where the per-eye matrix comes from
- The game uploads its camera in **VS constants c0..c3**. These are either a pure projection
  (view-space geometry such as Lara's skinned mesh) or a full world-view-projection.
  `ProxyDevice::SetVertexShaderConstantF` (`src/proxy/proxy_device.cpp:177-207`) intercepts
  `StartRegister==0 && Count==4`. It records the game's matrix (`vr_note_c0`,
  `vr_session.cpp:366`) and uploads a centre/current-eye substitute (`vr_substitute_c0`,
  `:777`).
- At draw time, `vr_substitute_c0_for_eye(game_c0, out, eye)` (`vr_session.cpp:846`)
  builds a **full replacement c0..c3 for that eye**:
  - UI/HUD/movie/loading/screen-wipe branch (`:882` onward): per-eye frusta from the
    runtime tangents, world-locked menu anchor, `hud_follow`, world markers with convergence
    shear `converge.m[2][0..1] = -offset*s/depth`.
  - PDA and Mission Prep pinned mappings (`mission_prep_pinned`, ~`:800`).
  - Pure projection (`if (gp.valid)`, `:1258`):
    `paused_head * vr_eye_matrix(eye, gp)`, with first-person hand near=4 and board-scale
    hands.
  - WVP (`:1390-1470`): recover the draw's projection, then
    `M * P^-1 * paused_head * vr_eye_matrix(eye, P)`.
  - Sky pinning: `sky_pinned_draw` (`:581`) re-centres camera-pinned draws on the eye with
    no eye separation, via `translation(delta) * M * P^-1 * ...` (`:1445`).
- `vr_eye_matrix` (`:3794`) is `[g_head_view *] vr_eye_projection(eye, game)`. The eye
  frusta and eye-to-head poses now come from the runtime every frame
  (`vr_set_eye_views`, `:295`).
- Fixed-function transforms are **not** made per eye. `SetTransform` only records the
  projection for the ImGui detector (`proxy_device.cpp` SetTransform). FF draws in the scene
  are drawn identically into both halves.

### 1.4 The duplicated calls per scene draw
`dispatch_draw` (`src/proxy/proxy_device.h:194-414`) handles three cases.

1. **Reflection pass** (`:336`): left eye into the game's target, right eye into a twin
   target (`reflection_stereo_bind_twin`), with a per-eye c0 each time. This is two draws
   into **two different surfaces**.
2. **Scene, `passes == 2`** (`stereo_passes`, `stereo.cpp:83`: same-frame on, scene target,
   game c0 known). The loop at `proxy_device.h:375-402` does this for each pass:
   - `stereo_begin_pass` (`stereo.cpp:102-137`): `SetViewport` to the eye half
     (X = pass*eye_w, Y=0, W=eye_w, H=eye_h, **the game's MinZ/MaxZ kept**; the eye size
     comes from `vr_stereo_target_eye_size`, `vr_session.cpp:498`, which also splits the
     water half-res target), then `SetVertexShaderConstantF(0, c0_eye, 4)`;
   - `hands_in_front` (diorama): another `SetViewport` with MinZ=0, MaxZ=0.02
     (`proxy_device.h:383-392`);
   - `reflection_stereo_eye_textures(p)` (`:395`): the right eye **swaps SetTexture** to the
     reflection twin, and both eyes swap the WaterFX screen copy to their own half
     (`src/vr/reflection_stereo.cpp`, eye_textures);
   - `issue()`, the real draw.
   
   After the loop it calls `reflection_stereo_restore_textures` and `stereo_end_draw`, which
   restore c0 to the uploaded value and the game viewport (`stereo.cpp:139-157`).
   
   Net cost per scene draw: **2 draws + 2-4 SetViewport + 3 SetVertexShaderConstantF
   (+ texture swaps)**, each going through FEX into the DXVK frontend under the device lock.
   Each also invalidates DXVK's draw batching (`d3d9_device.cpp:3103-3110`), costs 2
   constant-buffer uploads and 2 viewport CS commands on the CS thread, and causes 2x
   `vkCmdDraw` + state through the wow64 thunks.
3. **Mono** (`:405-413`): one draw. `vr_draw_c0_override` (`vr_session.cpp:376`) may patch
   c0 for interface draws in alternate-eye mode.

Subtleties the new path must keep:
- If one eye's substitution fails, c0 keeps the previous value. The other eye's matrix, or
  the uploaded one, carries over (`stereo_begin_pass` only uploads on success).
- `sky_pinned_draw` counts **calls**, not draws (its learning window `kSkyLearnCalls=32` is
  "one per eye per upload", `vr_session.cpp:544`).
- `binocular_zoom` (`:3811`) updates an EMA **on every call** of `vr_eye_projection`.

### 1.5 Submit (1.1.0)
Once per frame, `vr_submit_frame` (`src/vr/vr_submit.cpp:5115`) runs on the side-by-side
path (`:5145-5195`). It draws about 25 mod overlays straight into the double-wide back
buffer, each with per-eye-half viewports or quads (`draw_comfort_vignette`,
`draw_settings_panel`, `draw_picture_adjust`, ...). It then resolves MSAA
(`resolved_back_buffer`, `:4755`, StretchRect into a 1x copy) and calls
`submit_side_by_side`, which goes to `submit_surfaces` (`:4648`):
- **OpenVR**: `submit_openvr` (`:4611`). `prepare_openvr` calls
  `TransferSurface(surface, TRUE)`, which **waits for GPU idle** (`dxvk-trlvr/src/d3d9/d3d9_vr.cpp`
  TransferSurface), then `GetVRDesc`. `ovr_submit` (`src/vr/ovr_runtime.cpp:595-640`)
  submits the same VkImage twice with bounds (0..0.5 / 0.5..1).
- **OpenXR**: one swapchain image 2W x H (`oxr_swapchain_create`, `src/vr/xr_runtime.cpp:1019`,
  `arraySize = 1` at `:1070`). `CopySurfaceToImage` (new in the 1.1.0 DXVK fork) copies the
  frame in through DXVK's context and waits when `xr_copy_wait=1`. `xrEndFrame` gets a
  projection layer with two `subImage` rects (`imageArrayIndex = 0`, `:1169`).

## 2. DXVK fork (dxvk-trlvr) facts

- Version: `meson.build` declares **3.1.0** and the Frame log prints `DXVK: 3.1.0`. The tree
  has the **DXVK 3.x architecture**:
  - D3D9 shaders are compiled by **dxbc-spirv** (`subprojects/dxbc-spirv/sm3/sm3_converter.cpp`
    into an IR, then lowered by `D3D9ShaderLowerLegacyInputPass` in
    `src/d3d9/d3d9_shader.cpp:13-1090`). There is **no `dxso_compiler.cpp`** any more.
  - The FF VS is a prebuilt GLSL ubershader (`src/d3d9/shaders/d3d9_fixed_function_vert.vert`,
    wrapped in `d3d9_fixed_function.cpp:25-62`).
  - Push data is split into fixed blocks (`D3D9SharedPushData`/`D3D9VsPushData`/
    `D3D9FfvsPushData`, `d3d9_state.h:37-88`; limits 256 B total, **40 B per stage**,
    `src/dxvk/dxvk_limits.h:22-25`).
  - Viewport count is dynamic (`VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT`, `dxvk_graphics.cpp:777`,
    `DxvkContext::setViewports(n, ...)` `dxvk_context.cpp:2789`).
  
  **UNVERIFIED**: an exact match against an upstream v3.1.0 tag. That needs a diff against
  upstream git, which was out of scope offline. Do it in M1 (`git diff v3.1.0 --
  dxvk-trlvr`).
- Fork modifications found (grep for TRL markers, plus the 1.0.0 to 1.1.0 diff):
  - `src/d3d9/d3d9_vr.h/.cpp`: `IDirect3DVR9`. Its **IID changed in 1.1.0**
    (`b1e5c0a4-...`), so the proxy and DXVK must ship as a pair. Methods: GetVRDesc,
    TransferSurface, Lock/UnlockDevice, Lock/UnlockSubmissionQueue, WaitDeviceIdle, plus
    (1.1.0) GetDeviceDesc, CopySurfaceToImage and ReleaseExternalImages. 1.1.0 also fixed
    `GetVRDesc`'s QueueFamilyIndex (it was the queue index) and the COM refcount.
  - `src/d3d9/d3d9.def`: export `Direct3DCreateVR9 @39`.
  - `src/d3d9/d3d9_device.h:231`: `friend class D3D9VR`.
  - `src/util/config/config.cpp:667`: a `trl_vr.exe` profile (`cachedWriteOnlyBuffers`).
  - `src/dxvk/dxvk_openxr.cpp`: Vulkan extension lists taken from the env vars
    `TRLVR_XR_VK_{INSTANCE,DEVICE}_EXTENSIONS`. The proxy fills them for OpenXR
    (`xr_runtime.cpp:774`) **and for OpenVR** (`ovr_runtime.cpp:123-184`, from
    `GetVulkan*ExtensionsRequired`).
  - **Removed in 1.1.0**: `src/dxvk/dxvk_openvr.cpp/.h` (DXVK's own OpenVR extension
    provider) and `include/openvr/`.
- Features already enabled on the Frame, from the shipped DLL's log
  (`~/Games/TRLVR/logs/trl_vr_d3d9.log`, Turnip Mesa 26.2.99): `multiViewport 1`,
  `shaderOutputViewportIndex 1`, `shaderOutputLayer 1`, `multiDrawIndirect 1`,
  `drawIndirectFirstInstance 1`. `vk11.multiview` is **not** requested by DXVK at all
  (`dxvk_device_info.cpp` lists no `multiview`). The host Turnip binary
  (`/usr/lib/libvulkan_freedreno.so`, deckard-mesa 26.3.0_devel) contains the strings
  `VK_KHR_multiview`, `VK_QCOM_multiview_per_view_viewports`/`render_areas`,
  `VK_EXT_shader_viewport_index_layer` and `VK_EXT_vertex_attribute_divisor`. String
  presence does not prove exposure, but the log lines do for the VI features.
  `~/Games/LegendFrame/build-m1/vulkan-caps.txt` did **not** exist when I wrote this.

Relevant DXVK paths for the design:
- Draws: `DrawPrimitive` `d3d9_device.cpp:3023` (instanceCount 1 at `:3049`),
  `DrawIndexedPrimitive` `:3072` (`GetInstanceCount()` `:3099`, `:7360`), `DrawPrimitiveUP`
  `:3135`, `DrawIndexedPrimitiveUP` `:3185`. Consecutive draws are batched into one
  multi-draw when no state changes in between.
- `PrepareDraw` `:7366` calls `BindViewportAndScissor` (`:6836`, one viewport,
  `ctx->setViewports(1,...)` `:6900`) and `UpdateShaderConstants<VS>` (`:6001`; the static
  CBV is copied by the layout, `layout->copyConstantData` `:6074`). It also handles push data
  (`UpdatePushData` `:6678`).
- VS constant lowering: `setupStaticCbv` (`d3d9_shader.cpp:266`) and `lowerLegacyConstInput`
  (`:346`). The static path maps `cN` to a struct member through the compacted layout. The
  dynamic path (relative addressing) loads from `VSDynamicConstants`, clamped by
  `vs.floatCount`.
- Shader cache: `D3D9CommonShader` calls `createCachedShader(ShaderKey.toString(), ...)`
  (`d3d9_shader.cpp:1188-1196`). Converted IR is cached by **key**, so any change to the
  lowering must change the key.
- SPIR-V builtins: `eViewportIndex` maps to `ViewportIndex` plus `MultiViewport` and
  `ShaderViewportIndex` caps (`subprojects/dxbc-spirv/spirv/spirv_builder.cpp:975`).
  `eInstanceId` maps to `InstanceIndex - BaseInstance` (`:946`). **There is no `eViewIndex`
  builtin in the IR** (`ir/ir.h:641-760`), so multiview would need dxbc-spirv changes.

## 3. Chosen architecture

### 3.1 SPS-VI: what happens for one scene draw
1. The proxy decides the draw is a stereo scene draw (same predicate as `stereo_passes()==2`)
   and that it is single-pass eligible (§5.2).
2. The proxy computes `c0_L` and `c0_R` with the **existing**
   `vr_substitute_c0_for_eye(game_c0, ., EyeLeft/EyeRight)`. It applies the same failure
   carry-over rule as `stereo_begin_pass` and calls the functions in the same order (L then
   R), so the side effects (sky learning, zoom EMA) match.
3. The proxy calls `IDirect3DVRStereo9::SetStereoDraw(c0_L, c0_R, eye_w, eye_h)`. This is
   sticky: the proxy skips the call when both matrices and the size are unchanged since the
   last call.
4. The proxy calls `issue()` **once**.
5. In DXVK, when stereo is armed, `PrepareDraw` does three things:
   - binds **2 viewports**. Each is the D3D9 viewport rule applied to the eye rect
     `{eye*eye_w, 0, eye_w, eye_h}` with the game's MinZ/MaxZ, including DXVK's 0.5 offset
     and the scissor-rect intersection per viewport. This is exactly what two
     `SetViewport` calls produce today.
   - sets the VS push flag `stereo=1`.
   - appends the 8-vec4 bank to the VS static CBV upload.
   
   The draw is emitted with `instanceCount = 2*n`.
6. VS epilogue: `view = stereo ? (InstanceId & 1) : 0`, `ViewportIndex = view`. Every read of
   c0..c3 becomes `stereo ? bank[view*4 + i] : c[i]`. The API c0..c3 state is never touched,
   so nothing has to be restored (two-pass needed `stereo_end_draw` for that).
7. Mono draws (anything not armed): one viewport and `stereo=0`. Shader behaviour is
   identical to today, and they cost one extra select per c0 read.

Result per scene draw: 1 proxy-to-DXVK draw call plus at most 1 small private call, instead
of about 8 calls. One constant upload instead of 2. One `vkCmdDrawIndexed` instead of 2.
Draw batching works again for runs of draws with the same state.

GPU work is the same as today: the VS runs twice and the same pixels are shaded. The GPU was
never the limit. Turnip's binning with a VS-written ViewportIndex is **UNVERIFIED** for
cost, so it is probe P3.

### 3.2 Why not layered multiview first
Multiview would make the scene RT a 2-layer W x H image. Everything that today treats the
scene as one 2W-wide 2D image would then need new code paths:
- about 25 `draw_*` overlays in `vr_submit.cpp`, which draw per-eye halves by viewport, quad
  or scissor;
- the water half-res split (`vr_stereo_target_eye_size`);
- the WaterFX screen-copy split (`reflection_stereo_note_copy`, StretchRect of halves);
- the reflection twin;
- the MSAA resolve (`resolved_back_buffer`);
- `TransferSurface` and `GetVRDesc` (OpenVR bounds);
- `CopySurfaceToImage` (OpenXR);
- the game's own post passes that sample the scene texture as a `sampler2D`.

In DXVK it would also need:
- 2-layer image creation for "stereo-widened" surfaces, `2D_ARRAY` views and RT binding
  with a layer count;
- `viewMask` in rendering info and in the pipeline key (3.x code not inspected in detail:
  **UNVERIFIED** where);
- per-layer StretchRect and resolve;
- a new `eViewIndex` IR builtin and `MultiView` capability in dxbc-spirv;
- enabling `vk11.multiview`.

The CPU saving is the same as SPS-VI: one frontend draw and one vkCmdDraw. The only extra
win would be on the GPU (Turnip hardware multiview / multi-position VS), and the GPU is not
the bottleneck. So multiview costs much more effort and risk for no gain on the measured
problem. It stays as an M6 option, and the interface below is shaped so that "view index
source" can later switch from `InstanceId&1` to `ViewIndex`.

### 3.3 Per-view transform: bank versus clip-space post-transform
The brief's `clip_eye = (P_e V_e V_m^-1 P_m^-1) * clip_mono` is mathematically fine. It is
not what the mod does, though, and it would not reproduce the same images:
- several UI branches build matrices that are **not** `X * c0_mono`;
- sky draws change the world translation;
- first-person hands change the near plane;
- it needs an invertible c0 (UI/ortho c0 are not);
- it adds float round-off on the right eye.

The bank carries the proxy's own per-eye results, so it is exact by construction. It also
covers shaders that use c0..c3 rows for something other than position. Those shaders are
treated identically today, because two-pass rewrites the same registers. FF draws get no
bank (FF ignores c0) and fall back to two-pass (§5.2). That matches today's behaviour
exactly with no FF shader work.

## 4. Proxy <-> DXVK private interface

A **new** COM interface that `QueryInterface` returns from the existing `IDirect3DVR9` object.
A DXVK that does not implement it returns `E_NOINTERFACE`, and the proxy then keeps two-pass.
That way a 1.1.0 stock DXVK paired with the new proxy still works.

```cpp
// dxvk-trlvr/src/d3d9/d3d9_vr.h (addition)
struct D3D9_VR_STEREO_CAPS { UINT Version; BOOL ViewportIndex; BOOL Multiview; UINT MaxBankVec4; };
MIDL_INTERFACE("<new uuid>") IDirect3DVRStereo9 : public IUnknown {
  // Before the game creates any shader: compiles every VS with the stereo epilogue
  // (key-suffixed). Returns D3DERR_INVALIDCALL if VS were already created.
  virtual HRESULT STDMETHODCALLTYPE EnableSinglePassStereo(UINT Mode /*1=viewport index*/,
                                                           D3D9_VR_STEREO_CAPS* pCaps) = 0;
  // Arm stereo for following draws (sticky) with per-view replacements for VS c0..c3
  // (D3D register layout, 16 floats each) and the eye rect size in pixels of the current RT.
  virtual HRESULT STDMETHODCALLTYPE SetStereoDraw(const float* pC0Left, const float* pC0Right,
                                                  UINT EyeWidth, UINT EyeHeight) = 0;
  virtual HRESULT STDMETHODCALLTYPE ClearStereoDraw() = 0;            // back to mono
  virtual HRESULT STDMETHODCALLTYPE GetStereoStats(UINT* pStereoDraws, UINT* pRejected) = 0;
};
```

- **Lock and threading**: same rules as the other `IDirect3DVR9` methods
  (`D3D9DeviceLock`). These are called on the game's render thread between D3D9 calls.
- **Stereo target**: no image is designated. "Stereo" is a draw property, not an image
  property, because the proxy already knows which RT is the scene (`drawing_the_scene`). The
  eye rect is passed explicitly. That covers both the 2W x H scene and the W x H/2 water
  mask, where the eye is (W/2) x (H/2).
- **MSAA**: nothing changes. The scene stays one 2W multisampled image, and
  `resolved_back_buffer` resolves it with one StretchRect. The two viewports never overlap,
  so per-eye coverage is unaffected.
- **State blocks / Reset**: `Reset` clears the armed state. `SetStereoDraw` is not recorded
  by state blocks (the game uses PUREDEVICE).

## 5. Changes file by file

### 5.1 DXVK (`dxvk-trlvr/`)
- `src/d3d9/d3d9_vr.h/.cpp`: add `IDirect3DVRStereo9` (implemented on `D3D9VR`, reachable
  through QueryInterface). Forward to new `D3D9DeviceEx` members: `m_stereo.enabled`,
  `armed`, `eyeW/H`, `bank[32]` floats, and counters.
- `src/d3d9/d3d9_state.h`: new `D3D9StereoPushData { uint32_t flags; }`, VS stage, at
  `Offset = sizeof(D3D9VsPushData)+sizeof(D3D9FfvsPushData)` (20). It fits the 40 B VS block
  and moves no existing offset, so the FF GLSL push layout is untouched. Add a
  `PushDataStereo` dirty flag and handle it in `UpdatePushData` (`d3d9_device.cpp:6678`).
- `src/d3d9/d3d9_shader.h`: `D3D9ShaderOptions.stereoMode` (fix the `static_assert` at
  `:39`). It must be folded into `ShaderKey` before `createCachedShader`
  (`d3d9_shader.cpp:1188`), for example a `"_sps1"` suffix, so cached mono IR is never
  reused.
- `src/d3d9/d3d9_shader.cpp` (`D3D9ShaderLowerLegacyInputPass`, VS only, when `stereoMode`):
  - declare a push-data member for the stereo flags next to `setupVsPushData` (`:247`);
  - in `setupStaticCbv` (`:266`), append **8 vec4 members** ("vrBank") after the existing
    members (struct case), or 8 more `uvec4` elements (dword-array case). Record their
    member index. Make sure there is always a static CBV when stereo is on, even if
    `totalCount` was 0;
  - in `lowerLegacyConstInput` (`:346`), float + static path: for `srcIndex < 4` rewrite the
    load to `Select(stereoFlag, BufferLoad(bank, view*4+srcIndex), original)`. Float +
    dynamic path: if the clamped index is < 4 and stereo, select the bank element the same
    way. `view` is recomputed at each use from `InputLoad(eInstanceId) & 1`, because uses can
    sit in SM3 subroutines (other IR functions);
  - entry-point prologue: declare `DclOutputBuiltIn(u32, eViewportIndex)` and store `view`.
    **UNVERIFIED**: whether the sm3 converter's output lowering tolerates an extra builtin
    output. Check with `spirv-dis` of a dumped shader (`DXVK_SHADER_DUMP_PATH`) in M2.
- `src/d3d9/d3d9_device.cpp`:
  - `UpdateShaderConstants<VS>` (`:6001`): when the stereo bank is dirty, mark the VS
    constants dirty. After `copyConstantData` (`:6074`), memcpy 128 B of bank to the bank
    offset (static size grows by 128 B for stereo-compiled shaders).
  - `BindViewportAndScissor` (`:6836`): when armed, build 2 `DxvkViewport` from the eye
    rects (same cf/zBias/scissor code, factored into a helper) and call
    `setViewports(2, ...)`. Arm/disarm sets `ViewportScissor` dirty.
  - Draw entry points: when armed, `instanceCount = 2` for non-instanced draws (`:3049`,
    `:3099`, `:3170`, `:3236`, `:3361`). If armed and `m_vbSlotTracking.instanced`, keep the
    single instance, count a rejection and log once. The proxy avoids this case anyway.
  - `Reset`/`ResetState`: disarm.
- `src/d3d9/d3d9_fixed_function.cpp`, `shaders/*.vert`: **no change** in M2 (FF falls
  back). Optional M5: FF epilogue in GLSL with the same push flag.
- `src/dxvk/*`: **no change** for SPS-VI. The backend already supports n viewports, dynamic
  viewport count, and VS ViewportIndex with the features enabled.
- Queries: occlusion queries spanning a stereo draw count both eyes' samples, which is what
  two-pass does (both passes sit inside the query). Clears are not duplicated today and stay
  single (they clear the game's 2W viewport). StretchRect, resolve, the swapchain and the
  mirror window present are untouched.
- `dxvk.conf` kill switch: `d3d9.trlvrSinglePass = False` makes `EnableSinglePassStereo`
  fail (the proxy logs it and uses two-pass).

### 5.2 Proxy (`src/`)
- `src/common/config.h/.cpp`: `[vr] stereo_mode = twopass | singlepass` (default `twopass`
  until M5 sign-off, then `singlepass`), `[vr] stereo_stats = 0|1`. `stereo_same_frame=0`
  keeps meaning alternate-eye and overrides both.
- `src/proxy/proxy_d3d9.cpp`: right after `vr_submit_init(device)` (`:298`) and before
  returning to the game, query `IDirect3DVRStereo9` and call `EnableSinglePassStereo`. Log
  the result. Shaders are created later by the game, so the variant is in place in time.
- `src/vr/stereo.cpp/.h`: new `stereo_single_pass_eligible()` and
  `stereo_issue_single(device, issue)`. The latter computes L/R with the exact failure
  carry-over, applies `hands_in_front` (game viewport MinZ/MaxZ set before, restored after),
  arms sticky stereo, issues once and counts stats. Keep `stereo_begin_pass` and
  `stereo_end_draw` for fallback.
- `src/proxy/proxy_device.h` `dispatch_draw` (`:375-402`): if `passes > 1 &&
  single_pass_on && eligible`, call `stereo_issue_single`. Otherwise use the current loop.
  When a mono draw follows a stereo draw, call `ClearStereoDraw()` (sticky state is cleared
  in `dispatch_draw`'s mono branch and before the reflection branch).
- **Eligibility** (each draw that fails it falls back to two-pass, counted by reason):
  - a programmable VS is bound (`imgui_state().vertex_shader`, `proxy_device.h` SetVertexShader);
  - no per-eye texture swap is needed. A new `reflection_stereo_draw_needs_eye_textures()`
    returns true when a bound stage holds the reflection texture (twin present) or the
    WaterFX copy (split active), using the same tests as `reflection_stereo_eye_textures`;
  - no stream instancing is active (track `SetStreamSourceFreq`, `proxy_device.h:448`, which
    is a passthrough today);
  - not the reflection pass (`proxy_device.h:336` branch stays two-pass into twin surfaces).
- `sample_hand_projection` (debug): log the bank matrices, since the actual c0 per eye can
  no longer be read back.
- `SetVertexShaderConstantF` (`proxy_device.cpp:177`): unchanged in M3. In M4 the
  centre-eye substitution there can become lazy (it is only needed by mono or fallback
  draws).
- Water reflection and shadow maps stay as they are: the reflection uses twin surfaces,
  shadow maps are mono. UI/HUD/menus/overlays/ImGui (`imgui_overlay`, `overlay.cpp`) are
  unchanged. HUD draws on the scene target take the single-pass path with their per-eye UI
  matrices, which is the same output.
- Runtime A/B: a `tune.cpp` hotkey (proposal: Shift+F6) toggles single-pass and two-pass
  live. This works because every VS is compiled stereo-capable and two-pass simply never
  arms.

### 5.3 M4: shared per-eye maths (proxy CPU)
The 2.9-5.2 ms/frame of matrix maths **does not go away** with single-pass, because we
still need two matrices per draw. M4 adds `vr_substitute_c0_stereo(in, outL, outR)` in
`vr_session.cpp`:
- compute the eye-independent prefix once. `M*P^-1*paused_head` is left-associative in the
  current code, so sharing it is bit-exact;
- cache `vr_eye_matrix(eye, gp)` per frame, keyed by gp and pose generation;
- move the `binocular_zoom` EMA to once per frame;
- advance `sky_pinned_draw`'s call counter by 2.

Prove it with `src/test/mathtest.cpp`: compare the dual function bit for bit against two
single calls over captured c0 sets. This also speeds up the existing two-pass path.

## 6. Runtimes (1.1.0 abstraction) and what single-pass implies

- `[vr] vr_runtime = auto | openxr | openvr` (`src/common/config.cpp:38,889`;
  `vr_runtime.cpp:84`). `auto` tries OpenXR first, which needs `openxr_loader.dll` beside
  the game (delay-loaded) and a runtime that starts, and falls back to OpenVR. Every
  `xr_*` call is forwarded to `oxr_*`/`ovr_*` (`src/vr/vr_backend.h`).
- **OpenVR** (`ovr_runtime.cpp`): Vulkan extensions come from the compositor through the env
  vars. Submit is in place: TransferSurface(wait), GetVRDesc, and two `Submit` calls with
  bounds (`ovr_submit` `:595`). Pacing is `WaitGetPoses` (`:519`).
- **OpenXR** (`xr_runtime.cpp`): a swapchain is created from `GetDeviceDesc`. Each frame the
  proxy calls `xrWaitFrame`/`xrBeginFrame`, acquires, `CopySurfaceToImage`s the 2W image
  (wait if `xr_copy_wait=1`), releases, and calls `xrEndFrame` with two subImage rects.
- **On the Frame**: Proton 11 ARM64 has **no 32-bit wineopenxr**, so `auto` ends on
  **OpenVR**. The design assumes **OpenVR first**. Set `vr_runtime = openvr` in the Frame
  ini so it does not probe OpenXR at start-up (and do not ship `openxr_loader.dll`). Proton
  Experimental ARM64 does have a 32-bit wineopenxr, so OpenXR is an M6 experiment there:
  `vr_runtime=openxr`, plus `openxr_loader.dll`, plus SteamVR as the OpenXR runtime.
- **SPS-VI implications**: none for either runtime. The submitted image is still the 2W
  double-wide image, the bounds and rects stay as they are, and `TransferSurface` and
  `CopySurfaceToImage` stay as they are.
- **Layered-multiview implications (M6 only)**:
  - OpenVR would need either `Submit_VulkanTextureWithArrayData` with
    `VRVulkanTextureArrayData_t{m_unArrayIndex, m_unArraySize=2}` (one image, two Submits),
    which is **UNVERIFIED** through Proton's vrclient Vulkan translation, or two layer blits
    into a 2W staging image (two copies per frame; `prepare_openvr` stays as it is).
  - OpenXR would take it more naturally: `arraySize = 2` (`xr_runtime.cpp:1070`),
    `imageArrayIndex = eye` (`:1169`), and a layer parameter added to `CopySurfaceToImage`.
- **Frame pacing lever (both runtimes, optional M5)**: the TransferSurface /
  `xr_copy_wait` GPU wait serialises CPU and GPU (6-8 ms "submit"). It is a separate
  experiment (`TransferSurface(surface, FALSE)` behind a key) and is risky. The compositor
  must see finished work; DXVK's `LockSubmissionQueue` already flushes and synchronises the
  CS thread before Submit.

## 7. Risks, ranked

1. **Expected gain smaller than hoped.** Single-pass removes the duplicate DXVK frontend
   and CS work but not the mod maths or the game's own 6-10 ms update. Estimate: M3 alone
   about 50-58 fps, M3+M4 about 60-68 fps (alternate-eye reached 68 with zero mod maths).
   *Probe*: compare M3 `perf cpu` draw-call ms with session 3/5. If short, do M4 and the
   FEX knobs (`FEX_MAXINST`, Proton Experimental ARM64).
2. **IR lowering correctness in dxbc-spirv** (SM3 subroutines, dynamic indexing, the
   extra output builtin, the CBV struct member limit `ir::Type::MaxStructMembers`).
   *Mitigation*: the stereotest harness (PLAN M2-C) renders known matrices and reads back
   pixels. Check dumped SPIR-V with `spirv-val`. Every shader that fails lowering is
   compiled mono and its draws fall back (log `D3D9VR: stereo lowering failed for <key>`).
3. **Hidden per-eye state the proxy changes between passes.** Known: viewport,
   c0, texture swaps, hands depth range. Anything else (a future 1.1.x feature calling
   `SetRenderState` per pass) would be lost. *Mitigation*: the code review checklist covers
   `dispatch_draw` and `stereo_begin_pass` only. A debug key renders frame N two-pass and
   frame N+1 single-pass; `F12` captures both for a pixel diff (screenshot helper in
   `vr_submit` mirror).
4. **Toolchain**: the proxy uses 187 `__try` blocks in 11 files and MSVC attributes. The M1 mingw build
   currently fails in `vr_submit.cpp:992` with the `seh_compat.h` shim
   (`build-m1/logs/build-all.log`), and the 1.1.0 source list (overlay, xr/ovr runtimes,
   godmode, physics, openxr) is not yet in `container-build.sh`. *Mitigation*: the M1 lane
   finishes the shim, or switches the proxy to clang-cl plus an xwin MSVC SDK. DXVK already
   builds with mingw.
5. **Turnip cost of VS-written ViewportIndex with binning/LRZ, and instanceCount=2.**
   *Probe P3*: compare GPU scene in alternate-eye against single-pass on the same spot. If
   GPU time rises a lot, try `ViewportIndex` through `VK_QCOM_multiview_per_view_viewports`
   (needs multiview, M6).
6. **Instanced or FF scene draws are common** (unknown). *Probe*: M3 stats line fallback
   reasons. If FF is large, add the FF GLSL epilogue (M5).
7. **IID/ABI mismatch** between proxy and DXVK (1.1.0 already changed the IID). *Mitigation*:
   separate interface, version field in caps, fallback to two-pass.
8. **IR cache poisoning**. *Mitigation*: key suffix. Delete the DXVK state cache on first
   install.

## 8. Test plan on the Frame

Conditions: start each session cool. Session 4 was thermally throttled, so check fancontrol
temps before launching. Use the shortcut (trl_vr.exe, LAA), SteamVR 72 Hz, resolution
override 1440, `vr_runtime=openvr`, `stereo_same_frame=1`, and the same save and spot for
every A/B (the first Bolivia courtyard).

Perf targets, from the `perf:` and `perf cpu:` lines, 60 s medians:

| mode | fps | GPU scene | draw-call CPU | submit |
|---|---|---|---|---|
| two-pass (baseline, self-built M1) | about 40 | about 17 ms | 6-10 ms | 6-8 ms |
| single-pass M3 | >= 50 | <= 9 ms | <= 4 ms | <= 5 ms |
| single-pass + M4 | 60-72 (cap 72) | <= 7 ms | <= 2.5 ms | |

Visual checks, single-pass against two-pass via the live toggle:
- convergence and world scale on near and far geometry;
- HUD position and follow;
- world markers (lock-on, grab hand);
- menus: world-locked pause, PDA, Mission Prep;
- cutscenes (theatre on and off) and movies/loading screens;
- sky (no slide, at infinity);
- water: mask, WaterFX copy, reflection on, if enabled;
- shadows (mono);
- first-person hands, board/diorama hands-in-front;
- MSAA 2 edges;
- ImGui settings panel and picture adjust;
- comfort vignette.

Fallback switches: Shift+F6 live; `[vr] stereo_mode=twopass`; `dxvk.conf`
`d3d9.trlvrSinglePass = False`; restore the shipped DLLs (PLAN rollback).

## 9. Build and packaging

- Container `legendframe-mingw` (Fedora 43, mingw32-gcc, meson, glslang;
  `~/Games/LegendFrame/toolchain/Containerfile`, `build.sh`, `container-build.sh`). DXVK
  builds (`build-m1/out/dist/d3d9_dxvk.dll`). The proxy needs the SEH fix and the 1.1.0
  source list from `build.bat`.
- Deliverables beside the game: `d3d9.dll` (proxy), `d3d9_dxvk.dll`, `trlvr_launcher.exe`,
  `trlvr.ini` keys. Proxy and DXVK always ship as a pair. `openvr_api.dll` is unchanged, and
  there is no `openxr_loader.dll` on Proton 11.
- Privacy: keep `/PDBALTPATH`-like hygiene (no absolute paths in DLLs; mingw `-s`). Commits
  as simplysanatori noreply.
