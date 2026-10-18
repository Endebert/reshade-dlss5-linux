// Standalone mode for Vulkan games: the transport.
//
// The model only runs on D3D12, so a Vulkan game's frame is carried across, as for D3D11: right
// after the motion-vector effect has rendered, the back buffer, depth (converted to R32_FLOAT)
// and motion vectors are copied into textures created shared on a private D3D12 device and
// imported into the game's VkDevice; the runner's standalone pass runs there and the result is
// copied back.
//
// Following dlss5-bridge by NIGos (MIT), nothing in Vulkan is hooked: ReShade already enables
// VK_KHR_external_memory_win32 on the devices it creates, which is all the import needs. The
// few Vulkan declarations used are written out rather than vendored, as the bridge does.
//
// The two APIs are ordered by the bridge's park. Flushing ReShade's list mid-frame is not an
// option: the flush that ends the frame is the one that waits on the game's present semaphores,
// and a submit ahead of it copies the swapchain image before the game has finished drawing it --
// a frame from three presents ago. So the copies in are recorded, a vkCmdSetEvent announces them,
// and a vkCmdWaitEvents stalls the game's queue until a worker thread, having seen the first
// event, has run the D3D12 pass and host-sets the second; the copy back is recorded after the
// wait.

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdint>
#include <cstdio>

#include <reshade.hpp>

#include "nr_runner.hpp"

#include "../build/shaders/nr_vk_depth_spv.h"
#include "../build/shaders/nr_vk_depth_flip_spv.h"

namespace nr_vulkan {

// --- the Vulkan subset --------------------------------------------------------

typedef struct VkDevice_T* VkDev;
typedef struct VkCommandBuffer_T* VkCmd;
typedef uint64_t VkH;  // VkImage, VkDeviceMemory

struct VkExtent3D_ { uint32_t width, height, depth; };
struct VkOffset3D_ { int32_t x, y, z; };
struct VkSubresourceLayers_ { uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount; };
struct VkSubresourceRange_ { uint32_t aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount; };
struct VkExternalMemoryImageCreateInfo_ { uint32_t sType; const void* pNext; uint32_t handleTypes; };
struct VkImageCreateInfo_ {
  uint32_t sType; const void* pNext; uint32_t flags; uint32_t imageType; uint32_t format;
  VkExtent3D_ extent; uint32_t mipLevels, arrayLayers, samples, tiling, usage, sharingMode;
  uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; uint32_t initialLayout;
};
struct VkMemoryRequirements_ { uint64_t size, alignment; uint32_t memoryTypeBits; };
struct VkMemoryWin32HandleProperties_ { uint32_t sType; void* pNext; uint32_t memoryTypeBits; };
struct VkImportMemoryWin32HandleInfo_ { uint32_t sType; const void* pNext; uint32_t handleType; HANDLE handle; LPCWSTR name; };
struct VkMemoryDedicatedAllocateInfo_ { uint32_t sType; const void* pNext; VkH image; VkH buffer; };
struct VkMemoryAllocateInfo_ { uint32_t sType; const void* pNext; uint64_t allocationSize; uint32_t memoryTypeIndex; };
struct VkImageMemoryBarrier_ {
  uint32_t sType; const void* pNext; uint32_t srcAccessMask, dstAccessMask, oldLayout, newLayout;
  uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkH image; VkSubresourceRange_ subresourceRange;
};
struct VkImageCopy_ {
  VkSubresourceLayers_ srcSubresource; VkOffset3D_ srcOffset;
  VkSubresourceLayers_ dstSubresource; VkOffset3D_ dstOffset; VkExtent3D_ extent;
};

constexpr uint32_t kStImageCreateInfo = 14, kStMemoryAllocateInfo = 5, kStImageMemoryBarrier = 45;
constexpr uint32_t kStExternalMemoryImage = 1000072001, kStImportMemoryWin32 = 1000073000;
constexpr uint32_t kStMemoryWin32Props = 1000073002, kStMemoryDedicated = 1000127001;
constexpr uint32_t kHandleD3D12Resource = 0x40;
constexpr uint32_t kLayoutUndefined = 0, kLayoutGeneral = 1, kLayoutTransferSrc = 6,
                   kLayoutTransferDst = 7;
constexpr uint32_t kAccessRead = 0x8000, kAccessWrite = 0x10000, kStageAll = 0x10000;
constexpr uint32_t kQueueFamilyIgnored = 0xFFFFFFFFu;
constexpr uint32_t kStEventCreateInfo = 10, kStMemoryBarrier = 46;
constexpr uint32_t kEventSet = 3, kStageHost = 0x4000;
struct VkEventCreateInfo_ { uint32_t sType; const void* pNext; uint32_t flags; };
struct VkMemoryBarrier_ { uint32_t sType; const void* pNext; uint32_t srcAccessMask, dstAccessMask; };

using PFN_GetDeviceProcAddr = void*(__stdcall*)(VkDev, const char*);
using PFN_CreateImage = int32_t(__stdcall*)(VkDev, const VkImageCreateInfo_*, const void*, VkH*);
using PFN_DestroyImage = void(__stdcall*)(VkDev, VkH, const void*);
using PFN_GetImageMemoryRequirements = void(__stdcall*)(VkDev, VkH, VkMemoryRequirements_*);
using PFN_GetMemoryWin32HandleProperties = int32_t(__stdcall*)(VkDev, uint32_t, HANDLE, VkMemoryWin32HandleProperties_*);
using PFN_AllocateMemory = int32_t(__stdcall*)(VkDev, const VkMemoryAllocateInfo_*, const void*, VkH*);
using PFN_FreeMemory = void(__stdcall*)(VkDev, VkH, const void*);
using PFN_BindImageMemory = int32_t(__stdcall*)(VkDev, VkH, VkH, uint64_t);
using PFN_CmdPipelineBarrier = void(__stdcall*)(VkCmd, uint32_t, uint32_t, uint32_t, uint32_t, const void*, uint32_t, const void*, uint32_t, const VkImageMemoryBarrier_*);
using PFN_CmdCopyImage = void(__stdcall*)(VkCmd, VkH, uint32_t, VkH, uint32_t, uint32_t, const VkImageCopy_*);
using PFN_CreateEvent = int32_t(__stdcall*)(VkDev, const VkEventCreateInfo_*, const void*, VkH*);
using PFN_DestroyEvent = void(__stdcall*)(VkDev, VkH, const void*);
using PFN_EventHost = int32_t(__stdcall*)(VkDev, VkH);  // vkGetEventStatus, vkSetEvent, vkResetEvent
using PFN_CmdSetEvent = void(__stdcall*)(VkCmd, VkH, uint32_t);
using PFN_CmdWaitEvents = void(__stdcall*)(VkCmd, uint32_t, const VkH*, uint32_t, uint32_t, uint32_t, const VkMemoryBarrier_*, uint32_t, const void*, uint32_t, const void*);

struct Vk {
  PFN_CreateImage CreateImage = nullptr;
  PFN_DestroyImage DestroyImage = nullptr;
  PFN_GetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
  PFN_GetMemoryWin32HandleProperties GetMemoryWin32HandleProperties = nullptr;
  PFN_AllocateMemory AllocateMemory = nullptr;
  PFN_FreeMemory FreeMemory = nullptr;
  PFN_BindImageMemory BindImageMemory = nullptr;
  PFN_CmdPipelineBarrier CmdPipelineBarrier = nullptr;
  PFN_CmdCopyImage CmdCopyImage = nullptr;
  PFN_CreateEvent CreateEvent = nullptr;
  PFN_DestroyEvent DestroyEvent = nullptr;
  PFN_EventHost GetEventStatus = nullptr;
  PFN_EventHost SetEvent = nullptr;
  PFN_EventHost ResetEvent = nullptr;
  PFN_CmdSetEvent CmdSetEvent = nullptr;
  PFN_CmdWaitEvents CmdWaitEvents = nullptr;
};

// DXGI -> VkFormat for the formats that cross.
inline uint32_t VkFormatOf(DXGI_FORMAT f) {
  switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return 37;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return 44;
    case DXGI_FORMAT_B8G8R8X8_UNORM: return 44;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 64;  // A2B10G10R10_UNORM_PACK32
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 97;
    case DXGI_FORMAT_R16G16_FLOAT: return 83;
    case DXGI_FORMAT_R32G32_FLOAT: return 103;
    case DXGI_FORMAT_R32_FLOAT: return 100;
    default: return 0;
  }
}

// --- state --------------------------------------------------------------------

struct Shared {
  ID3D12Resource* tex12 = nullptr;
  VkH image = 0;
  bool laid_out = false;  // moved to GENERAL once; it never leaves it
  VkH memory = 0;
  UINT w = 0, h = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct State {
  bool failed = false;
  VkDev device = nullptr;
  Vk vk;
  ID3D12Device* dev12 = nullptr;
  Shared color, depth, motion;
  // Depth conversion, on ReShade's API: a compute pipeline and an R32_FLOAT texture it writes.
  reshade::api::pipeline_layout layout = {0};
  reshade::api::pipeline pipeline = {0}, pipeline_flip = {0};
  reshade::api::resource depth32 = {0};
  reshade::api::resource_view depth32_uav = {0};
  UINT depth32_w = 0, depth32_h = 0;
  // The park: two event pairs used in turn, and the worker that serves them.
  VkH ev_in[2] = {}, ev_out[2] = {};
  HANDLE worker = nullptr, wake = nullptr, done = nullptr;
  LONG stop = 0;
  int pair = 0;           // the pair the worker serves next
  bool armed = false;     // a frame was handed to the worker and not yet collected
  LONG missed = 0;  // the worker never saw the copies of the last frame
};

inline State v;

inline bool Fail(const char* why, long code = 0) {
  ngx_probe::Logf("nr-vulkan: %s (%ld) -- Vulkan standalone disabled", why, code);
  std::snprintf(nr_runner::s.sa_status, sizeof(nr_runner::s.sa_status),
                "Vulkan transport failed: %s (see ReShade.log)", why);
  v.failed = true;
  return false;
}

// --- setup --------------------------------------------------------------------

inline bool LoadVk(VkDev device) {
  HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
  if (loader == nullptr) loader = LoadLibraryW(L"vulkan-1.dll");
  auto gdpa = loader != nullptr ? (PFN_GetDeviceProcAddr)GetProcAddress(loader, "vkGetDeviceProcAddr")
                                : nullptr;
  if (gdpa == nullptr) return Fail("vkGetDeviceProcAddr not found");
  Vk& f = v.vk;
  f.CreateImage = (PFN_CreateImage)gdpa(device, "vkCreateImage");
  f.DestroyImage = (PFN_DestroyImage)gdpa(device, "vkDestroyImage");
  f.GetImageMemoryRequirements = (PFN_GetImageMemoryRequirements)gdpa(device, "vkGetImageMemoryRequirements");
  f.GetMemoryWin32HandleProperties = (PFN_GetMemoryWin32HandleProperties)gdpa(device, "vkGetMemoryWin32HandlePropertiesKHR");
  f.AllocateMemory = (PFN_AllocateMemory)gdpa(device, "vkAllocateMemory");
  f.FreeMemory = (PFN_FreeMemory)gdpa(device, "vkFreeMemory");
  f.BindImageMemory = (PFN_BindImageMemory)gdpa(device, "vkBindImageMemory");
  f.CmdPipelineBarrier = (PFN_CmdPipelineBarrier)gdpa(device, "vkCmdPipelineBarrier");
  f.CmdCopyImage = (PFN_CmdCopyImage)gdpa(device, "vkCmdCopyImage");
  f.CreateEvent = (PFN_CreateEvent)gdpa(device, "vkCreateEvent");
  f.DestroyEvent = (PFN_DestroyEvent)gdpa(device, "vkDestroyEvent");
  f.GetEventStatus = (PFN_EventHost)gdpa(device, "vkGetEventStatus");
  f.SetEvent = (PFN_EventHost)gdpa(device, "vkSetEvent");
  f.ResetEvent = (PFN_EventHost)gdpa(device, "vkResetEvent");
  f.CmdSetEvent = (PFN_CmdSetEvent)gdpa(device, "vkCmdSetEvent");
  f.CmdWaitEvents = (PFN_CmdWaitEvents)gdpa(device, "vkCmdWaitEvents");
  if (f.GetMemoryWin32HandleProperties == nullptr)
    return Fail("VK_KHR_external_memory_win32 is not enabled on the game's device");
  if (!f.CreateImage || !f.DestroyImage || !f.GetImageMemoryRequirements || !f.AllocateMemory ||
      !f.FreeMemory || !f.BindImageMemory || !f.CmdPipelineBarrier || !f.CmdCopyImage ||
      !f.CreateEvent || !f.DestroyEvent || !f.GetEventStatus || !f.SetEvent || !f.ResetEvent ||
      !f.CmdSetEvent || !f.CmdWaitEvents)
    return Fail("a Vulkan entry point did not resolve");
  v.device = device;
  return true;
}

using PFN_D3D12CreateDevice_ = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using PFN_CreateDXGIFactory1_ = HRESULT(WINAPI*)(REFIID, void**);

// The private D3D12 device on the NVIDIA adapter (a laptop also has an integrated GPU).
inline bool EnsureD3D12() {
  if (v.dev12 != nullptr) return true;
  HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
  auto create_factory = dxgi != nullptr ? (PFN_CreateDXGIFactory1_)GetProcAddress(dxgi, "CreateDXGIFactory1")
                                        : nullptr;
  IDXGIFactory1* factory = nullptr;
  IDXGIAdapter1* chosen = nullptr;
  if (create_factory != nullptr && SUCCEEDED(create_factory(IID_PPV_ARGS(&factory)))) {
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc = {};
      adapter->GetDesc1(&desc);
      if (desc.VendorId == 0x10DE && chosen == nullptr) {
        chosen = adapter;
        continue;
      }
      adapter->Release();
    }
    factory->Release();
  }
  HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
  auto create = d3d12 != nullptr ? (PFN_D3D12CreateDevice_)GetProcAddress(d3d12, "D3D12CreateDevice")
                                 : nullptr;
  if (create == nullptr) {
    if (chosen != nullptr) chosen->Release();
    return Fail("D3D12CreateDevice not found");
  }
  HRESULT hr = E_FAIL;
  for (D3D_FEATURE_LEVEL level :
       {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_0}) {
    hr = create(nullptr, level, IID_PPV_ARGS(&v.dev12));  // the default adapter, as dlss5-bridge
    if (SUCCEEDED(hr)) {
      ngx_probe::Logf("nr-vulkan: private D3D12 device at feature level 0x%x%s", (unsigned)level,
                      chosen != nullptr ? " on the NVIDIA adapter" : " on the default adapter");
      break;
    }
  }
  if (chosen != nullptr) chosen->Release();
  if (FAILED(hr) || v.dev12 == nullptr) return Fail("D3D12CreateDevice failed", hr);
  return true;
}

inline bool EnsureDepthPipeline(reshade::api::device* dev) {
  using namespace reshade::api;
  if (v.pipeline.handle != 0) return true;
  descriptor_range src = {};
  src.binding = 0;
  src.count = 1;
  src.visibility = shader_stage::compute;
  src.type = descriptor_type::texture_shader_resource_view;
  descriptor_range dst = src;
  dst.dx_register_space = 1;
  dst.type = descriptor_type::texture_unordered_access_view;
  const pipeline_layout_param params[2] = {pipeline_layout_param(src), pipeline_layout_param(dst)};
  if (!dev->create_pipeline_layout(2, params, &v.layout))
    return Fail("depth conversion pipeline layout failed");
  shader_desc cs = {};
  cs.code = nr_vk_depth_spv;
  cs.code_size = sizeof(nr_vk_depth_spv);
  cs.entry_point = "main";
  pipeline_subobject sub = {pipeline_subobject_type::compute_shader, 1, &cs};
  if (!dev->create_pipeline(v.layout, 1, &sub, &v.pipeline))
    return Fail("depth conversion pipeline failed");
  cs.code = nr_vk_depth_flip_spv;
  cs.code_size = sizeof(nr_vk_depth_flip_spv);
  if (!dev->create_pipeline(v.layout, 1, &sub, &v.pipeline_flip))
    return Fail("depth conversion pipeline failed");
  return true;
}

inline bool EnsureDepth32(reshade::api::device* dev, UINT w, UINT h) {
  using namespace reshade::api;
  if (v.depth32.handle != 0 && v.depth32_w == w && v.depth32_h == h) return true;
  if (v.depth32_uav.handle != 0) dev->destroy_resource_view(v.depth32_uav);
  if (v.depth32.handle != 0) dev->destroy_resource(v.depth32);
  v.depth32 = {0};
  v.depth32_uav = {0};
  const resource_desc desc(w, h, 1, 1, format::r32_float, 1, memory_heap::gpu_only,
                           resource_usage::unordered_access | resource_usage::copy_source);
  if (!dev->create_resource(desc, nullptr, resource_usage::copy_source, &v.depth32) ||
      !dev->create_resource_view(v.depth32, resource_usage::unordered_access,
                                 resource_view_desc(format::r32_float), &v.depth32_uav))
    return Fail("depth conversion target failed");
  v.depth32_w = w;
  v.depth32_h = h;
  return true;
}

inline void Release(Shared& t) {
  if (t.image != 0 && v.vk.DestroyImage != nullptr) v.vk.DestroyImage(v.device, t.image, nullptr);
  if (t.memory != 0 && v.vk.FreeMemory != nullptr) v.vk.FreeMemory(v.device, t.memory, nullptr);
  if (t.tex12 != nullptr) t.tex12->Release();
  t = {};
}

// A texture created shared on D3D12 (simultaneous access: Vulkan does no D3D12 transitions) and
// imported into the game's VkDevice as a dedicated allocation.
inline bool EnsureShared(Shared& t, UINT w, UINT h, DXGI_FORMAT format, const char* name) {
  if (t.tex12 != nullptr && t.w == w && t.h == h && t.format == format) return true;
  Release(t);
  const uint32_t vk_format = VkFormatOf(format);
  if (vk_format == 0) {
    char why[96];
    std::snprintf(why, sizeof(why), "no Vulkan format for the %s (DXGI %u)", name, (unsigned)format);
    return Fail(why);
  }
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = w;
  desc.Height = h;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  HANDLE handle = nullptr;
  HRESULT hr = v.dev12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                IID_PPV_ARGS(&t.tex12));
  if (SUCCEEDED(hr)) hr = v.dev12->CreateSharedHandle(t.tex12, nullptr, GENERIC_ALL, nullptr, &handle);
  if (FAILED(hr)) {
    Release(t);
    return Fail("shared D3D12 texture failed", hr);
  }

  const Vk& f = v.vk;
  VkExternalMemoryImageCreateInfo_ ext = {kStExternalMemoryImage, nullptr, kHandleD3D12Resource};
  VkImageCreateInfo_ ci = {};
  ci.sType = kStImageCreateInfo;
  ci.pNext = &ext;
  ci.imageType = 1;  // 2D
  ci.format = vk_format;
  ci.extent = {w, h, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = 1;
  ci.tiling = 0;     // optimal
  ci.usage = 1 | 2;  // transfer src | dst
  int32_t r = f.CreateImage(v.device, &ci, nullptr, &t.image);
  VkMemoryRequirements_ req = {};
  VkMemoryWin32HandleProperties_ props = {kStMemoryWin32Props, nullptr, 0};
  if (r == 0) {
    f.GetImageMemoryRequirements(v.device, t.image, &req);
    r = f.GetMemoryWin32HandleProperties(v.device, kHandleD3D12Resource, handle, &props);
  }
  uint32_t bits = req.memoryTypeBits & (props.memoryTypeBits != 0 ? props.memoryTypeBits : ~0u);
  if (bits == 0) bits = req.memoryTypeBits;
  uint32_t type = 0;
  while (type < 32 && (bits & (1u << type)) == 0) ++type;
  if (r == 0 && type < 32) {
    VkMemoryDedicatedAllocateInfo_ dedicated = {kStMemoryDedicated, nullptr, t.image, 0};
    VkImportMemoryWin32HandleInfo_ import = {kStImportMemoryWin32, &dedicated, kHandleD3D12Resource,
                                             handle, nullptr};
    VkMemoryAllocateInfo_ ai = {kStMemoryAllocateInfo, &import, req.size, type};
    r = f.AllocateMemory(v.device, &ai, nullptr, &t.memory);
    if (r == 0) r = f.BindImageMemory(v.device, t.image, t.memory, 0);
  } else if (r == 0) {
    r = -1;
  }
  CloseHandle(handle);
  if (r != 0) {
    Release(t);
    char why[96];
    std::snprintf(why, sizeof(why), "importing the shared %s into Vulkan failed", name);
    return Fail(why, r);
  }
  t.w = w;
  t.h = h;
  t.format = format;
  ngx_probe::Logf("nr-vulkan: shared %s %ux%u format=%u imported (memory type %u)", name, w, h,
                  (unsigned)format, type);
  return true;
}

// --- per frame ----------------------------------------------------------------

inline void ImageBarrier(VkCmd cb, VkH image, uint32_t from, uint32_t to) {
  VkImageMemoryBarrier_ b = {};
  b.sType = kStImageMemoryBarrier;
  b.srcAccessMask = kAccessRead | kAccessWrite;
  b.dstAccessMask = kAccessRead | kAccessWrite;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = kQueueFamilyIgnored;
  b.image = image;
  b.subresourceRange = {1, 0, 1, 0, 1};
  v.vk.CmdPipelineBarrier(cb, kStageAll, kStageAll, 0, 0, nullptr, 0, nullptr, 1, &b);
}

inline void CopyImage(VkCmd cb, VkH src, uint32_t src_layout, VkH dst, uint32_t dst_layout, UINT w,
                      UINT h) {
  VkImageCopy_ region = {};
  region.srcSubresource = {1, 0, 0, 1};
  region.dstSubresource = {1, 0, 0, 1};
  region.extent = {w, h, 1};
  v.vk.CmdCopyImage(cb, src, src_layout, dst, dst_layout, 1, &region);
}

// --- the park -------------------------------------------------------------------

// Polls without burning a core: yields for the first millisecond, then sleeps.
inline bool PollEvent(VkH event, DWORD timeout_ms) {
  const ULONGLONG t0 = GetTickCount64();
  for (;;) {
    if (v.vk.GetEventStatus(v.device, event) == (int32_t)kEventSet) return true;
    const ULONGLONG waited = GetTickCount64() - t0;
    if (waited > timeout_ms) return false;
    if (waited == 0) SwitchToThread(); else Sleep(1);
  }
}

// Waits for the copies of the armed frame to execute, runs the D3D12 pass and releases the
// game's queue. Releasing is unconditional: a parked queue whose event is never set is a lost
// device.
inline DWORD WINAPI ParkWorker(LPVOID) {
  for (;;) {
    WaitForSingleObject(v.wake, INFINITE);
    if (v.stop != 0) break;
    const int p = v.pair;
    if (PollEvent(v.ev_in[p], 2000))
      nr_runner::SubmitStandaloneAndWait(1500);
    else
      InterlockedExchange(&v.missed, 1);
    v.vk.SetEvent(v.device, v.ev_out[p]);
    SetEvent(v.done);
  }
  return 0;
}

inline bool EnsurePark() {
  if (v.worker != nullptr) return true;
  const VkEventCreateInfo_ ci = {kStEventCreateInfo, nullptr, 0};
  for (int i = 0; i < 2; ++i)
    if (v.vk.CreateEvent(v.device, &ci, nullptr, &v.ev_in[i]) != 0 ||
        v.vk.CreateEvent(v.device, &ci, nullptr, &v.ev_out[i]) != 0)
      return Fail("vkCreateEvent failed");
  v.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  v.done = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  if (v.wake == nullptr || v.done == nullptr) return Fail("CreateEvent failed");
  v.worker = CreateThread(nullptr, 0, ParkWorker, nullptr, 0, nullptr);
  if (v.worker == nullptr) return Fail("the park's worker thread did not start");
  return true;
}

// Collects the frame handed to the worker last. False if the transport has to stop.
inline bool CollectPark() {
  if (!v.armed) return true;
  if (WaitForSingleObject(v.done, 3000) != WAIT_OBJECT_0) {
    for (int i = 0; i < 2; ++i) v.vk.SetEvent(v.device, v.ev_out[i]);
    return Fail("the D3D12 pass did not finish within 3 s");
  }
  v.armed = false;
  if (InterlockedExchange(&v.missed, 0) != 0)
    return Fail("the game's queue never reached the copies (the frame was not submitted)");
  v.pair ^= 1;
  return true;
}

inline void StopPark() {
  if (v.worker != nullptr) {
    InterlockedExchange(&v.stop, 1);
    SetEvent(v.wake);
    WaitForSingleObject(v.worker, 5000);
    CloseHandle(v.worker);
  }
  for (int i = 0; i < 2; ++i) {
    if (v.device != nullptr && v.vk.SetEvent != nullptr) {
      if (v.ev_out[i] != 0) v.vk.SetEvent(v.device, v.ev_out[i]);
    }
  }
  if (v.device != nullptr && v.vk.DestroyEvent != nullptr)
    for (int i = 0; i < 2; ++i) {
      if (v.ev_in[i] != 0) v.vk.DestroyEvent(v.device, v.ev_in[i], nullptr);
      if (v.ev_out[i] != 0) v.vk.DestroyEvent(v.device, v.ev_out[i], nullptr);
    }
  if (v.wake != nullptr) CloseHandle(v.wake);
  if (v.done != nullptr) CloseHandle(v.done);
}

inline DXGI_FORMAT ColorFormat(DXGI_FORMAT f) {
  switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
  }
}

// One frame, right after the motion-vector effect rendered. back_buffer is a render target, the
// depth and motion views are shader resources (ReShade's states).
inline void OnFrame(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                    reshade::api::resource back_buffer, reshade::api::resource_view depth_srv,
                    reshade::api::resource motion, bool depth_inverted, bool depth_upside_down,
                    float scale_x, float scale_y) {
  using namespace reshade::api;
  if (v.failed || back_buffer.handle == 0) return;
  if (!nr_runner::StandaloneAllowed()) return;
  if (depth_srv.handle == 0 || motion.handle == 0) {
    std::snprintf(nr_runner::s.sa_status, sizeof(nr_runner::s.sa_status), "waiting: %s",
                  depth_srv.handle == 0 ? "no depth buffer bound (Generic Depth)"
                                        : "no motion vectors (enable iMMERSE Launchpad)");
    return;
  }
  device* dev = runtime->get_device();
  if (v.device == nullptr && !LoadVk(reinterpret_cast<VkDev>(dev->get_native()))) return;
  if (!EnsureD3D12() || !EnsureDepthPipeline(dev) || !EnsurePark()) return;
  // The previous frame's pass has to be done before this one is recorded on the same list.
  if (!CollectPark()) return;

  const resource_desc bd = dev->get_resource_desc(back_buffer);
  const resource_desc md = dev->get_resource_desc(motion);
  const resource depth_res = dev->get_resource_from_view(depth_srv);
  const resource_desc dd = dev->get_resource_desc(depth_res);
  if (bd.texture.samples > 1) {
    Fail("multisampled back buffer is not supported");
    return;
  }
  const DXGI_FORMAT color_format = ColorFormat(static_cast<DXGI_FORMAT>(bd.texture.format));
  DXGI_FORMAT motion_format = static_cast<DXGI_FORMAT>(md.texture.format);
  if (motion_format == DXGI_FORMAT_R16G16_TYPELESS) motion_format = DXGI_FORMAT_R16G16_FLOAT;
  // A resize replaces the shared images; the queue must be done with the old ones first.
  if (v.color.tex12 != nullptr &&
      (v.color.w != bd.texture.width || v.color.h != bd.texture.height ||
       v.depth.w != dd.texture.width || v.depth.h != dd.texture.height ||
       v.motion.w != md.texture.width || v.motion.h != md.texture.height))
    runtime->get_command_queue()->wait_idle();
  if (!EnsureShared(v.color, bd.texture.width, bd.texture.height, color_format, "colour") ||
      !EnsureShared(v.motion, md.texture.width, md.texture.height, motion_format, "motion") ||
      !EnsureShared(v.depth, dd.texture.width, dd.texture.height, DXGI_FORMAT_R32_FLOAT, "depth") ||
      !EnsureDepth32(dev, dd.texture.width, dd.texture.height))
    return;

  // In: depth converted on ReShade's API, then the three copies into the shared images.
  cmd_list->barrier(v.depth32, resource_usage::copy_source, resource_usage::unordered_access);
  cmd_list->bind_pipeline(pipeline_stage::all_compute,
                          depth_upside_down ? v.pipeline_flip : v.pipeline);
  cmd_list->push_descriptors(shader_stage::compute, v.layout, 0,
                             descriptor_table_update{{}, 0, 0, 1,
                                                     descriptor_type::texture_shader_resource_view,
                                                     &depth_srv});
  cmd_list->push_descriptors(shader_stage::compute, v.layout, 1,
                             descriptor_table_update{{}, 0, 0, 1,
                                                     descriptor_type::texture_unordered_access_view,
                                                     &v.depth32_uav});
  cmd_list->dispatch((dd.texture.width + 7) / 8, (dd.texture.height + 7) / 8, 1);
  cmd_list->barrier(v.depth32, resource_usage::unordered_access, resource_usage::copy_source);
  cmd_list->barrier(back_buffer, resource_usage::render_target, resource_usage::copy_source);
  cmd_list->barrier(motion, resource_usage::shader_resource, resource_usage::copy_source);

  VkCmd cb = reinterpret_cast<VkCmd>(cmd_list->get_native());
  // The shared images live in GENERAL, as dlss5-bridge keeps them; the GENERAL to GENERAL barrier
  // every frame is the memory dependency against what the D3D12 side wrote.
  for (Shared* t : {&v.color, &v.depth, &v.motion}) {
    ImageBarrier(cb, t->image, t->laid_out ? kLayoutGeneral : kLayoutUndefined, kLayoutGeneral);
    t->laid_out = true;
  }
  CopyImage(cb, back_buffer.handle, kLayoutTransferSrc, v.color.image, kLayoutGeneral, v.color.w,
            v.color.h);
  CopyImage(cb, v.depth32.handle, kLayoutTransferSrc, v.depth.image, kLayoutGeneral, v.depth.w,
            v.depth.h);
  CopyImage(cb, motion.handle, kLayoutTransferSrc, v.motion.image, kLayoutGeneral, v.motion.w,
            v.motion.h);
  cmd_list->barrier(motion, resource_usage::copy_source, resource_usage::shader_resource);

  // The D3D12 pass is recorded now and submitted by the worker once the copies have executed.
  const bool recorded =
      nr_runner::OnStandaloneFrame(v.color.tex12, v.depth.tex12, v.motion.tex12, depth_inverted,
                                   scale_x, scale_y, D3D12_RESOURCE_STATE_COMMON);
  if (!recorded) {
    cmd_list->barrier(back_buffer, resource_usage::copy_source, resource_usage::render_target);
    return;
  }

  // Park: announce the copies, stall the queue until the worker releases it, copy the result
  // back. The pair was used two frames ago; the worker has since seen the other pair's first
  // event, which the queue reached only after passing this pair's wait, so resetting is safe.
  const int p = v.pair;
  v.vk.ResetEvent(v.device, v.ev_in[p]);
  v.vk.ResetEvent(v.device, v.ev_out[p]);
  v.vk.CmdSetEvent(cb, v.ev_in[p], kStageAll);
  const VkMemoryBarrier_ mb = {kStMemoryBarrier, nullptr, kAccessRead | kAccessWrite,
                               kAccessRead | kAccessWrite};
  v.vk.CmdWaitEvents(cb, 1, &v.ev_out[p], kStageHost | kStageAll, kStageAll, 1, &mb, 0, nullptr,
                     0, nullptr);
  ImageBarrier(cb, v.color.image, kLayoutGeneral, kLayoutGeneral);
  cmd_list->barrier(back_buffer, resource_usage::copy_source, resource_usage::copy_dest);
  CopyImage(cb, v.color.image, kLayoutGeneral, back_buffer.handle, kLayoutTransferDst, v.color.w,
            v.color.h);
  cmd_list->barrier(back_buffer, resource_usage::copy_dest, resource_usage::render_target);
  ResetEvent(v.done);
  v.armed = true;
  SetEvent(v.wake);
}

inline void Shutdown(reshade::api::device* dev) {
  StopPark();
  Release(v.color);
  Release(v.depth);
  Release(v.motion);
  if (dev != nullptr) {
    if (v.depth32_uav.handle != 0) dev->destroy_resource_view(v.depth32_uav);
    if (v.depth32.handle != 0) dev->destroy_resource(v.depth32);
    if (v.pipeline.handle != 0) dev->destroy_pipeline(v.pipeline);
    if (v.pipeline_flip.handle != 0) dev->destroy_pipeline(v.pipeline_flip);
    if (v.layout.handle != 0) dev->destroy_pipeline_layout(v.layout);
  }
  if (v.dev12 != nullptr) v.dev12->Release();
  v = {};
}

}  // namespace nr_vulkan
