// FSR3 session source: the FFX create/dispatch event handlers the ffx_api.hpp hook
// bodies call into. Counterpart of ngx_probe.hpp's DLSS hooks, same one-
// responsibility split: nr_fsr consumes ffx_probe's extractors (UpscaleInputs/
// OnUpscaleGeometry) and nr_runner's state, and ffx_api.hpp keeps only the detour
// plumbing.
//
// Division of work, invariants: the create hook captures the upscale context's flag
// word (only on a successful create); the dispatch hook stamps the arbitration tick
// (unless Force DLSS excludes it), runs the source claim and extracts an upscale
// dispatch's guides/geometry; the recording hooks -- PreRecord (model resolution
// Render, before the real dispatch) and PostRecord (Full, after it) -- each extract
// from the same desc into a stack Inputs and run the FSR prologue and Evaluate.
// Per the spec's threading section: ffxDispatch may be reached from another thread
// than the evaluates (frame generation plausibly on its own), so the tick stamps
// are relaxed atomics, per-dispatch inputs live on this hook's stack for the
// invocation, and a later dispatch's extraction overwrites nr_fsr::last_inputs
// (one observable frame's worth -- never shared mutable state between concurrent
// dispatches).

#pragma once

#include "ffx_api.hpp"  // vendored FFX types + ffx_probe extraction helpers
#include "nr_runner.hpp"  // runner state/Inputs; also pins the NR_HOST_MOCK mode contract

#ifdef NR_HOST_MOCK
// Host unit-test build (test/unit/ffx_extract_test.cpp): no ngx_probe.hpp -- its
// real body needs <windows.h>/<detours.h>. ffx_mock.hpp already declared
// ngx_probe::Logf and friends declaration-only; what the test links is the
// no-op bodies below, called only through the nr_fsr paths the test exercises.
namespace ngx_probe {
inline void Log(const char*) {}
inline void Logf(const char*, ...) {}
}  // namespace ngx_probe
#else
#include "ngx_probe.hpp"  // Logf + nr_runner (ngx_probe.hpp includes it, as ffx_api.hpp does)
#endif

// On a host-mode contract violation nr_runner.hpp has already #errored above
// (and defined NR_HOST_MODE_CONTRACT_ERROR); skip everything below so that
// error stays the only diagnostic, exactly as ffx_api.hpp does.
#ifndef NR_HOST_MODE_CONTRACT_ERROR

namespace nr_fsr {

// Set once an upscale create-context desc has been seen (and successfully created);
// the panel/status reads it to tell "the game really created an FSR upscaler" apart
// from "no FFX create yet", and the record/claim gates use it as the session's
// create-desc gate (see RecordPass). Inline: several TUs include this header.
inline bool saw_upscale_create = false;

// First-dispatch dump budget: the same 3 as ffx_probe::kFullDumps in the hook
// trampoline, declared anew because that constant lives in ffx_api.hpp's
// NR_HOST_MOCK-guarded section (invisible to the host build). The heartbeat
// stays with the hook's own dispatch counter in ffx_api.hpp.
constexpr uint32_t kFullDumps = 3;

// The most recent upscale dispatch's extracted guides. The extraction helpers
// take a caller-owned Inputs, and the spec forbids shared mutable input state
// between dispatches -- but not a copy stamped after a successful extraction:
// the stack Inputs below is this dispatch's working copy, this is where it lands
// so the test (and later readers) can observe that classification reached the
// extraction. One observable frame's worth; the next dispatch overwrites it.
inline nr_runner::Inputs last_inputs;

// The ffxCreateContext hook's tail: walk the desc chain; if it holds the upscale
// create, record its flags (depth-inverted and friends read them through
// UpscaleInputs) and mark the upscaler as seen. Chain walk as in the SDK's
// pNext traversal; a null chain logs nothing and changes nothing.
inline void OnFfxCreateContext(const ffx_api::ffxCreateContextDescHeader* header) {
  for (const ffx_api::ffxApiHeader* h = header; h != nullptr; h = h->pNext) {
    if (h->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) continue;
    const auto* up = reinterpret_cast<const ffx_api::ffxCreateContextDescUpscale*>(h);
    ffx_api::active_upscale_flags = up->flags;
    saw_upscale_create = true;
    ngx_probe::Logf("nr-fwd: FSR3 upscaler context created (flags=0x%x)", up->flags);
    return;  // one upscale create per context chain
  }
}

// The ffxDispatch hook's head: tick first (arbitration reads the stamp), then
// classify header->type -- any type ticks; the upscale type additionally
// extracts. The recording/insertion calls are NOT here: they are the separate
// PreRecord/PostRecord entry points below, each extracting into its own stack
// `Inputs` in RecordPass, called from the hook body in ffx_api.hpp (which decides
// per mode where the trampoline goes relative to them). Returns nothing.
inline void OnFfxDispatch(void* command_list,
                          const ffx_api::ffxDispatchDescHeader* header) {
  // Force DLSS: hard gate (spec's "the excluded hook bodies return immediately after the
  // log line"). Not even the tick stamp -- Force DLSS means FSR is not the frame's source,
  // so the arbitration stamp must not advance on dispatches nobody will act on; the gate is
  // also what makes the one-time log line's condition stable.
  if (nr_runner::s.frame_source == nr_runner::kFsDlss) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      ngx_probe::Log("nr-fwd: DLSS session forced \u2014 FSR3 dispatches ignored");
    }
    return;
  }
  nr_runner::s.last_fsr_tick.store(GetTickCount64(), std::memory_order_relaxed);

  static uint32_t count = 0;
  const uint32_t n = ++count;

  const bool upscale = header != nullptr &&
                       header->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  nr_runner::Inputs in;  // stack: this dispatch's working copy, never shared state
  if (upscale) {
    const auto* d = reinterpret_cast<const ffx_api::ffxDispatchDescUpscale*>(header);
    if (ffx_probe::UpscaleInputs(d, &in)) {
      // The desc-type gate, not context identity: see the create-desc gate below. This
      // dispatch was made with an upscale desc, so geometry/extraction always land...
      ffx_probe::OnUpscaleGeometry(*d, in);
      last_inputs = in;  // stamped: the extraction path ran to completion
      // ...but the claim (and, with it, running the pass at all) additionally requires
      // that some upscale context create has been seen this session. Without it a
      // dispatch arriving before our ffxCreateContext hook chain saw any create would
      // claim FSR from geometry alone, and RecordPass would record with the flags of
      // nothing (depth_inverted reading 0 off the not-yet-captured flag word) -- either
      // from a context created before the hooks landed, or a loader/upscaler split
      // where our create hook is not on the creating module's chain (see nr_fsr.hpp's
      // create gate note). The claim: an upscale dispatch is classified (stamp first,
      // then claim from it, the ordering PreparePass shows for DLSS). Called on this
      // hook's thread -- the dispatch thread, the mirror of the evaluate thread that
      // claims DLSS. Arbitration decides (SourceChoice): this can still return without
      // claiming when Force DLSS gates above, Force standalone wins, or a fresh DLSS
      // outranks the FSR stamp.
      if (saw_upscale_create) {
        nr_runner::OnFsrDispatchClaim(GetTickCount64());
      } else {
        static bool said = false;
        if (!said) {
          said = true;
          ngx_probe::Log(
              "nr-fwd: FSR3 dispatch before an upscale context was seen -- pass not run "
              "(check which module ffxCreateContext was hooked on)");
        }
      }
    }
    if (n <= kFullDumps)
      ngx_probe::Logf(
          "nr-fwd: FSR3 dispatch #%u upscale %p -> %p %ux%u (render %ux%u, cmdlist=%p)", n,
          d->color.resource, d->output.resource, d->upscaleSize.width, d->upscaleSize.height,
          d->renderSize.width, d->renderSize.height, command_list);
  } else if (n <= kFullDumps) {
    ngx_probe::Logf("nr-fwd: FSR3 dispatch #%u type=0x%llx (not upscale, cmdlist=%p)", n,
                    (unsigned long long)(header ? header->type : 0), command_list);
  }
}

// --- recording around the trampoline -----------------------------------------
//
// Two entry points, one per model resolution, called from the hook body in
// ffx_api.hpp (the ngx_probe evaluate hook's `pre` pattern): PreRecord before
// real_Dispatch when PreSr() was true, PostRecord after it otherwise. Each is
// PreDlssEvaluate's shape adapted to the FFX desc instead of the NGX parameter
// block: type + D3D12 sanity check, extraction into a stack Inputs, the FSR
// prologue (PrepareFsrPass -- ffxDispatch carries no parameter block), Evaluate.
//
// Barriers: none added here, deliberately -- and this is a DEVIATION from the spec's
// original "the desc carries its current state; compose transitions in and restores"
// mechanism, chosen deliberately:
//   - FfxApiResource.state is never read. Its FfxApiResourceState bits are a FFX
//     enum with its own values (COMMON = 1<<0, UNORDERED_ACCESS = 1<<1, ...
//     RENDER_TARGET = 1<<8) that collide numerically with D3D12_RESOURCE_STATE_*
//     (COMMON = 0, and the D3D12 bits carry the <<16 accumulation rule), so a
//     naive passthrough of the word as a D3D12 state would silently barrier into
//     wrong states; a real translation table would still be guessing what FFX's
//     backend recorded as "current" after its own barriers.
//   - What the pass actually needs is fixed, not desc-driven: RecordPre/UAV entry
//     and the DLSS paths' NonPixel entry both begin from an assumed state. For the
//     Render path Evaluate's own NON_PIXEL -> UAV entry barrier (top of Evaluate)
//     supplies it -- the state FFX leaves its color input in after a dispatch, and
//     the state the DLSS paths start from too (RecordPre runs the same for them).
//     RecordPre's UAV -> COPY_SOURCE and RecordPost's restore back to UAV
//     (nr_compose.hpp, both bodies) close the bracket; RecordPost additionally
//     hands the frame back NON_PIXEL for the Render path (Evaluate's trailing
//     barrier).
//   - Depth and motion vectors are only read (model evaluate, debug views), never
//     transitioned -- identical to the DLSS session paths, which bind the same
//     guides straight out of the parameter block.
// The assumption is the vkd3d-tolerant one this add-on already runs on (the
// warn-and-write-anyway precedent for UAV flags); the in-game acceptance test is
// what must confirm it: no barrier/validation warnings in the log and no
// flicker/color corruption on the Render path's entry and Full path's write-back.
#ifndef NR_HOST_MOCK

// The shared recording half: desc checks, extraction, geometry from the caller.
// `frame` is the dispatch's color resource (Render: the upscale input; Full: the
// upscale output the pass writes back into).
inline void RecordPass(const ffx_api::ffxDispatchDescUpscale* d, ID3D12Resource* frame) {
  // Create-desc gate: only dispatches on a context created through our hook chain with
  // an upscale create desc may run the pass (the spec's "context identity decides").
  // Implemented as desc-type gate + session-wide saw_upscale_create rather than a
  // context-handle -> desc map: full identity would need to key ffxCreateContext's
  // opaque handle to its desc, but in practice the split is loader-hooked dispatches
  // against an upscaler-created context -- and when both modules' exports are hooked,
  // the same-process create still lands in our hook chain (Install attaches every
  // export the target module provides, and is retried per present until both modules
  // are covered), so the session-wide flag is the create side of the same fact. The
  // flag word captured with it is what carries the real DEPTH_INVERTED state; before
  // any create it would read 0, which is exactly the silent-wrong-depth this gate
  // exists to prevent. Unknown contexts (created before our hooks, or a module we do
  // not hook) therefore pass the dispatch through untouched -- the claim gate in
  // OnFfxDispatch enforces the same rule, one log line earlier.
  if (!saw_upscale_create) {
    static bool said = false;
    if (!said) {
      said = true;
      ngx_probe::Log(
          "nr-fwd: FSR3 dispatch before an upscale context was seen -- pass not run "
          "(check which module ffxCreateContext was hooked on)");
    }
    return;
  }
  // D3D12 sanity check: FfxApiResourceDescription carries no backend/API field; a
  // texture type is what the DX12 backend writes from the D3D12 resource desc.
  // Not D3D12-shaped: one bail-out line, the dispatch passes through untouched.
  if (!ffx_probe::IsD3d12TextureType(d->color.description)) {
    ngx_probe::Logf("nr-fwd: FSR3 dispatch not D3D12-shaped (color type=%u) -- pass skipped",
                    d->color.description.type);
    return;
  }
  if (frame == nullptr) return;  // malformed desc; Evaluate's own null guard agrees
  auto* cmd = static_cast<ID3D12GraphicsCommandList*>(d->commandList);
  if (cmd == nullptr) return;

  nr_runner::Inputs in;  // stack: this dispatch's working copy (extraction contract)
  if (!ffx_probe::UpscaleInputs(d, &in)) return;
  in.color = frame;  // Render records from the color input, Full from the output
  // The encode reference follows the DLSS precedent: the source's output format
  // in both modes (PreDlssEvaluate keys off the DLSS-SR output the same way).
  in.output_format = ffx_probe::OutputFormat(d->output.description);

  if (!nr_runner::PrepareFsrPass(cmd)) return;
  nr_runner::Evaluate(cmd, in);
  // The retire-every test lever, on this path the way OnDlssEvaluated carries it.
  // Qualified by hand: the NR_TEST_HOOK_AFTER_EVALUATE macro expands unqualified
  // for calls inside namespace nr_runner, and nr_fsr is a different namespace.
#ifdef DLSSNR_TEST_HOOKS
  nr_runner::TestHookAfterEvaluate();
#endif
}

// Model resolution Render, before the real dispatch (the PreDlssEvaluate shape):
// the pass enhances the upscale's color input in place, then ffxDispatch upscales
// the enhanced frame. The desc was already classified in OnFfxDispatch.
inline void PreRecord(const ffx_api::ffxDispatchDescHeader* header) {
  if (header == nullptr || header->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE) return;
  const auto* d = reinterpret_cast<const ffx_api::ffxDispatchDescUpscale*>(header);
  RecordPass(d, static_cast<ID3D12Resource*>(d->color.resource));
}

// Model resolution Full, after a successful real dispatch (the OnDlssEvaluated
// shape): the frame is the upscale output; RecordPost writes the enhanced frame
// back into it before the game's post-processing reads it.
inline void PostRecord(const ffx_api::ffxDispatchDescHeader* header) {
  if (header == nullptr || header->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE) return;
  const auto* d = reinterpret_cast<const ffx_api::ffxDispatchDescUpscale*>(header);
  RecordPass(d, static_cast<ID3D12Resource*>(d->output.resource));
}
#else
// Host unit-test build: the record bodies would pull Evaluate's graph into the
// link, whose mock side (test/unit/ffx_mock.hpp) is declaration-only by design --
// the extraction test exercises classification and extraction, not recording.
inline void PreRecord(const ffx_api::ffxDispatchDescHeader*) {}
inline void PostRecord(const ffx_api::ffxDispatchDescHeader*) {}
#endif  // !NR_HOST_MOCK

}  // namespace nr_fsr

#endif  // !NR_HOST_MODE_CONTRACT_ERROR
