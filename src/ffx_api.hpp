// Transcribed from the FidelityFX SDK (MIT): ffx_api.h, ffx_api_types.h,
// ffx_upscale.h — local copy /home/ren/Projects/bloodborne_pc/tools/fsr4cap/ffx/.
// Only the members this addon reads; layout must match that SDK release.

#pragma once

#include <stdint.h>

// Build-mode contract, enforced rather than assumed (task 1 review finding 2):
// nr_runner.hpp picks between the real Windows/NGX include block and the host
// source mock from NR_HOST_MOCK, and that selection is pinned by the first of
// nr_runner.hpp / this header to be included in a TU.
//   - Real MSVC-targeted build: define nothing (exactly as before; the guard
//     nr_runner.hpp installs only fires for non-Windows compilers).
//   - Host g++ test build: `#define NR_HOST_MOCK` before the first of these two
//     headers. A host compile that gets that wrong fails with an #error naming
//     the contract (nr_runner.hpp) instead of an opaque missing <windows.h>.
#include "nr_runner.hpp"  // nr_runner::Inputs / nr_runner::s — mode per the contract above

// If the contract above was violated, nr_runner.hpp has already reported it with
// its #error; skip this header's body so that error is the only diagnostic.
#ifndef NR_HOST_MODE_CONTRACT_ERROR

// The SDK declares everything inside extern "C"; these are plain aggregates either
// way, so the vendored slice is transcribed as C++ declarations inside a namespace.
// The namespace also keeps the names from colliding with any other vendored copy.
namespace ffx_api {

// --- ffx_api.h -------------------------------------------------------------

typedef uint64_t ffxStructType_t;

typedef struct ffxApiHeader {
  ffxStructType_t type;       ///< The structure type; the corresponding value sits nearby with a similar name.
  struct ffxApiHeader* pNext; ///< Next structure, optional parameters/extensions. May be null.
} ffxApiHeader;

typedef ffxApiHeader ffxCreateContextDescHeader;
typedef ffxApiHeader ffxDispatchDescHeader;

/// A pointer to a function that can receive messages from the runtime. May be null.
typedef void (*ffxApiMessage)(uint32_t type, const wchar_t* message);

// The effect-identifier field of the type words; sub-ids live in the low bits.
#define FFX_API_EFFECT_MASK 0x00ff0000u
#define FFX_API_EFFECT_ID_UPSCALE 0x00010000u
#define FFX_API_EFFECT_ID_FRAMEGENERATION 0x00020000u

// Combines an effect id with a sub-version into a type word.
#define FFX_API_MAKE_EFFECT_SUB_ID(effectId, subversion) \
  ((effectId & FFX_API_EFFECT_MASK) | (subversion & ~FFX_API_EFFECT_MASK))

// --- ffx_api_types.h -------------------------------------------------------

/// A structure encapsulating a 2-dimensional point, using 32bit unsigned integers.
struct FfxApiDimensions2D {
  uint32_t width;  ///< The width of a 2-dimensional range.
  uint32_t height; ///< The height of a 2-dimensional range.
};

/// A structure encapsulating a 2-dimensional set of floating point coordinates.
struct FfxApiFloatCoords2D {
  float x; ///< The x coordinate of a 2-dimensional point.
  float y; ///< The y coordinate of a 2-dimensional point.
};

/// A structure describing a resource.
struct FfxApiResourceDescription {
  uint32_t type;   ///< The type of the resource.
  uint32_t format; ///< The surface format.
  union {
    uint32_t width; ///< The width of the texture resource.
    uint32_t size;  ///< The size of the buffer resource.
  };

  union {
    uint32_t height; ///< The height of the texture resource.
    uint32_t stride; ///< The stride of the buffer resource.
  };

  union {
    uint32_t depth;     ///< The depth of the texture resource.
    uint32_t alignment; ///< The alignment of the buffer resource.
  };

  uint32_t mipCount; ///< Number of mips (or 0 for full mipchain).
  uint32_t flags;    ///< A set of resource flags.
  uint32_t usage;    ///< Resource usage flags.
};

struct FfxApiResource {
  void* resource;
  FfxApiResourceDescription description;
  uint32_t state;
};

// --- ffx_upscale.h ---------------------------------------------------------

/// Create-time flags of the upscale context (FfxApiCreateContextUpscaleFlags).
enum FfxApiCreateContextUpscaleFlags : uint32_t {
  FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE = (1 << 0),                ///< Input color data is high-dynamic range.
  FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS = (1 << 1), ///< Motion vectors are at display resolution.
  FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION = (1 << 2),///< Motion vectors include the jitter pattern.
  FFX_UPSCALE_ENABLE_DEPTH_INVERTED = (1 << 3),                    ///< Depth buffer data is inverted [1..0].
  FFX_UPSCALE_ENABLE_DEPTH_INFINITE = (1 << 4),                    ///< Depth uses an infinite far plane.
  FFX_UPSCALE_ENABLE_AUTO_EXPOSURE = (1 << 5),                     ///< Automatic exposure is applied.
  FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION = (1 << 6),                ///< The application uses dynamic resolution scaling.
  FFX_UPSCALE_ENABLE_DEBUG_CHECKING = (1 << 7),                    ///< The runtime checks API values and reports issues.
  FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE = (1 << 8),             ///< The color resource holds perceptual colors.
  FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION = (1 << 9),               ///< Debug visualization is allowed.
};

#define FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE \
  FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_UPSCALE, 0x00)
struct ffxCreateContextDescUpscale {
  ffxCreateContextDescHeader header;        ///< Header descriptor, use type FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE.
  uint32_t flags;                          ///< Zero or a combination of FfxApiCreateContextUpscaleFlags.
  FfxApiDimensions2D maxRenderSize;        ///< The maximum size that rendering will be performed at.
  FfxApiDimensions2D maxUpscaleSize;       ///< The presentation-resolution size targeted by upscaling.
  ffxApiMessage fpMessage;                 ///< Optional message callback. May be null.
};

#define FFX_API_DISPATCH_DESC_TYPE_UPSCALE \
  FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_UPSCALE, 0x01)
struct ffxDispatchDescUpscale {
  ffxDispatchDescHeader header;       ///< Header descriptor, use type FFX_API_DISPATCH_DESC_TYPE_UPSCALE.
  void* commandList;                  ///< Command list to record upscaling commands into.
  FfxApiResource color;               ///< Color buffer for the current frame (at render resolution).
  FfxApiResource depth;               ///< 32bit depth values for the current frame (at render resolution).
  FfxApiResource motionVectors;       ///< 2-dimensional motion vectors.
  FfxApiResource exposure;            ///< Optional 1x1 exposure resource.
  FfxApiResource reactive;            ///< Optional reactive-object alpha resource.
  FfxApiResource transparencyAndComposition; ///< Optional transparency/composition alpha resource.
  FfxApiResource output;              ///< Output color buffer for the current frame (at presentation resolution).
  FfxApiFloatCoords2D jitterOffset;   ///< The subpixel jitter offset applied to the camera.
  FfxApiFloatCoords2D motionVectorScale; ///< The scale factor to apply to motion vectors.
  FfxApiDimensions2D renderSize;      ///< The resolution that was used for rendering the input resources.
  FfxApiDimensions2D upscaleSize;     ///< The resolution the upscaler will upscale to (optional, maxUpscaleSize otherwise).
  bool enableSharpening;              ///< Enable an additional sharpening pass.
  float sharpness;                    ///< The sharpness value between 0 and 1.
  float frameTimeDelta;               ///< The time elapsed since the last frame (milliseconds).
  float preExposure;                  ///< The pre exposure value (must be > 0.0f).
  bool reset;                         ///< True when the camera moved discontinuously.
  float cameraNear;                   ///< The distance to the near plane of the camera.
  float cameraFar;                    ///< The distance to the far plane of the camera.
  float cameraFovAngleVertical;       ///< The vertical camera field of view (radians).
  float viewSpaceToMetersFactor;      ///< The scale factor to convert view space units to meters.
  uint32_t flags;                     ///< Zero or a combination of FfxApiDispatchFsrUpscaleFlags.
};

// The create-context upscale flags captured by the ffxCreateContext hook (Task 3
// writes; read here by UpscaleInputs/OnUpscaleGeometry). Zero until an upscale
// create desc has been seen, so before any upscale create every FFX flag reads off.
// Declared only; the single inline definition is further down (one entity across
// all TUs that include this header -- no ODR violation at link).
extern uint32_t active_upscale_flags;

}  // namespace ffx_api

// --- probe interface ---------------------------------------------------------
// Task 2 completes Install/Uninstall; Task 3's nr_fsr.hpp drives the extraction.
namespace ffx_probe {

// extern "C" (as in the plan) so the test links the definitions at file scope in
// this header by their unmangled names; the definitions below are inline as well --
// the same ODR constraint as active_upscale_flags: several TUs include this header.
extern "C" bool UpscaleInputs(const ffx_api::ffxDispatchDescUpscale* d, nr_runner::Inputs* in);
extern "C" void OnUpscaleGeometry(const ffx_api::ffxDispatchDescUpscale& d,
                                   const nr_runner::Inputs& in);
bool Install();
void Uninstall();
extern bool installed;

}  // namespace ffx_probe

// Flag word of the upscale context last created through the hooked ffxCreateContext;
// zero until then, so every FFX flag reads off before any upscale create (Task 3's
// ffxCreateContext hook writes it, next to the saw_upscale_create bookkeeping).
namespace ffx_api {
// inline: this header is included by more than one TU (the test and Task 3's hook
// code); a non-inline definition here would be a multiple-definition link error.
inline uint32_t active_upscale_flags = 0;
}

// --- extraction (Task 1 bodies) -----------------------------------------------
// Definitions live in ffx_probe next to their declarations: a non-inline
// declaration in one scope and an inline definition in another is exactly the
// declaration/definition mismatch that keeps the weak (emittable) copy from being
// emitted; same scope, inline definition wins and every TU gets the symbol.
namespace ffx_probe {

// Pure: pulls the guides an upscale dispatch carries into the frame's Inputs. False
// unless the dispatch desc is an upscale one, so a non-upscale dispatch (frame
// generation, CAS, any other effect in the chain) can never feed the pass even if a
// caller forgets to filter first.
extern "C" inline bool UpscaleInputs(const ffx_api::ffxDispatchDescUpscale* d, nr_runner::Inputs* in) {
  if (d == nullptr || in == nullptr) return false;
  if (d->header.type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE) return false;
  in->depth = static_cast<ID3D12Resource*>(d->depth.resource);
  in->motion = static_cast<ID3D12Resource*>(d->motionVectors.resource);
  in->output = static_cast<ID3D12Resource*>(d->output.resource);
  in->mv_scale_x = d->motionVectorScale.x;
  in->mv_scale_y = d->motionVectorScale.y;
  in->reset = d->reset;
  in->depth_inverted =
      (ffx_api::active_upscale_flags & ffx_api::FFX_UPSCALE_ENABLE_DEPTH_INVERTED) != 0;
  return true;
}

// Pure state update: stores the geometry the pass runs at, following the DLSS
// precedent OnDlssCreate established -- color_in from the color resource's full
// allocation (a dynamic-resolution render inside a full-size resource still passes),
// the render/upscale sizes into the dlss_* slots the source branch applies. The
// DepthInverted bit (same value 1<<3 in both flag words) carries over so ReadGuides'
// consumers see it; the other FFX create flags are not DLSS flags and stay unmapped.
extern "C" inline void OnUpscaleGeometry(const ffx_api::ffxDispatchDescUpscale& d,
                                        const nr_runner::Inputs& in) {
  nr_runner::s.dlss_render_w = d.renderSize.width;
  nr_runner::s.dlss_render_h = d.renderSize.height;
  nr_runner::s.dlss_out_w = d.upscaleSize.width;
  nr_runner::s.dlss_out_h = d.upscaleSize.height;
  const bool depth_inverted =
      (ffx_api::active_upscale_flags & ffx_api::FFX_UPSCALE_ENABLE_DEPTH_INVERTED) != 0;
  if (depth_inverted)
    nr_runner::s.dlss_create_flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
  else
    nr_runner::s.dlss_create_flags &= ~NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
  nr_runner::s.color_in_w = d.color.description.width;
  nr_runner::s.color_in_h = d.color.description.height;
  (void)in;  // the extraction itself lands in Inputs; geometry is state only
}

}  // namespace ffx_probe

// --- Detours installation (Task 2) ---------------------------------------------
// The FFX API attach, shape-copied from ngx_probe::TryInstall/Uninstall: same
// process-wide stale-detour marker, same blocked state, same all-or-nothing
// transaction discipline. The hook bodies are stubs for now -- they forward to
// real_* (dispatch adds the first-3-then-heartbeat line so the attach is
// verifiable from the log); Task 3 wires the extraction in.
// NR_HOST_MOCK guard: the host unit-test build has neither <windows.h> nor
// <detours.h>. The declarations above stay visible in both modes; only this
// section compiles in the real build.
#ifndef NR_HOST_MOCK

#include "ngx_probe.hpp"  // Logf/Warnf + AttachedMarker (one marker per process)

namespace ffx_probe {

// Dump the first few dispatches, then a one-liner heartbeat (the constants
// pattern of ngx_probe.hpp:213).
constexpr uint32_t kFullDumps = 3;
constexpr uint32_t kHeartbeatEvery = 600;

// The exported entry points as ffx_api.h declares them: the context handle is
// an opaque pointer, the return code a uint32_t. The allocation-callbacks
// struct is only ever passed through to the real implementation, so it stays
// incomplete here.
using ffxContext = void*;
using ffxReturnCode_t = uint32_t;
struct ffxAllocationCallbacks;

static ffxReturnCode_t (*real_CreateContext)(ffxContext*,
                                             ffx_api::ffxCreateContextDescHeader*,
                                             const ffxAllocationCallbacks*) = nullptr;
static ffxReturnCode_t (*real_DestroyContext)(ffxContext*,
                                               const ffxAllocationCallbacks*) = nullptr;
static ffxReturnCode_t (*real_Dispatch)(ffxContext*,
                                        const ffx_api::ffxDispatchDescHeader*) = nullptr;

// Stub hook bodies (Task 3/6 replace them): forward to the real export. The
// dispatch one carries the first-3-then-heartbeat line, nothing else.
inline ffxReturnCode_t HookFfxCreateContext(ffxContext* context,
                                            ffx_api::ffxCreateContextDescHeader* desc,
                                            const ffxAllocationCallbacks* memCb) {
  return real_CreateContext(context, desc, memCb);
}

inline ffxReturnCode_t HookFfxDestroyContext(ffxContext* context,
                                             const ffxAllocationCallbacks* memCb) {
  return real_DestroyContext(context, memCb);
}

inline ffxReturnCode_t HookFfxDispatch(ffxContext* context,
                                       const ffx_api::ffxDispatchDescHeader* desc) {
  static uint32_t count = 0;
  const uint32_t n = ++count;
  if (n <= kFullDumps) {
    ngx_probe::Logf("ffx-probe: ffxDispatch #%u context=%p", n, context);
  } else if (n % kHeartbeatEvery == 0) {
    ngx_probe::Logf("ffx-probe: ffxDispatch #%u (alive)", n);
  }
  return real_Dispatch(context, desc);
}

struct HookEntry {
  const char* export_name;
  void** real;
  void* hook;
};

inline const HookEntry kHooks[] = {
    {"ffxCreateContext", reinterpret_cast<void**>(&real_CreateContext),
     reinterpret_cast<void*>(&HookFfxCreateContext)},
    {"ffxDestroyContext", reinterpret_cast<void**>(&real_DestroyContext),
     reinterpret_cast<void*>(&HookFfxDestroyContext)},
    {"ffxDispatch", reinterpret_cast<void**>(&real_Dispatch),
     reinterpret_cast<void*>(&HookFfxDispatch)},
};

// Defined here next to the declaration at the probe interface (same ODR note as
// active_upscale_flags above).
inline bool installed = false;
inline bool blocked = false;  // a state Install must never retry out of
// Whether ngx_probe's live detours owned the marker when we attached: the
// marker is one process-wide fact, and when it is shared only NGX's own
// uninstall decides when it may clear (ours can only re-mark it).
inline bool marker_shared = false;
// The module actually hooked (the panel / Task 5 status reads which upscaler
// is in use); null until the attach succeeds.
inline HMODULE upscaler_module = nullptr;

// Try to attach to the first FFX loader/upscaler module present in the process.
// Returns true once hooks are installed (further calls are no-ops). Cheap when
// nothing is loaded yet -- safe to call every present until it succeeds.
// All-or-nothing: every export the module provides is hooked, or none are.
inline bool Install() {
  if (installed) return true;
  if (blocked) return false;

  // Loader first (the surface games resolve through), upscaler DLL second. No
  // scan of all loaded modules in v1: a game that ships a differently named
  // module gets an explicit candidate added here instead.
  const char* candidates[] = {"amd_fidelityfx_loader_dx12.dll",
                              "amd_fidelityfx_upscaler_dx12.dll"};

  HMODULE module = nullptr;
  const char* module_name = nullptr;
  for (const char* candidate : candidates) {
    module = GetModuleHandleA(candidate);
    if (module != nullptr && GetProcAddress(module, "ffxDispatch") != nullptr) {
      module_name = candidate;
      break;
    }
    module = nullptr;
  }
  if (module == nullptr) return false;

  LONG* marker = ngx_probe::AttachedMarker();
  if (marker != nullptr && *marker != 0 && !ngx_probe::installed) {
    // A previous image of this DLL was unloaded without detaching: its detours
    // point into freed memory, and patching over them builds a jmp cycle (the
    // GTA V Enhanced hang, issue #3). Nothing can repair the exports from here.
    // One marker per process (ngx_probe's guard comment): a stale NGX detour
    // and a stale FFX detour must equally block. ngx_probe::installed is the
    // live-vs-stale discriminator -- a marker this image's NGX attach just set
    // is a live detour chain to chain onto, not a stale one to refuse.
    ngx_probe::Warnf("ffx-probe: not hooking (marker)");
    blocked = true;
    return false;
  }

  if (DetourTransactionBegin() != NO_ERROR) {
    ngx_probe::Log("ffx-probe: DetourTransactionBegin failed");
    return false;
  }
  DetourUpdateThread(GetCurrentThread());

  int attached = 0;
  bool failed = false;
  for (const auto& entry : kHooks) {
    FARPROC proc = GetProcAddress(module, entry.export_name);
    if (proc == nullptr) {
      // Not an error: a stripped module may carry only part of the API.
      ngx_probe::Logf("ffx-probe: %s not exported by %s", entry.export_name, module_name);
      continue;
    }
    *entry.real = reinterpret_cast<void*>(proc);
    if (DetourAttach(entry.real, entry.hook) != NO_ERROR) {
      ngx_probe::Warnf("ffx-probe: DetourAttach failed for %s", entry.export_name);
      failed = true;
      break;
    }
    ++attached;
  }

  if (failed || attached == 0) {
    DetourTransactionAbort();
    for (const auto& entry : kHooks) *entry.real = nullptr;
    // A failed attach is deterministic for a given export -- retrying every
    // present would only repeat it (and flood the log). Stay unhooked.
    if (failed) blocked = true;
    return false;
  }
  if (DetourTransactionCommit() != NO_ERROR) {
    // A failed commit aborts the transaction itself: no patches were applied.
    ngx_probe::Log("ffx-probe: DetourTransactionCommit failed");
    for (const auto& entry : kHooks) *entry.real = nullptr;
    blocked = true;
    return false;
  }

  marker_shared = marker != nullptr && *marker != 0;
  if (marker != nullptr) *marker = 1;
  installed = true;
  upscaler_module = module;
  ngx_probe::Logf("ffx-probe: hooked %d FFX exports on %s", attached, module_name);
  return true;
}

// Mirror of Install, for DllMain(DLL_PROCESS_DETACH) on FreeLibrary: the
// detours must not outlive the DLL image they jump into. Anything left
// attached here keeps the marker set, so no later load hooks on top of it.
inline void Uninstall() {
  if (!installed) return;

  if (DetourTransactionBegin() != NO_ERROR) {
    ngx_probe::Warnf("ffx-probe: DetourTransactionBegin failed on uninstall — detours leak");
    return;
  }
  DetourUpdateThread(GetCurrentThread());

  bool queued[std::size(kHooks)] = {};
  bool all = true;
  int detached = 0;
  for (size_t i = 0; i < std::size(kHooks); ++i) {
    if (*kHooks[i].real == nullptr) continue;
    if (DetourDetach(kHooks[i].real, kHooks[i].hook) != NO_ERROR) {
      ngx_probe::Warnf("ffx-probe: DetourDetach failed for %s — that detour leaks",
                       kHooks[i].export_name);
      all = false;
      continue;
    }
    queued[i] = true;
    ++detached;
  }

  if (DetourTransactionCommit() != NO_ERROR) {
    // A failed commit aborts the transaction itself: everything stays patched.
    ngx_probe::Warnf("ffx-probe: DetourTransactionCommit failed on uninstall — detours leak");
    return;
  }

  for (size_t i = 0; i < std::size(kHooks); ++i) {
    if (queued[i]) *kHooks[i].real = nullptr;
  }
  installed = false;
  if (all) upscaler_module = nullptr;
  if (LONG* marker = ngx_probe::AttachedMarker(); marker != nullptr) {
    if (marker_shared) {
      // Shared with NGX's live detours; DllMain detaches NGX first, so its
      // outcome already decided the marker. Only re-mark it if ours leak.
      if (!all) *marker = 1;
    } else {
      // Ours set it from zero: cleared only if all detached (a leak keeps the
      // stale state marked for the next image).
      *marker = all ? 0 : 1;
    }
  }
  ngx_probe::Logf("ffx-probe: detached %d FFX hooks", detached);
}

}  // namespace ffx_probe

#endif  // NR_HOST_MOCK (Detours installation section)

#endif  // !NR_HOST_MODE_CONTRACT_ERROR
