// Host-side unit test for the FSR3 frame-source extraction functions: plain C++17,
// g++ on Linux, no framework -- asserts in main() only. NR_HOST_MOCK turns
// src/nr_runner.hpp into a source mock (see that header's top-of-file block), so the
// runner's includes, Windows/NGX/D3D12 surface and ffx_probe's extraction functions
// all live in this one translation unit.

#define NR_HOST_MOCK
#include <cassert>
#include "../../src/ffx_api.hpp"    // vendored FFX types + ffx_probe decls + the extractors
#include "../../src/nr_runner.hpp"  // host-mock build: State/Inputs/PassW/PassH

// Definition-only glue for paths this test actually calls: ffx_mock.hpp declares
// GetTickCount64 (the runner's Windows surface) but gives no body -- nothing
// reached it before nr_fsr's tick. A monotonic fake clock: any non-zero value
// passes the tick asserts, and nothing else in the harness reads it.
ULONGLONG GetTickCount64() {
  static ULONGLONG t = 1000;
  return ++t;
}

// The dispatch hook's source claim (nr_runner::OnFsrDispatchClaim) is reachable from
// nr_fsr::OnFfxDispatch, so RetireFeature's Drop/StandaloneIdle bodies link even though
// every test claim runs with no feature to retire (RetireFeature early-returns) and
// never touches a fence. Bodies the mock declares but never defined: never called.
DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
UINT64 ID3D12Fence::GetCompletedValue() const { return 0; }
HRESULT ID3D12Fence::SetEventOnCompletion(UINT64, HANDLE) { return 0; }
void ID3D12Resource::Release() {}

namespace {

namespace ffx = ffx_api;

// A dispatch whose header carries the upscale type, the guides the pass reads, and
// non-default motion-vector scaling so a pass-through of the wrong field cannot pass.
ffx::ffxDispatchDescUpscale MakeUpscaleDispatch() {
  ffx::ffxDispatchDescUpscale d = {};
  d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  d.color.resource = reinterpret_cast<void*>(0xC011);
  d.color.description.width = 1920;
  d.color.description.height = 1080;
  d.depth.resource = reinterpret_cast<void*>(0xDE90);
  d.motionVectors.resource = reinterpret_cast<void*>(0xF00D);
  d.output.resource = reinterpret_cast<void*>(0x007B);
  d.motionVectorScale = {2.5f, -1.25f};
  d.renderSize = {1706, 960};
  d.upscaleSize = {2560, 1440};
  d.reset = true;
  return d;
}

// 1. Upscale-typed dispatch -> UpscaleInputs true; depth/motion/output pointers and
//    mv_scale_x/y == desc.motionVectorScale.x/y; reset mirrors desc.reset; the
//    depth-inverted guide reflects the create flags, 0 before any upscale create.
void TestExtractFromUpscaleDispatch() {
  ffx::ffxDispatchDescUpscale d = MakeUpscaleDispatch();
  nr_runner::Inputs in = {};
  assert(ffx_probe::UpscaleInputs(&d, &in));
  assert(in.color == reinterpret_cast<ID3D12Resource*>(0xC011));
  assert(in.depth == reinterpret_cast<ID3D12Resource*>(0xDE90));
  assert(in.motion == reinterpret_cast<ID3D12Resource*>(0xF00D));
  assert(in.output == reinterpret_cast<ID3D12Resource*>(0x007B));
  // The color allocation the pass geometry keys off travels with the pointer
  // (Task 6 records from in.color, PreDlssEvaluate-style).
  assert(d.color.description.width == 1920 && d.color.description.height == 1080);
  assert(in.mv_scale_x == d.motionVectorScale.x);
  assert(in.mv_scale_y == d.motionVectorScale.y);
  assert(in.reset == d.reset);
  // ffx_api::active_upscale_flags is 0 until an upscale create has been seen (Task 3
  // writes it): the graceful default is the depth-inverted guide off.
  assert(ffx::active_upscale_flags == 0u);
  assert(!in.depth_inverted);
}

// 2. A dispatch whose header type is not FFX_API_DISPATCH_DESC_TYPE_UPSCALE -> false
//    (guards the pass against non-upscale dispatches even if a caller forgets).
void TestRejectsNonUpscaleDispatch() {
  ffx::ffxDispatchDescUpscale d = {};
  d.header.type = FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_FRAMEGENERATION, 0x01);
  nr_runner::Inputs in = {};
  assert(!ffx_probe::UpscaleInputs(&d, &in));
}

// 3. Geometry follows the DLSS precedent (full color allocation, not renderSize):
//    color_in_w/h come from the color resource's description; dlss_render/out from
//    renderSize/upscaleSize; the DepthInverted bit of dlss_create_flags mirrors the
//    create flags; PassW/PassH resolve per model resolution.
void TestGeometryFollowsColorAllocation() {
  ffx::ffxDispatchDescUpscale d = MakeUpscaleDispatch();
  nr_runner::Inputs in = {};

  // What Task 3's ffxCreateContext hook stores when the game's upscale context is
  // created. Task 1 ships the flag word at 0 (asserted above); the geometry test
  // provides the flags explicitly so the DepthInverted mapping is observable.
  ffx::active_upscale_flags =
      ffx::FFX_UPSCALE_ENABLE_DEPTH_INVERTED | ffx::FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;

  ffx_probe::OnUpscaleGeometry(d, in);

  // The color allocation, not renderSize: a dynamic-resolution render inside a
  // full-size resource must still pass (the DLSS precedent, spec geometry note).
  assert(nr_runner::s.color_in_w == 1920 && nr_runner::s.color_in_h == 1080);
  assert(nr_runner::s.dlss_render_w == 1706 && nr_runner::s.dlss_render_h == 960);
  assert(nr_runner::s.dlss_out_w == 2560 && nr_runner::s.dlss_out_h == 1440);
  // The DepthInverted bit (same value in both flag words) lands in the stored DLSS
  // create flags; the other FFX create flags are not DLSS flags and stay unmapped.
  assert((nr_runner::s.dlss_create_flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0);

  // The kSrcDlss branch of PreparePass applies the stored dlss_* geometry when the
  // DLSS source takes over (and the FSR source's branch will do the same); mirror it
  // here so PassW/PassH resolve the way the real pass would.
  nr_runner::s.render_w = nr_runner::s.dlss_render_w;
  nr_runner::s.render_h = nr_runner::s.dlss_render_h;
  nr_runner::s.out_w = nr_runner::s.dlss_out_w;
  nr_runner::s.out_h = nr_runner::s.dlss_out_h;
  nr_runner::s.create_flags = nr_runner::s.dlss_create_flags;

  nr_runner::s.dlss_resolution = nr_runner::kResRender;
  assert(nr_runner::PassW() == 1920 && nr_runner::PassH() == 1080);
  nr_runner::s.dlss_resolution = nr_runner::kResFull;
  assert(nr_runner::PassW() == 2560 && nr_runner::PassH() == 1440);

  ffx::active_upscale_flags = 0;  // restore the pre-create default for later tests
}

// 3b (Task 6): output_format resolution -- the FFX surface-format enum (as the
//    DX12 backend's ffxApiGetSurfaceFormatDX12 writes it into the resource
//    description) maps back to the DXGI format the encode decision keys off.
//    R8G8B8A8_UNORM_SRGB and B8G8R8A8_UNORM (DD2's swapchain family) are pinned
//    exactly; unknown/out-of-enum formats resolve to UNKNOWN so Evaluate falls
//    back to the frame's own format instead of a wrong encoding guess.
void TestOutputFormatMapsFfxSurfaceFormat() {
  ffx::FfxApiResourceDescription desc = {};
  desc.format = ffx::FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
  assert(ffx_probe::OutputFormat(desc) == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
  desc.format = ffx::FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
  assert(ffx_probe::OutputFormat(desc) == DXGI_FORMAT_B8G8R8A8_UNORM);
  desc.format = ffx::FFX_API_SURFACE_FORMAT_UNKNOWN;
  assert(ffx_probe::OutputFormat(desc) == DXGI_FORMAT_UNKNOWN);
  desc.format = 0xFFFFFFFFu;  // out of enum: never a DXGI value by accident
  assert(ffx_probe::OutputFormat(desc) == DXGI_FORMAT_UNKNOWN);
}

// 3c (Task 6): the resource-type sanity check -- the description's type enum is
//    what the DX12 backend writes from the D3D12 resource desc (BUFFER=0,
//    TEXTURE1D=1, TEXTURE2D=2, CUBE=3, TEXTURE3D=4); anything else means the
//    desc did not come through a D3D12 backend and the pass must not touch it.
void TestTextureTypeRecognized() {
  ffx::FfxApiResourceDescription desc = {};
  desc.type = ffx::FFX_API_RESOURCE_TYPE_TEXTURE2D;
  assert(ffx_probe::IsD3d12TextureType(desc));
  desc.type = ffx::FFX_API_RESOURCE_TYPE_BUFFER;
  assert(!ffx_probe::IsD3d12TextureType(desc));
  desc.type = 99;  // not an FfxApiResourceType value
  assert(!ffx_probe::IsD3d12TextureType(desc));
}

}  // namespace


// --- Task 3 additions: nr_fsr classification + create-flag capture --------------
#include "../../src/nr_fsr.hpp"  // nr_fsr::OnFfxCreateContext / OnFfxDispatch

namespace fsr {

// 4. An upscale create-context desc through nr_fsr::OnFfxCreateContext stores its
//    flags (Task 3's create hook); a non-upscale chain leaves them untouched.
void TestCreateContextCapturesUpscaleFlags() {
  nr_fsr::saw_upscale_create = false;

  ffx::ffxCreateContextDescUpscale up = {};
  up.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  up.flags = ffx::FFX_UPSCALE_ENABLE_DEPTH_INVERTED | ffx::FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
  nr_fsr::OnFfxCreateContext(&up.header);

  assert(ffx::active_upscale_flags == up.flags);
  assert(nr_fsr::saw_upscale_create);

  // A chain without an upscale create must not touch either (a framegen context
  // passes its own headers straight through).
  ffx::ffxApiHeader other = {};
  other.type = FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_FRAMEGENERATION, 0x00);
  nr_fsr::OnFfxCreateContext(&other);
  assert(ffx::active_upscale_flags == up.flags);
  assert(nr_fsr::saw_upscale_create);

  ffx::active_upscale_flags = 0;  // restore the pre-create default for later tests
}

// 5. A non-upscale (framegen-typed) dispatch still ticks the arbitration stamp but
//    must not extract: the stack Inputs stays default (null guides).
void TestNonUpscaleDispatchTicksWithoutExtracting() {
  ffx::ffxDispatchDescUpscale d = MakeUpscaleDispatch();
  d.header.type = FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_FRAMEGENERATION, 0x01);

  nr_runner::s.last_fsr_tick.store(0, std::memory_order_relaxed);
  nr_fsr::last_inputs = {};  // reset so the negative asserts below are order-independent
  nr_fsr::OnFfxDispatch(d.commandList, &d.header);

  // Ticked: the arbitration stamp now sees this dispatch...
  assert(nr_runner::s.last_fsr_tick.load(std::memory_order_relaxed) != 0);
  // ...but nothing was extracted: a non-upscale dispatch can never feed the pass.
  assert(nr_fsr::last_inputs.color == nullptr);
  assert(nr_fsr::last_inputs.depth == nullptr);
  assert(nr_fsr::last_inputs.motion == nullptr);
  assert(nr_fsr::last_inputs.output == nullptr);
}

// 6. An upscale-typed dispatch classifies through: the tick lands and the extracted
//    guides reach nr_fsr::last_inputs (the classification path Task 6 records from).
void TestUpscaleDispatchClassifiesAndExtracts() {
  ffx::ffxDispatchDescUpscale d = MakeUpscaleDispatch();
  nr_runner::s.last_fsr_tick.store(0, std::memory_order_relaxed);

  nr_fsr::OnFfxDispatch(d.commandList, &d.header);

  assert(nr_runner::s.last_fsr_tick.load(std::memory_order_relaxed) != 0);
  assert(nr_fsr::last_inputs.color == reinterpret_cast<ID3D12Resource*>(0xC011));
  assert(nr_fsr::last_inputs.depth == reinterpret_cast<ID3D12Resource*>(0xDE90));
  assert(nr_fsr::last_inputs.motion == reinterpret_cast<ID3D12Resource*>(0xF00D));
  assert(nr_fsr::last_inputs.output == reinterpret_cast<ID3D12Resource*>(0x007B));
  // The color allocation reached the geometry state through the full dispatch path
  // (OnUpscaleGeometry): the size PassW/PrepareFsrPass key off.
  assert(nr_runner::s.color_in_w == 1920 && nr_runner::s.color_in_h == 1080);
}

// 6b. Force DLSS hard gate: with frame_source == kFsDlss the dispatch hook returns
//    before the arbitration stamp and before extraction -- last_fsr_tick stays 0 and
//    last_inputs keeps nothing from this dispatch (the "excluded hook bodies return
//    immediately" rule, in unit-test form).
void TestForceDlssGateBlocksDispatch() {
  ffx::ffxDispatchDescUpscale d = MakeUpscaleDispatch();
  nr_runner::s.last_fsr_tick.store(0, std::memory_order_relaxed);
  nr_fsr::last_inputs = {};  // deterministic baseline for the untouched asserts
  const int prev_frame_source = nr_runner::s.frame_source;
  nr_runner::s.frame_source = nr_runner::kFsDlss;

  nr_fsr::OnFfxDispatch(d.commandList, &d.header);

  assert(nr_runner::s.last_fsr_tick.load(std::memory_order_relaxed) == 0);
  assert(nr_fsr::last_inputs.color == nullptr);
  assert(nr_fsr::last_inputs.depth == nullptr);
  assert(nr_fsr::last_inputs.motion == nullptr);
  assert(nr_fsr::last_inputs.output == nullptr);

  nr_runner::s.frame_source = prev_frame_source;  // restore for later tests
}

}  // namespace fsr

// Note: nr_fsr's "logs the first 3 dispatches" gating (src/nr_fsr.hpp, `static uint32_t n`)
// is diagnostic-only and is not asserted here -- it is verified by `bash build.sh` compilation
// plus the in-game first-dispatch dumps (plan Task 6), not by this unit test.

// --- Task 4 additions: source arbitration (SourceChoice) ----------------------

namespace arb {

// Shared clock base for the arbiter cases: a fresh stamp is `now - 1`, a stale
// one older than both quiet windows (kDlssQuietMs == kFsrQuietMs == 3000).
constexpr ULONGLONG kNow = 100000;
constexpr ULONGLONG kFresh = kNow - 1;
constexpr ULONGLONG kStale = kNow - 4000;

// 7. Auto: dlss fresh, fsr fresh -> kSrcDlss (priority dlss > fsr).
//    Auto: dlss stale, fsr fresh -> kSrcFsr.
//    Auto: both stale, within arm -> -1 (standalone not armed yet).
//    Force FSR3 with fresh dlss -> kSrcFsr (force beats priority).
//    Force DLSS with only fsr fresh -> -1 (pass never runs).
void TestArbiterPriorityAndForces() {
  using nr_runner::SourceChoice;
  assert(SourceChoice(kNow, kFresh, kFresh, nr_runner::kFsAuto) == nr_runner::kSrcDlss);
  assert(SourceChoice(kNow, kStale, kFresh, nr_runner::kFsAuto) == nr_runner::kSrcFsr);
  // Both stale, still inside the 10 s standalone arm -> no source yet.
  nr_runner::s.armed_tick = kNow;
  assert(SourceChoice(kNow, kStale, kStale, nr_runner::kFsAuto) == -1);
  // Arm elapsed -> standalone claims in Auto (the kStandaloneArmMs gate).
  nr_runner::s.armed_tick = kNow - nr_runner::kStandaloneArmMs;
  assert(SourceChoice(kNow, kStale, kStale, nr_runner::kFsAuto) == nr_runner::kSrcStandalone);
  nr_runner::s.armed_tick = 0;  // restore for later tests
  // Force beats priority: Force FSR3 with a fresh DLSS stamp still picks FSR...
  assert(SourceChoice(kNow, kFresh, kFresh, nr_runner::kFsFsr) == nr_runner::kSrcFsr);
  // ...Force DLSS with only FSR fresh yields nothing (the pass never runs)...
  assert(SourceChoice(kNow, kStale, kFresh, nr_runner::kFsDlss) == -1);
  // ...and Force standalone claims regardless of activity or arm (the old Always).
  assert(SourceChoice(kNow, 0, 0, nr_runner::kFsStandalone) == nr_runner::kSrcStandalone);
  // Force standalone with a *fresh FSR stamp* and stale (or zero) DLSS still picks
  // standalone. The arbiter already returns kSrcStandalone unconditionally here -- this
  // pins it against future drift: StandaloneAllowed's force branch defers only to a
  // fresh DLSS session, never to last_fsr, so an FSR-active game still gets a frame
  // source instead of standing by forever.
  assert(SourceChoice(kNow, kStale, kFresh, nr_runner::kFsStandalone) ==
         nr_runner::kSrcStandalone);
  assert(SourceChoice(kNow, 0, kFresh, nr_runner::kFsStandalone) == nr_runner::kSrcStandalone);
}

// Zero stamps mean "never seen" and never count as fresh, whatever the clock says.
void TestZeroStampsNeverFresh() {
  using nr_runner::SourceChoice;
  assert(SourceChoice(kNow, 0, 0, nr_runner::kFsAuto) == -1);
  assert(SourceChoice(kNow, 0, kFresh, nr_runner::kFsAuto) == nr_runner::kSrcFsr);
  assert(SourceChoice(kNow, kFresh, 0, nr_runner::kFsAuto) == nr_runner::kSrcDlss);
  assert(SourceChoice(kNow, 0, kFresh, nr_runner::kFsDlss) == -1);
  assert(SourceChoice(kNow, 0, 0, nr_runner::kFsFsr) == -1);
}

}  // namespace arb

int main() {
  TestExtractFromUpscaleDispatch();
  TestRejectsNonUpscaleDispatch();
  TestGeometryFollowsColorAllocation();
  TestOutputFormatMapsFfxSurfaceFormat();
  TestTextureTypeRecognized();
  fsr::TestCreateContextCapturesUpscaleFlags();
  fsr::TestNonUpscaleDispatchTicksWithoutExtracting();
  fsr::TestUpscaleDispatchClassifiesAndExtracts();
  fsr::TestForceDlssGateBlocksDispatch();
  arb::TestArbiterPriorityAndForces();
  arb::TestZeroStampsNeverFresh();
  std::printf("ffx_extract_test: all asserts passed\n");
  return 0;
}
