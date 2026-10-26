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
  assert(in.depth == reinterpret_cast<ID3D12Resource*>(0xDE90));
  assert(in.motion == reinterpret_cast<ID3D12Resource*>(0xF00D));
  assert(in.output == reinterpret_cast<ID3D12Resource*>(0x007B));
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
  nr_fsr::OnFfxDispatch(d.commandList, &d.header);

  // Ticked: the arbitration stamp now sees this dispatch...
  assert(nr_runner::s.last_fsr_tick.load(std::memory_order_relaxed) != 0);
  // ...but nothing was extracted: a non-upscale dispatch can never feed the pass.
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
  assert(nr_fsr::last_inputs.depth == reinterpret_cast<ID3D12Resource*>(0xDE90));
  assert(nr_fsr::last_inputs.motion == reinterpret_cast<ID3D12Resource*>(0xF00D));
  assert(nr_fsr::last_inputs.output == reinterpret_cast<ID3D12Resource*>(0x007B));
}

}  // namespace fsr

int main() {
  TestExtractFromUpscaleDispatch();
  TestRejectsNonUpscaleDispatch();
  TestGeometryFollowsColorAllocation();
  fsr::TestCreateContextCapturesUpscaleFlags();
  fsr::TestNonUpscaleDispatchTicksWithoutExtracting();
  fsr::TestUpscaleDispatchClassifiesAndExtracts();
  std::printf("ffx_extract_test: all asserts passed\n");
  return 0;
}
