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

#endif  // !NR_HOST_MODE_CONTRACT_ERROR
