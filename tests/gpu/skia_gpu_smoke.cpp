/**
 * @file skia_gpu_smoke.cpp
 * @brief Proves a Graphite/Vulkan Skia build works: device -> Context -> 64x64
 *        gradient -> readback -> PNG.
 *
 * This is the acceptance check for plan step 2.1 (doc/gpu-migration/GPU-RENDER-PLAN.md).
 * It is deliberately standalone and links Skia directly, NOT libopenshot: in a
 * normal build libopenshot is compiled against the CPU Skia in /usr/local, and
 * pulling both static archives into one binary would be a mess.
 *
 * The Vulkan bootstrap below is throwaway. Step 2.2 replaces it with
 * src/gpu/GpuDevice, which owns the instance, device, queue and Context for the
 * whole process; nothing here should be treated as the shape of that class.
 *
 *   OPENSHOT_GPU_DEVICE=<n>   pick physical device n instead of auto-selecting
 *   argv[1]                   output PNG path (default: skia_gpu_smoke.png)
 */

#include "include/core/SkAlphaType.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkGradient.h"
#include "include/encode/SkPngEncoder.h"

#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"

#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanPreferredFeatures.h"

// Private, but the symbols are in libskia.a and install_skia_gpu.sh puts these
// two self-contained headers in the prefix. Graphite requires the caller to hand
// it a VulkanMemoryAllocator and Skia's VMA-backed one is not otherwise reachable.
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include <cstdio>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

// ---------------------------------------------------------------------------
// Vulkan bootstrap
// ---------------------------------------------------------------------------

struct VulkanDevice {
    VkInstance       instance   = VK_NULL_HANDLE;
    VkPhysicalDevice physical   = VK_NULL_HANDLE;
    VkDevice         device     = VK_NULL_HANDLE;
    VkQueue          queue      = VK_NULL_HANDLE;
    uint32_t         queueIndex = 0;
    uint32_t         apiVersion = 0;
    std::string      name;

    // Skia holds pointers into these for the lifetime of the Context.
    std::vector<const char*>       enabledInstanceExts;
    std::vector<const char*>       enabledDeviceExts;
    VkPhysicalDeviceFeatures2      features2{};
    skgpu::VulkanPreferredFeatures skiaFeatures;
    skgpu::VulkanExtensions        extensions;
};

PFN_vkVoidFunction smokeGetProc(const char* name, VkInstance instance, VkDevice device) {
    if (device != VK_NULL_HANDLE) {
        return vkGetDeviceProcAddr(device, name);
    }
    return vkGetInstanceProcAddr(instance, name);
}

bool createVulkanDevice(VulkanDevice* out) {
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS) {
        std::fprintf(stderr, "vkEnumerateInstanceVersion failed\n");
        return false;
    }
    // Skia's floor is 1.1; VulkanPreferredFeatures' ceiling is 1.4. Ask for 1.3,
    // which every driver we care about (NVIDIA 5xx, lavapipe) supports.
    const uint32_t apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3
                                                                   : VK_API_VERSION_1_1;
    out->apiVersion = apiVersion;
    out->skiaFeatures.init(apiVersion);

    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExts(instExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExts.data());
    out->skiaFeatures.addToInstanceExtensions(instExts.data(), instExts.size(),
                                              out->enabledInstanceExts);

    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "openshot-gpu-smoke";
    app.apiVersion         = apiVersion;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo        = &app;
    instInfo.enabledExtensionCount   = static_cast<uint32_t>(out->enabledInstanceExts.size());
    instInfo.ppEnabledExtensionNames = out->enabledInstanceExts.data();
    if (vkCreateInstance(&instInfo, nullptr, &out->instance) != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateInstance failed (no Vulkan ICD? try VK_ICD_FILENAMES)\n");
        return false;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(out->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::fprintf(stderr, "no Vulkan physical devices\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(out->instance, &deviceCount, devices.data());

    int chosen = -1;
    if (const char* env = std::getenv("OPENSHOT_GPU_DEVICE")) {
        const int idx = std::atoi(env);
        if (idx < 0 || idx >= static_cast<int>(deviceCount)) {
            std::fprintf(stderr, "OPENSHOT_GPU_DEVICE=%d out of range (%u devices)\n",
                         idx, deviceCount);
            return false;
        }
        chosen = idx;
    }
    VkPhysicalDeviceProperties props{};
    for (uint32_t i = 0; i < deviceCount && chosen < 0; ++i) {
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.apiVersion >= apiVersion &&
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosen = static_cast<int>(i);
        }
    }
    for (uint32_t i = 0; i < deviceCount && chosen < 0; ++i) {
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.apiVersion >= apiVersion) {
            chosen = static_cast<int>(i);
        }
    }
    if (chosen < 0) {
        std::fprintf(stderr, "no physical device supports Vulkan %u.%u\n",
                     VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion));
        return false;
    }
    out->physical = devices[chosen];
    vkGetPhysicalDeviceProperties(out->physical, &props);
    out->name = props.deviceName;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(out->physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(out->physical, &familyCount, families.data());
    bool foundQueue = false;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            out->queueIndex = i;
            foundQueue = true;
            break;
        }
    }
    if (!foundQueue) {
        std::fprintf(stderr, "no graphics queue family\n");
        return false;
    }

    uint32_t devExtCount = 0;
    vkEnumerateDeviceExtensionProperties(out->physical, nullptr, &devExtCount, nullptr);
    std::vector<VkExtensionProperties> devExts(devExtCount);
    vkEnumerateDeviceExtensionProperties(out->physical, nullptr, &devExtCount, devExts.data());

    out->features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    out->skiaFeatures.addFeaturesToQuery(devExts.data(), devExts.size(), out->features2);
    vkGetPhysicalDeviceFeatures2(out->physical, &out->features2);
    out->skiaFeatures.addFeaturesToEnable(out->enabledDeviceExts, out->features2);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = out->queueIndex;
    queueInfo.queueCount       = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo devInfo{};
    devInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.pNext                   = &out->features2;
    devInfo.queueCreateInfoCount    = 1;
    devInfo.pQueueCreateInfos       = &queueInfo;
    devInfo.enabledExtensionCount   = static_cast<uint32_t>(out->enabledDeviceExts.size());
    devInfo.ppEnabledExtensionNames = out->enabledDeviceExts.data();
    if (vkCreateDevice(out->physical, &devInfo, nullptr, &out->device) != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateDevice failed\n");
        return false;
    }
    vkGetDeviceQueue(out->device, out->queueIndex, 0, &out->queue);

    out->extensions.init(smokeGetProc, out->instance, out->physical,
                         static_cast<uint32_t>(out->enabledInstanceExts.size()),
                         out->enabledInstanceExts.data(),
                         static_cast<uint32_t>(out->enabledDeviceExts.size()),
                         out->enabledDeviceExts.data());
    return true;
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

struct ReadbackResult {
    std::unique_ptr<const SkImage::AsyncReadResult> result;
    bool done = false;
};

void onReadback(SkImage::ReadPixelsContext ctx,
                std::unique_ptr<const SkImage::AsyncReadResult> result) {
    auto* out = static_cast<ReadbackResult*>(ctx);
    out->result = std::move(result);
    out->done = true;
}

}  // namespace

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "skia_gpu_smoke.png";

    VulkanDevice vk;
    if (!createVulkanDevice(&vk)) {
        return fail("Vulkan device creation");
    }
    std::printf("device:   %s (Vulkan %u.%u)\n", vk.name.c_str(),
                VK_API_VERSION_MAJOR(vk.apiVersion), VK_API_VERSION_MINOR(vk.apiVersion));

    skgpu::VulkanBackendContext backend{};
    backend.fInstance           = vk.instance;
    backend.fPhysicalDevice     = vk.physical;
    backend.fDevice             = vk.device;
    backend.fQueue              = vk.queue;
    backend.fGraphicsQueueIndex = vk.queueIndex;
    backend.fMaxAPIVersion      = vk.apiVersion;
    backend.fVkExtensions       = &vk.extensions;
    backend.fDeviceFeatures2    = &vk.features2;
    backend.fGetProc            = smokeGetProc;
    backend.fMemoryAllocator =
            skgpu::VulkanMemoryAllocators::Make(backend, skgpu::ThreadSafe::kNo);
    if (!backend.fMemoryAllocator) {
        return fail("VulkanMemoryAllocators::Make");
    }

    skgpu::graphite::ContextOptions options;
    std::unique_ptr<skgpu::graphite::Context> context =
            skgpu::graphite::ContextFactory::MakeVulkan(backend, options);
    if (!context) {
        return fail("graphite::ContextFactory::MakeVulkan");
    }
    std::printf("context:  Graphite/Vulkan up\n");

    std::unique_ptr<skgpu::graphite::Recorder> recorder = context->makeRecorder();
    if (!recorder) {
        return fail("Context::makeRecorder");
    }

    // kRGBA_8888 on purpose: this is the colour type step 2.3 uses for GPU
    // surfaces, and getting it wrong is exactly the R/B swap the plan warns about.
    constexpr int kSize = 64;
    const SkImageInfo info = SkImageInfo::Make(kSize, kSize, kRGBA_8888_SkColorType,
                                               kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
    sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(recorder.get(), info);
    if (!surface) {
        return fail("SkSurfaces::RenderTarget");
    }

    const SkPoint pts[2] = {{0, 0}, {kSize, 0}};
    const SkColor4f colors[2] = {SkColors::kRed, SkColors::kBlue};
    const SkGradient gradient(SkGradient::Colors(colors, SkTileMode::kClamp),
                              SkGradient::Interpolation{});
    SkPaint paint;
    paint.setShader(SkShaders::LinearGradient(pts, gradient));
    surface->getCanvas()->clear(SK_ColorBLACK);
    surface->getCanvas()->drawRect(SkRect::MakeWH(kSize, kSize), paint);

    std::unique_ptr<skgpu::graphite::Recording> recording = recorder->snap();
    if (!recording) {
        return fail("Recorder::snap");
    }
    skgpu::graphite::InsertRecordingInfo insert;
    insert.fRecording = recording.get();
    if (context->insertRecording(insert) != skgpu::graphite::InsertStatus::kSuccess) {
        return fail("Context::insertRecording");
    }

    ReadbackResult readback;
    context->asyncRescaleAndReadPixels(surface.get(), info,
                                       SkIRect::MakeWH(kSize, kSize),
                                       SkImage::RescaleGamma::kSrc,
                                       SkImage::RescaleMode::kNearest,
                                       onReadback, &readback);
    if (!context->submit(skgpu::graphite::SyncToCpu::kYes)) {
        return fail("Context::submit");
    }
    for (int spins = 0; !readback.done && spins < 1000; ++spins) {
        context->checkAsyncWorkCompletion();
    }
    if (!readback.done || !readback.result) {
        return fail("async readback never completed");
    }

    SkPixmap pixmap(info, readback.result->data(0), readback.result->rowBytes(0));

    // The gradient runs red -> blue left to right. Reading these back through a
    // kRGBA_8888 pixmap is the channel-order check: on a raster N32 surface the
    // same bytes would come back BGRA and "red" would read as blue.
    const SkColor left  = pixmap.getColor(1, kSize / 2);
    const SkColor right = pixmap.getColor(kSize - 2, kSize / 2);
    std::printf("pixels:   left=%08x right=%08x\n", left, right);
    if (SkColorGetR(left) < 200 || SkColorGetB(left) > 60) {
        return fail("left edge of the gradient is not red (channel order?)");
    }
    if (SkColorGetB(right) < 200 || SkColorGetR(right) > 60) {
        return fail("right edge of the gradient is not blue (channel order?)");
    }

    {
        SkFILEWStream stream(outPath);
        if (!stream.isValid() || !SkPngEncoder::Encode(&stream, pixmap, {})) {
            return fail("PNG encode");
        }
    }
    std::printf("wrote:    %s\n", outPath);
    std::printf("PASS\n");

    // Tear down inside out. The AsyncReadResult goes first: its pixels are owned
    // by the Graphite context and are "immediately invalidated if the Graphite
    // context is abandoned or destroyed" (Context.h), so outliving the context
    // is a use-after-free. Then surfaces and recordings, which belong to the
    // recorder; then the recorder, which belongs to the context; then the
    // context, which borrows the Vulkan device.
    readback.result.reset();
    surface.reset();
    recording.reset();
    recorder.reset();
    context.reset();
    backend.fMemoryAllocator.reset();
    vkDestroyDevice(vk.device, nullptr);
    vkDestroyInstance(vk.instance, nullptr);
    return 0;
}
