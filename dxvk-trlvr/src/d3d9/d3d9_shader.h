#pragma once

#include <array>
#include <atomic>
#include <string>

#include <sm3/sm3_parser.h>

#include "../dxvk/dxvk_shader.h"
#include "../dxvk/dxvk_shader_key.h"
#include "../dxvk/dxvk_shader_ir.h"

#include "../util/util_unmap.h"

#include "d3d9_resource.h"
#include "d3d9_shader_analysis.h"
#include "d3d9_util.h"

namespace dxvk {

  struct D3D9ShaderOptions {
    /** Whether the device is configured to do SWVP.
     * This must only be true for vertex shaders.
     * It results in significantly larger amounts of shader constants.
     */
    bool isSWVP;

    /** Whether to emulate d3d9 float behaviour using clampps
     * True:  Perform emulation to emulate behaviour (ie. anything * 0 = 0)
     * False: Don't do anything.
     */
    D3D9FloatEmulation d3d9FloatEmulation;

    /** Always use a spec constant to determine sampler type (instead of just in PS 1.x)
     * Works around a game bug in Halo CE where it gives cube textures to 2d/volume samplers
     */
    bool forceSamplerTypeSpecConstants;

    /** LegendFrame single-pass stereo variant (vertex shaders only).
     * 0: off, the shader compiles exactly as upstream.
     * 1: D3D9StereoShader::ModeViewportIndex (SPS-VI), see d3d9_vr.h.
     * Part of the hashed options; the cache name also gets the
     * D3D9StereoShader::KeySuffix so mono IR is never reused.
     */
    uint8_t stereoMode = 0u;
  };

  static_assert(sizeof(D3D9ShaderOptions) == 4u);

  /**
   * \brief Single-pass stereo shader constants
   *
   * Mirrors the frozen contract in d3d9_vr.h (D3D9_VR_STEREO_*), kept here
   * so the shader code does not need the Win32/Vulkan interface header.
   */
  struct D3D9StereoShader {
    /// D3D9_VR_STEREO_MODE_VIEWPORT_INDEX
    static constexpr uint8_t  ModeViewportIndex = 1u;
    /// D3D9_VR_STEREO_FLAG_ARMED, bit in D3D9StereoPushData::flags
    static constexpr uint32_t FlagArmed = 0x1u;
    /// D3D9_VR_STEREO_BANK_VEC4: left c0..c3, then right c0..c3
    static constexpr uint32_t BankVec4Count = 8u;
    /// Number of API float registers replaced per view (c0..c3)
    static constexpr uint32_t BankRegCount = 4u;
    /// D3D9_VR_STEREO_KEY_SUFFIX
    static constexpr const char* KeySuffix = "_sps1";

    /**
     * \brief Byte offset of bank[0] in the VS static constant buffer
     *
     * 16 * (Fs + I + ceil(B / 4)), i.e. the size of the mono part of the
     * static CBV declaration (see setupStaticCbv in d3d9_shader.cpp).
     */
    static uint32_t computeBankOffset(const D3D9ConstantBufferCopy* layout);

    /**
     * \brief Number of vertex shaders whose stereo lowering failed
     *
     * Process-wide running total. Each of these shaders was compiled
     * mono and must not be drawn armed (see D3D9CommonShader::IsStereoCapable).
     */
    static uint32_t getLoweringFailureCount();
  };

  /**
   * \brief Shared stereo compile status of one shader module
   *
   * Shared between all copies of a D3D9CommonShader and the converter,
   * which may run lazily on a compiler worker thread.
   */
  class D3D9StereoShaderStatus : public RcObject {

  public:

    enum Status : uint32_t {
      Unknown = 0u,
      Stereo  = 1u,
      Mono    = 2u,
    };

    /// Records the result. Logs and counts a failure exactly once.
    void setResult(const std::string& name, bool stereo, const char* reason);

    Status get() const {
      return Status(m_status.load(std::memory_order_acquire));
    }

  private:

    std::atomic<uint32_t> m_status = { Unknown };

  };

  struct D3D9ShaderCreateInfo {
    DxvkIrShaderCreateInfo irCreateInfo;

    D3D9ShaderOptions shaderOptions;
  };

  /**
   * \brief Shader resource mapping
   *
   * Helper class to compute backend resource
   * indices for D3D9 binding slots.
   */
  struct D3D9ShaderResourceMapping {
    enum CbvIndex : uint32_t {
      VSClipPlanes            = 0u,
      VSFixedFunction         = 1u,
      VSVertexBlendData       = 2u,
      VSStaticConstants       = 3u,
      VSDynamicConstants      = 4u,
      PSShared                = 5u,
      PSStaticConstants       = 6u,

      Count
    };

    static constexpr uint32_t computeTextureBinding(D3D9ShaderType shaderType, uint32_t index) {
      auto base = (shaderType == D3D9ShaderType::VertexShader) ? FirstVSSamplerSlot : 0u;
      return base + index;
    }

    static constexpr uint32_t getSwvpBufferIndex() {
      return caps::MaxTextures;
    }

    static constexpr std::pair<VkShaderStageFlags, uint32_t> getTextureSlotInfo(uint32_t index) {
      // Sampler slot and binding indices match 1:1, see above
      return std::make_pair(IsVSSampler(index) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, index);
    }
  };

  /**
   * \brief Common shader object
   * 
   * Stores the compiled SPIR-V shader and the SHA-1
   * hash of the original DXBC shader, which can be
   * used to identify the shader.
   */
  class D3D9CommonShader {

  public:

    D3D9CommonShader() = default;

    D3D9CommonShader(
            D3D9DeviceEx*         pDevice,
      const DxvkShaderHash&       ShaderKey,
            D3D9ShaderAnalysis&&  Analysis,
      const D3D9ShaderCreateInfo& ModuleInfo,
      const void*                 pShaderBytecode);


    Rc<DxvkShader> GetShader() const {
      return m_shader;
    }

    std::string GetName() const {
      return m_shader->debugName();
    }

    const D3D9InputSignature& GetInputSignature() const {
      return m_analysis.GetInputSignature();
    }

    const D3D9ShaderConstantsInfo& GetConstantsInfo() const { return m_analysis.GetConstantsInfo(); }
    const D3D9ImmediateConstantsInfo& GetImmediateConstants() const { return m_analysis.GetImmediateConstants(); }

    const D3D9ConstantBufferCopy* GetConstantLayout() const {
      return m_analysis.GetConstantLayout();
    }

    D3D9ShaderMasks GetShaderMask() const { return D3D9ShaderMasks{ m_analysis.GetSamplerMask(), m_analysis.GetRenderTargetMask() }; }

    dxbc_spv::sm3::ShaderInfo GetInfo() const { return m_analysis.GetShaderInfo(); }

    /**
     * \brief Whether the stereo variant was requested for this shader
     *
     * True for vertex shaders created with D3D9ShaderOptions::stereoMode set.
     * The shader then has the stereo CBV layout (bank appended) unless the
     * lowering failed, see \c IsStereoCapable.
     */
    bool IsStereoRequested() const {
      return m_stereoStatus != nullptr;
    }

    /**
     * \brief Whether draws with this shader may be armed
     *
     * True only if the stereo lowering succeeded, i.e. the compiled shader
     * writes ViewportIndex and reads c0..c3 from the bank. A shader whose
     * lowering failed (or that was loaded mono from the IR cache) returns
     * false; the device must draw it mono / two-pass and count a rejection.
     * May compile the shader on first call if it has not been compiled yet.
     */
    bool IsStereoCapable() const;

    /**
     * \brief Byte offset of the stereo bank in the VS static CBV
     *
     * Only meaningful if \c IsStereoRequested. The device must bind at least
     * this offset + 128 bytes of the static CBV for such shaders.
     */
    uint32_t GetStereoBankOffset() const {
      return D3D9StereoShader::computeBankOffset(GetConstantLayout());
    }

  private:

    D3D9ShaderAnalysis          m_analysis;
    Rc<DxvkShader>              m_shader;
    Rc<D3D9StereoShaderStatus>  m_stereoStatus;

  };

  /**
   * \brief Common shader interface
   * 
   * Implements methods for all D3D11*Shader
   * interfaces and stores the actual shader
   * module object.
   */
  template <typename Base>
  class D3D9Shader : public D3D9DeviceChild<Base> {

  public:

    D3D9Shader(
            D3D9DeviceEx*        pDevice,
            MemoryFilePool*      pAllocator,
      const D3D9CommonShader&    CommonShader,
      const void*                pShaderBytecode,
            uint32_t             BytecodeLength)
      : D3D9DeviceChild<Base>( pDevice )
      , m_shader             ( CommonShader )
      , m_bytecode           ( *pAllocator, BytecodeLength )
      , m_bytecodeLength     ( BytecodeLength ) {
      std::memcpy(m_bytecode.map(), pShaderBytecode, BytecodeLength);
      m_bytecode.unmap();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) {
      if (ppvObject == nullptr)
        return E_POINTER;

      *ppvObject = nullptr;

      if (riid == __uuidof(IUnknown)
       || riid == __uuidof(Base)) {
        *ppvObject = ref(this);
        return S_OK;
      }

      if (logQueryInterfaceError(__uuidof(Base), riid)) {
        Logger::warn("D3D9Shader::QueryInterface: Unknown interface query");
        Logger::warn(str::format(riid));
      }

      return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE GetFunction(void* pOut, UINT* pSizeOfData) {
      if (pSizeOfData == nullptr)
        return D3DERR_INVALIDCALL;

      if (pOut == nullptr) {
        *pSizeOfData = m_bytecodeLength;
        return D3D_OK;
      }

      uint32_t copyAmount = std::min(*pSizeOfData, m_bytecodeLength);
      std::memcpy(pOut, m_bytecode.map(), copyAmount);
      m_bytecode.unmap();
      return D3D_OK;
    }

    const D3D9CommonShader* GetCommonShader() const {
      return &m_shader;
    }

  private:

    D3D9CommonShader m_shader;

    MemoryFileRegion m_bytecode;
    uint32_t         m_bytecodeLength;

  };

  // Needs their own classes and not usings for forward decl.

  class D3D9VertexShader final : public D3D9Shader<IDirect3DVertexShader9> {

  public:

    D3D9VertexShader(
            D3D9DeviceEx*        pDevice,
            MemoryFilePool*      pAllocator,
      const D3D9CommonShader&    CommonShader,
      const void*                pShaderBytecode,
            uint32_t             BytecodeLength)
      : D3D9Shader<IDirect3DVertexShader9>( pDevice, pAllocator, CommonShader, pShaderBytecode, BytecodeLength ) { }

  };

  class D3D9PixelShader final : public D3D9Shader<IDirect3DPixelShader9> {

  public:

    D3D9PixelShader(
            D3D9DeviceEx*        pDevice,
            MemoryFilePool*      pAllocator,
      const D3D9CommonShader&    CommonShader,
      const void*                pShaderBytecode,
            uint32_t             BytecodeLength)
      : D3D9Shader<IDirect3DPixelShader9>( pDevice, pAllocator, CommonShader, pShaderBytecode, BytecodeLength ) { }

  };

  /**
   * \brief Shader module set
   * 
   * Some applications may compile the same shader multiple
   * times, so we should cache the resulting shader modules
   * and reuse them rather than creating new ones. This
   * class is thread-safe.
   */
  class D3D9ShaderModuleSet : public RcObject {
    
  public:
    
    HRESULT GetShaderModule(
            D3D9DeviceEx*           pDevice,
      const DxvkShaderHash&         ShaderKey,
            D3D9ShaderAnalysis&&    ShaderAnalysis,
      const D3D9ShaderCreateInfo&   ModuleInfo,
      const void*                   pShaderBytecode,
            D3D9CommonShader*       pShader);
    
  private:
    
    dxvk::mutex m_mutex;
    
    std::unordered_map<
      DxvkShaderHash,
      D3D9CommonShader,
      DxvkHash, DxvkEq> m_modules;
    
  };

  template<typename T>
  const D3D9CommonShader* GetCommonShader(const T& pShader) {
    return pShader != nullptr ? pShader->GetCommonShader() : nullptr;
  }

}
