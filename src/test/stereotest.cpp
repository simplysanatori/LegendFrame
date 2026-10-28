// stereotest.exe -- LegendFrame M2-C harness.
//
// Loads d3d9_dxvk.dll (the DXVK fork) directly from the exe's directory, creates a
// windowed device and renders the same coloured geometry into a 2W x H render target
// twice: (A) two-pass (SetViewport + SetVertexShaderConstantF(0,..) + Draw per eye) and
// (B) single-pass (IDirect3DVRStereo9::SetStereoDraw + one Draw). Both are read back
// and compared (max abs channel diff, count of differing pixels).
//
// No d3dx: the vs_3_0/ps_3_0 bytecode is built in this file by a tiny token emitter.
// Build: toolchain/stereotest.sh. Usage:
//   stereotest.exe [--w N] [--out DIR] [--dump] [--no-enable] [--iid {GUID}] [--only NAME]
// Exit: 0 all cases pass, 1 any failure, 2 no failure but some case not available.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include <cstdarg>

// Console output of a console exe is lost under "proton run" (steam.exe launches it
// without inheriting the handles), so every line also goes to <out>/stereotest.log.
static FILE* g_log = nullptr;
static void P(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
  if (g_log) { va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fflush(g_log); }
}

#ifdef STEREOTEST_USE_DXVK_HEADER
#include "d3d9_vr.h"   // frozen contract from lane M2-B
static GUID stereo_iid() { return __uuidof(IDirect3DVRStereo9); }
#else
// Local copy of the PLAN.md / DESIGN.md section 4 contract, for use before the header
// exists. The IID is supplied with --iid or STEREOTEST_IID (default: all zero, which
// makes QueryInterface fail with E_NOINTERFACE = "not available").
struct D3D9_VR_STEREO_CAPS { UINT Version; BOOL ViewportIndex; BOOL Multiview; UINT MaxBankVec4; };
struct IDirect3DVRStereo9 : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE EnableSinglePassStereo(UINT Mode, D3D9_VR_STEREO_CAPS* pCaps) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetStereoDraw(const float* pC0Left, const float* pC0Right,
                                                  UINT EyeWidth, UINT EyeHeight) = 0;
  virtual HRESULT STDMETHODCALLTYPE ClearStereoDraw() = 0;
  virtual HRESULT STDMETHODCALLTYPE GetStereoStats(UINT* pStereoDraws, UINT* pRejected) = 0;
};
static GUID g_iid_local = {};
static GUID stereo_iid() { return g_iid_local; }
static bool parse_guid(const char* s, GUID* g) {
  unsigned d1, d2, d3, b[8];
  if (!s) return false;
  if (*s == '{') s++;
  if (sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3],
             &b[4], &b[5], &b[6], &b[7]) != 11) return false;
  g->Data1 = d1; g->Data2 = (unsigned short)d2; g->Data3 = (unsigned short)d3;
  for (int i = 0; i < 8; i++) g->Data4[i] = (unsigned char)b[i];
  return true;
}
#endif

// ---------------------------------------------------------------- shader emitter
typedef std::vector<DWORD> Code;
enum { T_TEMP = 0, T_INPUT = 1, T_CONST = 2, T_ADDR = 3, T_OUTPUT = 6, T_COLOROUT = 8, T_LABEL = 18 };
enum { OP_MOV = 1, OP_MUL = 5, OP_DP4 = 9, OP_CALL = 25, OP_RET = 28, OP_LABEL = 30, OP_DCL = 31,
       OP_MOVA = 46 };
enum { U_POSITION = 0, U_TEXCOORD = 5, U_COLOR = 10 };
static DWORD regtok(int type, int num) {
  return 0x80000000u | ((DWORD)(type & 7) << 28) | ((DWORD)(type & 0x18) << 8) | (DWORD)(num & 0x7ff);
}
static const DWORD SWZ_XYZW = 0xE4, SWZ_X = 0x00;
static DWORD dst(int type, int num, DWORD mask = 0xF) { return regtok(type, num) | (mask << 16); }
static DWORD src(int type, int num, DWORD swz = SWZ_XYZW) { return regtok(type, num) | (swz << 16); }
// c[a0.x + off]: src token with the relative bit, followed by the a0.x token.
static void emit_rel_const(Code& c, int off) {
  c.push_back(regtok(T_CONST, off) | (SWZ_XYZW << 16) | 0x2000);
}
static void ins(Code& c, DWORD op, std::initializer_list<DWORD> t) {
  c.push_back(op | ((DWORD)t.size() << 24));
  for (DWORD x : t) c.push_back(x);
}
static void dcl(Code& c, int usage, int idx, DWORD regtoken) {
  ins(c, OP_DCL, {0x80000000u | (DWORD)usage | ((DWORD)idx << 16), regtoken});
}
static void vs_header(Code& c, bool texcoord_in) {
  c.push_back(0xFFFE0300u);   // vs_3_0
  dcl(c, U_POSITION, 0, dst(T_INPUT, 0));
  dcl(c, U_COLOR, 0, dst(T_INPUT, 1));
  if (texcoord_in) dcl(c, U_TEXCOORD, 0, dst(T_INPUT, 2));
  dcl(c, U_POSITION, 0, dst(T_OUTPUT, 0));
  dcl(c, U_COLOR, 0, dst(T_OUTPUT, 1));
}
static void emit_pos_static(Code& c) {
  for (int i = 0; i < 4; i++)
    ins(c, OP_DP4, {dst(T_OUTPUT, 0, 1u << i), src(T_INPUT, 0), src(T_CONST, i)});
}
static void emit_pos_relative(Code& c) {
  for (int i = 0; i < 4; i++) {
    c.push_back(OP_DP4 | (4u << 24));
    c.push_back(dst(T_OUTPUT, 0, 1u << i));
    c.push_back(src(T_INPUT, 0));
    emit_rel_const(c, i);
    c.push_back(regtok(T_ADDR, 0) | (SWZ_X << 16));
  }
}
static void emit_color(Code& c) {   // o1 = v1 * c4 (c4 is outside the stereo bank)
  ins(c, OP_MUL, {dst(T_OUTPUT, 1), src(T_INPUT, 1), src(T_CONST, 4)});
}
static Code build_vs(int kind) {   // 0 static, 1 relative a0, 2 call/ret
  Code c;
  vs_header(c, kind == 1);
  if (kind == 0) {
    emit_pos_static(c); emit_color(c);
  } else if (kind == 1) {
    ins(c, OP_MOVA, {dst(T_ADDR, 0, 1), src(T_INPUT, 2, SWZ_X)});
    emit_pos_relative(c); emit_color(c);
  } else {
    emit_color(c);
    ins(c, OP_CALL, {src(T_LABEL, 0)});
    ins(c, OP_RET, {});
    ins(c, OP_LABEL, {src(T_LABEL, 0)});
    emit_pos_static(c);
    ins(c, OP_RET, {});
  }
  c.push_back(0x0000FFFFu);
  return c;
}
static Code build_ps() {
  Code c;
  c.push_back(0xFFFF0300u);   // ps_3_0
  dcl(c, U_COLOR, 0, dst(T_INPUT, 0));
  ins(c, OP_MOV, {dst(T_COLOROUT, 0), src(T_INPUT, 0)});
  c.push_back(0x0000FFFFu);
  return c;
}

// ---------------------------------------------------------------- scene data
struct Vtx { float p[4]; float col[4]; float tc[4]; };
static const Vtx kVerts[9] = {
  // big triangle, deliberately reaching past +-1 NDC on both sides (viewport edge spill test)
  {{-1.8f,-0.9f,0.30f,1}, {0.1f,0.8f,0.9f,1}, {0,0,0,0}},
  {{ 1.8f,-0.5f,0.30f,1}, {0.9f,0.2f,0.8f,1}, {0,0,0,0}},
  {{ 0.0f, 0.9f,0.30f,1}, {1.0f,1.0f,1.0f,1}, {0,0,0,0}},
  // quad with z varying (perspective-correct interpolation via c3)
  {{-0.6f,-0.6f,0.0f,1}, {1.0f,0.0f,0.0f,1}, {0,0,0,0}},
  {{ 0.6f,-0.6f,1.0f,1}, {0.0f,1.0f,0.0f,1}, {0,0,0,0}},
  {{ 0.6f, 0.6f,1.0f,1}, {0.0f,0.0f,1.0f,1}, {0,0,0,0}},
  {{-0.6f,-0.6f,0.0f,1}, {1.0f,0.0f,0.0f,1}, {0,0,0,0}},
  {{ 0.6f, 0.6f,1.0f,1}, {0.0f,0.0f,1.0f,1}, {0,0,0,0}},
  {{-0.6f, 0.6f,0.0f,1}, {1.0f,1.0f,0.0f,1}, {0,0,0,0}},
};
// rows are c0..c3: oPos.x = dot(v, c0) etc.
static const float kL[16]   = { 0.80f, 0.10f, 0.00f,-0.15f,   0.00f, 0.80f, 0.00f, 0.05f,
                                0.00f, 0.00f, 0.50f, 0.10f,   0.00f, 0.00f, 0.20f, 1.00f };
static const float kR[16]   = { 0.70f,-0.10f, 0.00f, 0.20f,   0.05f, 0.90f, 0.00f,-0.05f,
                                0.00f, 0.00f, 0.40f, 0.20f,   0.00f, 0.00f, 0.15f, 1.00f };
static const float kMono[16]= { 0.30f, 0.00f, 0.00f, 0.00f,   0.00f, 0.30f, 0.00f, 0.00f,
                                0.00f, 0.00f, 0.30f, 0.00f,   0.00f, 0.00f, 0.00f, 1.00f };
static const float kTint[4] = { 1.0f, 0.9f, 0.8f, 1.0f };
static const uint32_t kClear = 0xFF202830u;

// ---------------------------------------------------------------- harness state
struct Img { int w = 0, h = 0; std::vector<uint32_t> px; };
struct Env {
  IDirect3D9* d3d = nullptr;
  IDirect3DDevice9* dev = nullptr;
  IUnknown* vr = nullptr;
  IDirect3DVRStereo9* st = nullptr;
  HRESULT hrQI = E_NOINTERFACE, hrEnable = E_NOINTERFACE;
  int W = 256, H = 256;
  IDirect3DSurface9 *rt1 = nullptr, *rtMS = nullptr, *sys = nullptr;
  IDirect3DVertexShader9* vs[3] = {};
  IDirect3DPixelShader9* ps = nullptr;
  IDirect3DVertexDeclaration9 *decl = nullptr, *declInst = nullptr;
  IDirect3DVertexBuffer9 *vb = nullptr, *vbInst = nullptr;
  IDirect3DIndexBuffer9* ib = nullptr;
  bool msaaOk = false;
  std::string outDir = ".";
  bool dump = false;
};
static Env E;

static bool failhr(const char* what, HRESULT hr) {
  if (SUCCEEDED(hr)) return false;
  P("  !! %s failed: 0x%08lX\n", what, (unsigned long)hr);
  return true;
}
#define TRY(x) do { HRESULT _h = (x); if (failhr(#x, _h)) return false; } while (0)

static bool setup_resources() {
  IDirect3DDevice9* d = E.dev;
  for (int k = 0; k < 3; k++) {
    Code c = build_vs(k);
    TRY(d->CreateVertexShader(c.data(), &E.vs[k]));
  }
  Code p = build_ps();
  TRY(d->CreatePixelShader(p.data(), &E.ps));
  D3DVERTEXELEMENT9 el[] = {
    {0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END()};
  TRY(d->CreateVertexDeclaration(el, &E.decl));
  D3DVERTEXELEMENT9 el2[] = {
    {0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
    {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {1, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    D3DDECL_END()};
  TRY(d->CreateVertexDeclaration(el2, &E.declInst));
  TRY(d->CreateVertexBuffer(sizeof(kVerts), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &E.vb, nullptr));
  void* m;
  TRY(E.vb->Lock(0, 0, &m, 0)); memcpy(m, kVerts, sizeof(kVerts)); E.vb->Unlock();
  float inst[4 * 2] = {0, 0, 0, 0, 0.1f, 0, 0, 0};
  TRY(d->CreateVertexBuffer(sizeof(inst), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &E.vbInst, nullptr));
  TRY(E.vbInst->Lock(0, 0, &m, 0)); memcpy(m, inst, sizeof(inst)); E.vbInst->Unlock();
  TRY(d->CreateIndexBuffer(9 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &E.ib, nullptr));
  TRY(E.ib->Lock(0, 0, &m, 0));
  for (int i = 0; i < 9; i++) ((uint16_t*)m)[i] = (uint16_t)i;
  E.ib->Unlock();
  int w2 = 2 * E.W;
  TRY(d->CreateRenderTarget(w2, E.H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &E.rt1, nullptr));
  if (E.msaaOk)
    TRY(d->CreateRenderTarget(w2, E.H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_2_SAMPLES, 0, FALSE, &E.rtMS, nullptr));
  TRY(d->CreateOffscreenPlainSurface(w2, E.H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &E.sys, nullptr));
  return true;
}

enum Mode { TWO, SINGLE, MONO };
struct Params {
  int vs = 0;
  bool msaa = false, scissor = false;
  Mode mode = TWO;
  bool instanced = false;   // bind instancing streams (SINGLE only; image not compared)
};
struct RenderResult { HRESULT hrArm = S_OK, hrDraw = S_OK; };

static D3DVIEWPORT9 vp(int x, int w, int h) {
  D3DVIEWPORT9 v = {(DWORD)x, 0, (DWORD)w, (DWORD)h, 0.0f, 1.0f};
  return v;
}
static bool issue_draw(const Params& p, HRESULT* hr) {
  IDirect3DDevice9* d = E.dev;
  HRESULT h;
  if (p.instanced) h = d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 9, 0, 3);
  else             h = d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 3);
  if (hr) *hr = h;
  return SUCCEEDED(h);
}

static bool readback(const Params& p, Img& out) {
  IDirect3DDevice9* d = E.dev;
  IDirect3DSurface9* src = E.rt1;
  if (p.msaa) TRY(d->StretchRect(E.rtMS, nullptr, E.rt1, nullptr, D3DTEXF_NONE));
  TRY(d->GetRenderTargetData(src, E.sys));
  D3DLOCKED_RECT lr;
  TRY(E.sys->LockRect(&lr, nullptr, D3DLOCK_READONLY));
  out.w = 2 * E.W; out.h = E.H; out.px.assign((size_t)out.w * out.h, 0);
  for (int y = 0; y < out.h; y++)
    memcpy(&out.px[(size_t)y * out.w], (char*)lr.pBits + (size_t)y * lr.Pitch, (size_t)out.w * 4);
  E.sys->UnlockRect();
  return true;
}

static bool render(const Params& p, Img* out, RenderResult* rr = nullptr) {
  IDirect3DDevice9* d = E.dev;
  const int W = E.W, H = E.H;
  TRY(d->SetRenderTarget(0, p.msaa ? E.rtMS : E.rt1));
  d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  D3DVIEWPORT9 full = vp(0, 2 * W, H);
  TRY(d->SetViewport(&full));
  TRY(d->Clear(0, nullptr, D3DCLEAR_TARGET, kClear, 1.0f, 0));
  d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  d->SetRenderState(D3DRS_LIGHTING, FALSE);
  d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  TRY(d->SetVertexShader(E.vs[p.vs]));
  TRY(d->SetPixelShader(E.ps));
  TRY(d->SetVertexDeclaration(p.instanced ? E.declInst : E.decl));
  TRY(d->SetStreamSource(0, E.vb, 0, sizeof(Vtx)));
  if (p.instanced) {
    TRY(d->SetStreamSource(1, E.vbInst, 0, 16));
    TRY(d->SetIndices(E.ib));
    TRY(d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 2));
    TRY(d->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1));
  }
  TRY(d->SetVertexShaderConstantF(0, kMono, 4));   // "game" c0..c3: unrelated to L/R
  TRY(d->SetVertexShaderConstantF(4, kTint, 1));
  if (p.scissor) {
    RECT rc = {W / 2, H / 4, W + W / 2, H - H / 4};   // straddles the eye boundary
    TRY(d->SetScissorRect(&rc));
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
  }
  TRY(d->BeginScene());
  bool ok = true;
  HRESULT hrDraw = S_OK;
  if (p.mode == TWO) {
    D3DVIEWPORT9 l = vp(0, W, H), r = vp(W, W, H);
    ok = SUCCEEDED(d->SetViewport(&l)) && SUCCEEDED(d->SetVertexShaderConstantF(0, kL, 4)) &&
         issue_draw(p, &hrDraw) &&
         SUCCEEDED(d->SetViewport(&r)) && SUCCEEDED(d->SetVertexShaderConstantF(0, kR, 4)) &&
         issue_draw(p, &hrDraw);
    d->SetViewport(&full);
    d->SetVertexShaderConstantF(0, kMono, 4);
  } else if (p.mode == SINGLE) {
    HRESULT hrArm = E.st->SetStereoDraw(kL, kR, W, H);
    if (rr) rr->hrArm = hrArm;
    if (FAILED(hrArm)) P("  .. SetStereoDraw: 0x%08lX\n", (unsigned long)hrArm);
    ok = issue_draw(p, &hrDraw);
    E.st->ClearStereoDraw();
  } else {
    ok = issue_draw(p, &hrDraw);
  }
  d->EndScene();
  if (rr) rr->hrDraw = hrDraw;
  if (p.instanced) {
    d->SetStreamSourceFreq(0, 1);
    d->SetStreamSourceFreq(1, 1);
    d->SetStreamSource(1, nullptr, 0, 0);
  }
  d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  if (!ok) { P("  !! draw failed: 0x%08lX\n", (unsigned long)hrDraw); return false; }
  if (out) return readback(p, *out);
  return true;
}

// ---------------------------------------------------------------- compare / dump
struct Diff { int maxd = 0; long ndiff = 0; };
static Diff compare(const Img& a, const Img& b) {
  Diff r;
  for (size_t i = 0; i < a.px.size(); i++) {
    uint32_t x = a.px[i], y = b.px[i];
    if (x == y) continue;
    int m = 0;
    for (int s = 0; s < 32; s += 8) {
      int dd = abs((int)((x >> s) & 255) - (int)((y >> s) & 255));
      if (dd > m) m = dd;
    }
    r.ndiff++;
    if (m > r.maxd) r.maxd = m;
  }
  return r;
}
static void write_ppm(const std::string& name, const Img& im, const Img* ref = nullptr) {
  std::string path = E.outDir + "/" + name + ".ppm";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { P("  (cannot write %s)\n", path.c_str()); return; }
  fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
  for (size_t i = 0; i < im.px.size(); i++) {
    uint32_t v = im.px[i];
    unsigned char rgb[3];
    if (ref) {   // diff map: amplified abs diff
      uint32_t o = ref->px[i];
      for (int c = 0; c < 3; c++) {
        int s = 16 - 8 * c;
        int dd = abs((int)((v >> s) & 255) - (int)((o >> s) & 255)) * 8;
        rgb[c] = (unsigned char)(dd > 255 ? 255 : dd);
      }
    } else { rgb[0] = (v >> 16) & 255; rgb[1] = (v >> 8) & 255; rgb[2] = v & 255; }
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
static void dump_pair(const std::string& base, const Img& a, const Img& b) {
  write_ppm(base + "_A_twopass", a);
  write_ppm(base + "_B_other", b);
  write_ppm(base + "_diff", a, &b);
  P("  dumped %s/%s_{A_twopass,B_other,diff}.ppm\n", E.outDir.c_str(), base.c_str());
}
static long count_non_clear(const Img& im, int x0, int x1) {
  long n = 0;
  for (int y = 0; y < im.h; y++)
    for (int x = x0; x < x1; x++) if (im.px[(size_t)y * im.w + x] != kClear) n++;
  return n;
}
static bool halves_equal(const Img& im) {
  int w = im.w / 2;
  for (int y = 0; y < im.h; y++)
    if (memcmp(&im.px[(size_t)y * im.w], &im.px[(size_t)y * im.w + w], (size_t)w * 4)) return false;
  return true;
}

// ---------------------------------------------------------------- cases
enum Status { PASS, FAIL, NA, SKIP };
struct Row { std::string name, two, single, note; Status st; int maxd; long ndiff; };
static std::vector<Row> rows;
static Img g_staticTwo, g_staticTwoMS, g_staticTwoSc;   // equivalence references

static std::string two_pass_check(const std::string& name, const Params& pp, const Img& im, const Img* refStatic) {
  long cov = count_non_clear(im, 0, E.W) , covR = count_non_clear(im, E.W, 2 * E.W);
  long min = (long)E.W * E.H / 20;
  if (cov < min || covR < min) { write_ppm(name + "_twopass_blank", im); return "BAD(blank)"; }
  if (halves_equal(im)) return "BAD(L==R)";
  if (refStatic && refStatic->w) {
    Diff d = compare(im, *refStatic);
    if (d.ndiff) { write_ppm(name + "_twopass_vs_static", im, refStatic); return "BAD(!=static)"; }
  }
  (void)pp;
  return "ok";
}

static void case_image(const std::string& name, Params p, const Img* staticRef, Img* keep) {
  Row row{name, "", "", "", FAIL, -1, -1};
  P("[%s]\n", name.c_str());
  Img a, b;
  p.mode = TWO;
  if (!render(p, &a)) { row.two = "ERR"; rows.push_back(row); return; }
  row.two = two_pass_check(name, p, a, staticRef);
  if (E.dump) write_ppm(name + "_twopass", a);
  if (keep) *keep = a;
  bool twoOk = row.two == "ok";
  if (!E.st || FAILED(E.hrEnable)) {
    row.single = "n/a"; row.st = NA;
    row.note = !E.st ? "no stereo interface (hr=0x" : "EnableSinglePassStereo failed (hr=0x";
    char buf[16]; snprintf(buf, sizeof buf, "%08lX)", (unsigned long)(!E.st ? E.hrQI : E.hrEnable));
    row.note += buf;
    if (!twoOk) row.st = FAIL;
    rows.push_back(row); return;
  }
  p.mode = SINGLE;
  RenderResult rr;
  if (!render(p, &b, &rr)) { row.single = "ERR"; rows.push_back(row); return; }
  Diff d = compare(a, b);
  row.single = "rendered"; row.maxd = d.maxd; row.ndiff = d.ndiff;
  row.st = (twoOk && d.ndiff == 0) ? PASS : FAIL;
  if (d.ndiff || E.dump) dump_pair(name, a, b);
  rows.push_back(row);
}

static void case_mono_after_clear() {
  Row row{"mono_after_clear", "ok", "", "", FAIL, -1, -1};
  P("[mono_after_clear]\n");
  Params mono; mono.mode = MONO;
  Img ref, after;
  if (!render(mono, &ref)) { row.two = "ERR"; rows.push_back(row); return; }
  if (!E.st || FAILED(E.hrEnable)) {
    row.single = "n/a"; row.st = NA; row.note = "no stereo interface";
    rows.push_back(row); return;
  }
  Params s; s.mode = SINGLE;
  if (!render(s, nullptr)) { row.single = "ERR"; rows.push_back(row); return; }   // arm + clear once
  if (!render(mono, &after)) { row.single = "ERR"; rows.push_back(row); return; }
  Diff d = compare(ref, after);
  row.single = "rendered"; row.maxd = d.maxd; row.ndiff = d.ndiff;
  row.st = d.ndiff == 0 ? PASS : FAIL;
  if (d.ndiff || E.dump) dump_pair("mono_after_clear", ref, after);
  rows.push_back(row);
}

static void case_instanced_reject() {
  Row row{"instanced_reject", "-", "", "", FAIL, -1, -1};
  P("[instanced_reject]\n");
  if (!E.st || FAILED(E.hrEnable)) {
    row.single = "n/a"; row.st = NA; row.note = "no stereo interface";
    rows.push_back(row); return;
  }
  UINT d0 = 0, r0 = 0, d1 = 0, r1 = 0;
  HRESULT hs0 = E.st->GetStereoStats(&d0, &r0);
  Params p; p.mode = SINGLE; p.instanced = true;
  RenderResult rr;
  bool drew = render(p, nullptr, &rr);
  HRESULT hs1 = E.st->GetStereoStats(&d1, &r1);
  bool rejByHr = FAILED(rr.hrArm) || FAILED(rr.hrDraw);
  bool rejByStat = SUCCEEDED(hs0) && SUCCEEDED(hs1) && r1 > r0;
  char buf[160];
  snprintf(buf, sizeof buf, "arm=0x%08lX draw=0x%08lX stats(rej %u->%u, stereo %u->%u)",
           (unsigned long)rr.hrArm, (unsigned long)rr.hrDraw, r0, r1, d0, d1);
  row.note = buf;
  row.single = rejByHr ? "rejected(hr)" : rejByStat ? "rejected(stat)" : "ACCEPTED";
  row.st = (rejByHr || rejByStat) ? PASS : FAIL;
  (void)drew;
  rows.push_back(row);
}

// ---------------------------------------------------------------- main
static const char* st_name(Status s) {
  return s == PASS ? "PASS" : s == FAIL ? "FAIL" : s == NA ? "N/A" : "SKIP";
}

int main(int argc, char** argv) {
  bool noEnable = false;
  std::string only;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--w" && i + 1 < argc) E.W = E.H = atoi(argv[++i]);
    else if (a == "--out" && i + 1 < argc) E.outDir = argv[++i];
    else if (a == "--dump") E.dump = true;
    else if (a == "--no-enable") noEnable = true;
    else if (a == "--only" && i + 1 < argc) only = argv[++i];
#ifndef STEREOTEST_USE_DXVK_HEADER
    else if (a == "--iid" && i + 1 < argc) { if (!parse_guid(argv[++i], &g_iid_local)) P("bad --iid\n"); }
#endif
  }
#ifndef STEREOTEST_USE_DXVK_HEADER
  if (getenv("STEREOTEST_IID")) parse_guid(getenv("STEREOTEST_IID"), &g_iid_local);
#endif
  setvbuf(stdout, nullptr, _IONBF, 0);
  g_log = fopen((E.outDir + "/stereotest.log").c_str(), "w");
  P("stereotest: W=H=%d, RT %dx%d\n", E.W, 2 * E.W, E.H);

  HMODULE dll = LoadLibraryA("d3d9_dxvk.dll");
  if (!dll) { P("FATAL: LoadLibrary(d3d9_dxvk.dll) failed (%lu)\n", GetLastError()); return 1; }
  typedef IDirect3D9* (WINAPI* PFN_create9)(UINT);
  typedef HRESULT (WINAPI* PFN_createvr)(IDirect3DDevice9*, IUnknown**);
  auto create9 = (PFN_create9)GetProcAddress(dll, "Direct3DCreate9");
  auto createvr = (PFN_createvr)GetProcAddress(dll, "Direct3DCreateVR9");
  if (!create9) { P("FATAL: no Direct3DCreate9 export\n"); return 1; }
  E.d3d = create9(D3D_SDK_VERSION);
  if (!E.d3d) { P("FATAL: Direct3DCreate9 returned null\n"); return 1; }

  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "stereotest";
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowA("stereotest", "stereotest", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  D3DPRESENT_PARAMETERS pp = {};
  pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = hwnd;
  pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  HRESULT hr = E.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
      D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE, &pp, &E.dev);
  if (FAILED(hr)) { P("FATAL: CreateDevice 0x%08lX\n", (unsigned long)hr); return 1; }
  D3DADAPTER_IDENTIFIER9 id;
  if (SUCCEEDED(E.d3d->GetAdapterIdentifier(0, 0, &id))) P("adapter: %s\n", id.Description);
  E.msaaOk = SUCCEEDED(E.d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_2_SAMPLES, nullptr));

  // Stereo interface, then enable BEFORE any shader exists.
  if (!createvr) { P("no Direct3DCreateVR9 export\n"); }
  else {
    hr = createvr(E.dev, &E.vr);
    if (FAILED(hr) || !E.vr) P("Direct3DCreateVR9: 0x%08lX\n", (unsigned long)hr);
    else {
      GUID iid = stereo_iid();
      E.hrQI = E.vr->QueryInterface(iid, (void**)&E.st);
      P("QueryInterface(IDirect3DVRStereo9): 0x%08lX%s\n", (unsigned long)E.hrQI,
             E.st ? "" : "  (not available)");
    }
  }
  if (E.st && !noEnable) {
    D3D9_VR_STEREO_CAPS caps = {};
    E.hrEnable = E.st->EnableSinglePassStereo(1, &caps);
    P("EnableSinglePassStereo(1): 0x%08lX  caps{ver %u vi %d mv %d bank %u}\n",
           (unsigned long)E.hrEnable, caps.Version, (int)caps.ViewportIndex, (int)caps.Multiview, caps.MaxBankVec4);
  } else if (noEnable) {
    E.hrEnable = E_NOTIMPL; P("--no-enable: single-pass skipped\n");
  }

  if (!setup_resources()) { P("FATAL: resource setup failed\n"); return 1; }

  auto want = [&](const char* n) { return only.empty() || only == n; };
  Params p;
  if (want("static") || only.empty()) { p = Params(); case_image("static", p, nullptr, &g_staticTwo); }
  if (want("reladdr")) { p = Params(); p.vs = 1; case_image("reladdr", p, &g_staticTwo, nullptr); }
  if (want("call"))    { p = Params(); p.vs = 2; case_image("call", p, &g_staticTwo, nullptr); }
  if (want("msaa2")) {
    if (!E.msaaOk) { rows.push_back({"msaa2", "-", "-", "2x MSAA unsupported", SKIP, -1, -1}); }
    else { p = Params(); p.msaa = true; case_image("msaa2", p, nullptr, nullptr); }
  }
  if (want("scissor")) { p = Params(); p.scissor = true; case_image("scissor", p, nullptr, nullptr); }
  if (want("mono_after_clear")) case_mono_after_clear();
  if (want("instanced_reject")) case_instanced_reject();

  UINT sd = 0, rj = 0;
  if (E.st && SUCCEEDED(E.hrEnable) && SUCCEEDED(E.st->GetStereoStats(&sd, &rj)))
    P("stereo stats: stereo draws %u, rejected %u\n", sd, rj);

  P("\n%-18s %-14s %-16s %7s %8s  %-6s %s\n", "case", "two-pass", "single-pass", "maxdiff", "ndiff", "status", "note");
  int nfail = 0, nna = 0;
  for (auto& r : rows) {
    char md[16] = "-", nd[16] = "-";
    if (r.maxd >= 0) { snprintf(md, sizeof md, "%d", r.maxd); snprintf(nd, sizeof nd, "%ld", r.ndiff); }
    P("%-18s %-14s %-16s %7s %8s  %-6s %s\n", r.name.c_str(), r.two.c_str(), r.single.c_str(), md, nd,
           st_name(r.st), r.note.c_str());
    if (r.st == FAIL) nfail++;
    if (r.st == NA) nna++;
  }
  int code = nfail ? 1 : nna ? 2 : 0;
  P("\nRESULT: %s (fail %d, not available %d)\n", code == 0 ? "ALL PASS" : code == 1 ? "FAILURES" : "INCOMPLETE", nfail, nna);
  fflush(stdout);
  TerminateProcess(GetCurrentProcess(), (UINT)code);   // skip DXVK teardown races
  return code;
}
