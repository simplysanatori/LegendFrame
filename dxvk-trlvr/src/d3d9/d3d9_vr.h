#pragma once

// IDirect3DVR9 -- hands the Vulkan handles behind a D3D9 surface to a VR runtime.
//
// OpenVR cannot be given a Direct3D 9 texture; its Submit takes D3D11, a DXGI
// shared handle, OpenGL, D3D12 or Vulkan, and nothing else. But DXVK *is*
// Vulkan underneath, so the image the game just rendered is already a VkImage.
// This interface exposes it, which makes submission free: no readback, no
// staging copy, and no D3D9Ex shared-surface detour.
//
// D3D9_TEXTURE_VR_DESC deliberately mirrors the leading members of
// vr::VRVulkanTextureData_t, so a filled desc can be memcpy'd or cast straight
// into a Submit call.
//
// The mod now runs on OpenXR, whose swapchain images belong to the runtime:
// the session is created from GetDeviceDesc and each frame is copied in with
// CopySurfaceToImage. GetVRDesc and TransferSurface are the OpenVR-era
// hand-over and are kept for a runtime that takes the image in place.
//
// The design follows the d3d9_vr interface in the L4D2VR and SiN VR DXVK forks.
// This is a fresh implementation against DXVK 3.1, whose device-locking API
// differs from the 3.0.2 those were written for.

#include <d3d9.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>
#undef VK_USE_PLATFORM_WIN32_KHR

struct D3D9_TEXTURE_VR_DESC {
  uint64_t         Image;            // VkImage; OpenVR types it as uint64_t
  VkDevice         Device;
  VkPhysicalDevice PhysicalDevice;
  VkInstance       Instance;
  VkQueue          Queue;
  uint32_t         QueueFamilyIndex;

  uint32_t         Width;
  uint32_t         Height;
  VkFormat         Format;
  uint32_t         SampleCount;
};

// What an OpenXR session is created from (XrGraphicsBindingVulkanKHR).
struct D3D9_VR_DEVICE_DESC {
  VkInstance       Instance;
  VkPhysicalDevice PhysicalDevice;
  VkDevice         Device;
  VkQueue          Queue;
  uint32_t         QueueFamilyIndex;
  uint32_t         QueueIndex;       // within the family
};

// A VkImage DXVK does not own: an OpenXR swapchain image. The runtime hands
// these out in COLOR_ATTACHMENT_OPTIMAL and wants them back that way.
struct D3D9_VR_EXTERNAL_IMAGE {
  uint64_t         Image;
  VkFormat         Format;
  uint32_t         Width;
  uint32_t         Height;
};

MIDL_INTERFACE("b1e5c0a4-3f7d-4a92-8c16-2d9e47f0a3b5")
IDirect3DVR9 : public IUnknown {
  // Fills pDesc with the Vulkan handles backing pSurface.
  virtual HRESULT STDMETHODCALLTYPE GetVRDesc(
          IDirect3DSurface9*    pSurface,
          D3D9_TEXTURE_VR_DESC* pDesc) = 0;

  // Moves the surface into TRANSFER_SRC_OPTIMAL, the layout the compositor
  // requires on submit. Pass TRUE to also wait for the resource to be idle.
  virtual HRESULT STDMETHODCALLTYPE TransferSurface(
          IDirect3DSurface9* pSurface,
          BOOL               waitResourceIdle) = 0;

  virtual HRESULT STDMETHODCALLTYPE LockDevice() = 0;
  virtual HRESULT STDMETHODCALLTYPE UnlockDevice() = 0;

  // VkQueue is externally synchronised and DXVK's own submit thread uses it,
  // so it has to be held across an OpenVR submit or the two race.
  virtual HRESULT STDMETHODCALLTYPE LockSubmissionQueue() = 0;
  virtual HRESULT STDMETHODCALLTYPE UnlockSubmissionQueue() = 0;

  virtual HRESULT STDMETHODCALLTYPE WaitDeviceIdle() = 0;

  // --- OpenXR ---------------------------------------------------------------
  // OpenXR swapchain images belong to the runtime, so a frame is copied into
  // one rather than submitted in place. The copy is recorded through DXVK's
  // own context, which keeps its layout and lifetime tracking correct and
  // puts the copy on the queue in order with the frame it reads.

  virtual HRESULT STDMETHODCALLTYPE GetDeviceDesc(
          D3D9_VR_DEVICE_DESC* pDesc) = 0;

  // Copies all of pSurface to (DstX, DstY) in the external image. With
  // WaitForCopy the call returns once the GPU has finished it. Must not be
  // called with the submission queue locked.
  virtual HRESULT STDMETHODCALLTYPE CopySurfaceToImage(
          IDirect3DSurface9*            pSurface,
    const D3D9_VR_EXTERNAL_IMAGE*       pDst,
          INT                           DstX,
          INT                           DstY,
          BOOL                          WaitForCopy) = 0;

  // Drops every wrapper made for an external image, after the GPU is done
  // with them. Call before the runtime destroys the swapchain.
  virtual HRESULT STDMETHODCALLTYPE ReleaseExternalImages() = 0;
};

// NOTE: the locks above only lock anything if the device was created with
// D3DCREATE_MULTITHREADED. Without it DXVK's AcquireLock hands back an empty
// guard. Tomb Raider: Legend does not ask for it (its flags are
// HARDWARE_VERTEXPROCESSING | PUREDEVICE), so the proxy forces the flag on at
// CreateDevice rather than this fork weakening DXVK's locking.

#ifdef _MSC_VER
struct __declspec(uuid("b1e5c0a4-3f7d-4a92-8c16-2d9e47f0a3b5")) IDirect3DVR9;
#else
__CRT_UUID_DECL(IDirect3DVR9, 0xb1e5c0a4, 0x3f7d, 0x4a92, 0x8c, 0x16, 0x2d, 0x9e, 0x47, 0xf0, 0xa3, 0xb5);
#endif

// --- Single-pass stereo (LegendFrame SPS-VI), contract v1 --------------------
//
// FROZEN CONTRACT (docs/PLAN.md "Contract frozen at the start of M2-B"). The
// proxy (M3-P), the DXVK device side (M2-B), the DXVK shader side (M2-A) and
// the harness (M2-C) all code against this block. Change it only through the
// overseer, and bump D3D9_VR_STEREO_INTERFACE_VERSION and the IID if the ABI
// changes. This header is also compiled into the 32-bit proxy (MSVC/mingw),
// so it carries no DXVK-internal types; every struct here is made of 4-byte
// members only and has the same layout on i686 and x86_64.
//
// How one stereo draw works (DESIGN.md section 3.1):
//  - The proxy arms the device with SetStereoDraw(c0L, c0R, eyeW, eyeH). This
//    is sticky until ClearStereoDraw, Reset, or another SetStereoDraw.
//  - Every draw while armed (programmable VS bound, no stream instancing) is
//    emitted ONCE with instanceCount = 2 and two viewports, one per eye rect
//    {eye * eyeW, 0, eyeW, eyeH}, eye 0 = left. Each viewport gets the game's
//    MinZ/MaxZ, DXVK's usual half-pixel offset and the scissor test per eye,
//    i.e. exactly what two SetViewport calls produce in two-pass today.
//  - The VS (compiled with the stereo variant) computes
//        view = (stereo flag set) ? (InstanceId & 1) : 0
//    writes ViewportIndex = view, and replaces every read of c0..c3 with
//        flag ? bank[view * 4 + i] : c[i]
//    The API c0..c3 state is never touched.
//
// VS push data (DXVK side, d3d9_state.h D3D9StereoPushData), VS stage:
//   byte offset D3D9_VR_STEREO_PUSH_OFFSET (= sizeof(D3D9VsPushData) 8
//   + sizeof(D3D9FfvsPushData) 12 = 20), one uint32 "flags":
//     bit 0 D3D9_VR_STEREO_FLAG_ARMED: this draw is a stereo draw.
//   Other bits are reserved and zero. No existing push offset moves; the FF
//   GLSL push layout is untouched.
//
// Bank (VS static constant buffer, CbvIndex::VSStaticConstants):
//   8 vec4 (128 bytes) appended after the shader's mono static constants:
//     bank[0..3] = left  c0..c3 (D3D register order, row i = 4 floats of ci)
//     bank[4..7] = right c0..c3
//   Byte offset of bank[0] in the CBV, for a given VS constant layout:
//     16 * (Fs + I + ceil(B / 4))
//   with Fs = static float vec4 count (0 when the float layout is dynamically
//   indexed: those floats live in VSDynamicConstants), I = int vec4 count,
//   B = bool count (each a computeConstantCount(0) of the compacted layout).
//   That is exactly the size of the mono declaration in both setupStaticCbv
//   cases (struct members, or the uvec4 array whose bools are packed four per
//   vec4). A stereo VS always declares the static CBV (bank only, at offset 0,
//   when it has no other static constants); the device always allocates the
//   static CBV + 128 B for every programmable VS once stereo is enabled, so
//   the bank is always inside the bound range, armed or not. The bank content
//   is only guaranteed while armed; unarmed reads must be discarded (the
//   select on the push flag does that).
//
// Shader variant: D3D9ShaderOptions gains "uint8_t stereoMode" (0 = off,
// 1 = D3D9_VR_STEREO_MODE_VIEWPORT_INDEX), set by the device for every shader
// created after EnableSinglePassStereo. It is part of the hashed options, and
// the shader side appends D3D9_VR_STEREO_KEY_SUFFIX to the cache name passed
// to createCachedShader, so mono IR is never reused for a stereo variant.
// Stereo lowering applies to programmable vertex shaders only. A VS whose
// lowering fails is compiled mono (logged by the shader side).

#define D3D9_VR_STEREO_INTERFACE_VERSION    1u

#define D3D9_VR_STEREO_MODE_QUERY           0u  // EnableSinglePassStereo: fill caps only, change nothing
#define D3D9_VR_STEREO_MODE_VIEWPORT_INDEX  1u  // SPS-VI

#define D3D9_VR_STEREO_PUSH_OFFSET          20u // bytes, VS stage push data
#define D3D9_VR_STEREO_PUSH_SIZE            4u
#define D3D9_VR_STEREO_FLAG_ARMED           0x1u

#define D3D9_VR_STEREO_BANK_VEC4            8u
#define D3D9_VR_STEREO_BANK_BYTES           128u
#define D3D9_VR_STEREO_KEY_SUFFIX           "_sps1"

struct D3D9_VR_STEREO_CAPS {
  UINT Version;        // D3D9_VR_STEREO_INTERFACE_VERSION
  BOOL ViewportIndex;  // SPS-VI usable on this device (multiViewport + shaderOutputViewportIndex)
  BOOL Multiview;      // layered multiview path (always FALSE in v1)
  UINT MaxBankVec4;    // D3D9_VR_STEREO_BANK_VEC4
};

MIDL_INTERFACE("d77445fd-6e00-4860-9a84-4020d143004c")
IDirect3DVRStereo9 : public IUnknown {
  // Selects the stereo shader variant for every vertex shader created from
  // now on. Call before the game creates any shader (right after CreateDevice).
  //   Mode = D3D9_VR_STEREO_MODE_VIEWPORT_INDEX: enable. Returns
  //     D3DERR_INVALIDCALL if vertex shaders were already created,
  //     D3DERR_NOTAVAILABLE if the device lacks the features, if the shader
  //     side does not implement the variant, or if dxvk.conf has
  //     d3d9.trlvrSinglePass = False. Calling it again once enabled is D3D_OK.
  //   Mode = D3D9_VR_STEREO_MODE_QUERY: only fills pCaps.
  // pCaps may be null; when given it is filled in every case.
  virtual HRESULT STDMETHODCALLTYPE EnableSinglePassStereo(
          UINT                  Mode,
          D3D9_VR_STEREO_CAPS*  pCaps) = 0;

  // Arms stereo for the following draws (sticky). pC0Left / pC0Right are the
  // per-view replacements for VS c0..c3, 16 floats each in D3D register
  // layout (c0.xyzw, c1.xyzw, c2.xyzw, c3.xyzw). EyeWidth / EyeHeight are the
  // eye rect in pixels of the currently bound render target (2W x H scene:
  // W x H; W x H/2 water mask: W/2 x H/2). Unchanged matrices and size cost
  // no upload. Returns D3DERR_INVALIDCALL if not enabled or on null/zero args.
  virtual HRESULT STDMETHODCALLTYPE SetStereoDraw(
    const float*                pC0Left,
    const float*                pC0Right,
          UINT                  EyeWidth,
          UINT                  EyeHeight) = 0;

  // Back to mono draws. Cheap and idempotent.
  virtual HRESULT STDMETHODCALLTYPE ClearStereoDraw() = 0;

  // Running totals since device creation (never reset, wrap at 2^32):
  // pStereoDraws = draws emitted as single-pass stereo, pRejected = armed
  // draws drawn mono instead (stream instancing or no programmable VS).
  // Either pointer may be null.
  virtual HRESULT STDMETHODCALLTYPE GetStereoStats(
          UINT*                 pStereoDraws,
          UINT*                 pRejected) = 0;
};

#ifdef _MSC_VER
struct __declspec(uuid("d77445fd-6e00-4860-9a84-4020d143004c")) IDirect3DVRStereo9;
#else
__CRT_UUID_DECL(IDirect3DVRStereo9, 0xd77445fd, 0x6e00, 0x4860, 0x9a, 0x84, 0x40, 0x20, 0xd1, 0x43, 0x00, 0x4c);
#endif

// Exported from d3d9.dll. Resolve with GetProcAddress and pass the live device.
extern "C" HRESULT __stdcall Direct3DCreateVR9(
        IDirect3DDevice9* pDevice,
        IDirect3DVR9**    ppInterface);
