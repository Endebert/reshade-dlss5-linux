// FSR3 session source: the FFX create/dispatch event handlers the ffx_api.hpp hook
// bodies call into. Counterpart of ngx_probe.hpp's DLSS hooks, same one-
// responsibility split: nr_fsr consumes the Task 1 extractors (ffx_probe::
// UpscaleInputs/OnUpscaleGeometry) and nr_runner's state, and ffx_api.hpp keeps
// only the detour plumbing.
//
// Task 3 scope: tick, classify, extract. The create hook captures the upscale
// context's flag word; the dispatch hook stamps the arbitration tick and extracts
// an upscale dispatch's guides/geometry. The recording/insertion calls are Task 6
// (see the OnFfxDispatch body for where they land). Per the spec's threading
// section: ffxDispatch may be reached from another thread than the evaluates
// (frame generation plausibly on its own), so the tick stamps are relaxed
// atomics, per-dispatch inputs live on this hook's stack for the invocation, and
// a later dispatch's extraction overwrites nr_fsr::last_inputs (one observable
// frame's worth -- never shared mutable state between concurrent dispatches).

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

// Set once an upscale create-context desc has been seen; the panel/status reads
// it later (Task 4+) to tell "the game really created an FSR upscaler" apart from
// "no FFX create yet". Inline: several TUs include this header.
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
// extracts. The recording/insertion calls are Task 6's: they belong right here,
// after this extraction, against the same stack `in` (and the hook body decides
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
      ffx_probe::OnUpscaleGeometry(*d, in);
      last_inputs = in;  // stamped: the extraction path ran to completion
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

}  // namespace nr_fsr

#endif  // !NR_HOST_MODE_CONTRACT_ERROR
