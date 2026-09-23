// CUDA -> Vulkan interop. See CudaInterop.h for why it allocates its own images
// instead of using GpuSurfacePool's.

#include "CudaInterop.h"

#include "GpuDevice.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(OPENSHOT_HAVE_SKIA_GPU) && defined(OPENSHOT_HAVE_CUDA)
#define OPENSHOT_CUDA_INTEROP 1
#endif

#ifdef OPENSHOT_CUDA_INTEROP

#include <dlfcn.h>
#include <unistd.h>

#include <cuda.h>

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkSize.h"
#include "skia/include/gpu/GpuTypes.h"
#include "skia/include/gpu/graphite/BackendTexture.h"
#include "skia/include/gpu/graphite/Image.h"
#include "skia/include/gpu/graphite/Recorder.h"
#include "skia/include/gpu/graphite/TextureInfo.h"
#include "skia/include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "skia/include/gpu/vk/VulkanTypes.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#endif  // OPENSHOT_CUDA_INTEROP

using namespace openshot;

#ifdef OPENSHOT_CUDA_INTEROP

namespace openshot
{
namespace cuda_detail
{

// The driver API is dlopen'd, never linked: a machine with no NVIDIA driver has
// to keep working, and that is the majority of them. The types come from cuda.h
// through decltype, so a wrong signature is a compile error rather than a crash;
// the *names* go through the two-step stringify so that cuda.h's own versioning
// macros (cuMemcpy2DAsync -> cuMemcpy2DAsync_v2) are applied before we ask the
// loader for them.
#define OPENSHOT_CU_STR2(x) #x
#define OPENSHOT_CU_STR(x) OPENSHOT_CU_STR2(x)

struct CudaApi
{
	void* library = nullptr;

	decltype(&cuInit) Init = nullptr;
	decltype(&cuGetErrorName) GetErrorName = nullptr;
	decltype(&cuDeviceGetCount) DeviceGetCount = nullptr;
	decltype(&cuDeviceGet) DeviceGet = nullptr;
	decltype(&cuDeviceGetName) DeviceGetName = nullptr;
	decltype(&cuDeviceGetUuid) DeviceGetUuid = nullptr;
	decltype(&cuDevicePrimaryCtxRetain) PrimaryCtxRetain = nullptr;
	decltype(&cuDevicePrimaryCtxRelease) PrimaryCtxRelease = nullptr;
	decltype(&cuCtxPushCurrent) CtxPushCurrent = nullptr;
	decltype(&cuCtxPopCurrent) CtxPopCurrent = nullptr;
	decltype(&cuStreamCreate) StreamCreate = nullptr;
	decltype(&cuStreamDestroy) StreamDestroy = nullptr;
	decltype(&cuStreamSynchronize) StreamSynchronize = nullptr;
	decltype(&cuImportExternalMemory) ImportExternalMemory = nullptr;
	decltype(&cuDestroyExternalMemory) DestroyExternalMemory = nullptr;
	decltype(&cuExternalMemoryGetMappedMipmappedArray) GetMappedMipmappedArray = nullptr;
	decltype(&cuMipmappedArrayGetLevel) MipmappedArrayGetLevel = nullptr;
	decltype(&cuMipmappedArrayDestroy) MipmappedArrayDestroy = nullptr;
	decltype(&cuImportExternalSemaphore) ImportExternalSemaphore = nullptr;
	decltype(&cuDestroyExternalSemaphore) DestroyExternalSemaphore = nullptr;
	decltype(&cuSignalExternalSemaphoresAsync) SignalExternalSemaphores = nullptr;
	decltype(&cuWaitExternalSemaphoresAsync) WaitExternalSemaphores = nullptr;
	decltype(&cuMemcpy2DAsync) Memcpy2DAsync = nullptr;

	/// Human-readable name for a driver error, for lastError()
	std::string message(const char* what, CUresult result) const
	{
		const char* name = nullptr;
		if (GetErrorName)
			GetErrorName(result, &name);
		return std::string(what) + " failed: " + (name ? name : "unknown CUDA error");
	}
};

bool loadCuda(CudaApi& api, std::string& error)
{
	// The stub (libcuda.so, from the -dev package) is not enough; ask for the
	// driver's own soname, which only exists where there is a driver.
	api.library = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
	if (!api.library) {
		error = "no CUDA driver (libcuda.so.1 will not load)";
		return false;
	}

	const char* missing = nullptr;
	const auto load = [&](void*& slot, const char* name) {
		if (missing)
			return;
		slot = dlsym(api.library, name);
		if (!slot)
			missing = name;
	};
#define OPENSHOT_CU_LOAD(field, symbol)                                     \
	do {                                                                    \
		void* address = nullptr;                                            \
		load(address, OPENSHOT_CU_STR(symbol));                             \
		api.field = reinterpret_cast<decltype(api.field)>(address);         \
	} while (0)

	OPENSHOT_CU_LOAD(Init, cuInit);
	OPENSHOT_CU_LOAD(GetErrorName, cuGetErrorName);
	OPENSHOT_CU_LOAD(DeviceGetCount, cuDeviceGetCount);
	OPENSHOT_CU_LOAD(DeviceGet, cuDeviceGet);
	OPENSHOT_CU_LOAD(DeviceGetName, cuDeviceGetName);
	OPENSHOT_CU_LOAD(DeviceGetUuid, cuDeviceGetUuid);
	OPENSHOT_CU_LOAD(PrimaryCtxRetain, cuDevicePrimaryCtxRetain);
	OPENSHOT_CU_LOAD(PrimaryCtxRelease, cuDevicePrimaryCtxRelease);
	OPENSHOT_CU_LOAD(CtxPushCurrent, cuCtxPushCurrent);
	OPENSHOT_CU_LOAD(CtxPopCurrent, cuCtxPopCurrent);
	OPENSHOT_CU_LOAD(StreamCreate, cuStreamCreate);
	OPENSHOT_CU_LOAD(StreamDestroy, cuStreamDestroy);
	OPENSHOT_CU_LOAD(StreamSynchronize, cuStreamSynchronize);
	OPENSHOT_CU_LOAD(ImportExternalMemory, cuImportExternalMemory);
	OPENSHOT_CU_LOAD(DestroyExternalMemory, cuDestroyExternalMemory);
	OPENSHOT_CU_LOAD(GetMappedMipmappedArray, cuExternalMemoryGetMappedMipmappedArray);
	OPENSHOT_CU_LOAD(MipmappedArrayGetLevel, cuMipmappedArrayGetLevel);
	OPENSHOT_CU_LOAD(MipmappedArrayDestroy, cuMipmappedArrayDestroy);
	OPENSHOT_CU_LOAD(ImportExternalSemaphore, cuImportExternalSemaphore);
	OPENSHOT_CU_LOAD(DestroyExternalSemaphore, cuDestroyExternalSemaphore);
	OPENSHOT_CU_LOAD(SignalExternalSemaphores, cuSignalExternalSemaphoresAsync);
	OPENSHOT_CU_LOAD(WaitExternalSemaphores, cuWaitExternalSemaphoresAsync);
	OPENSHOT_CU_LOAD(Memcpy2DAsync, cuMemcpy2DAsync);
#undef OPENSHOT_CU_LOAD

	if (missing) {
		error = std::string("the CUDA driver has no ") + missing;
		dlclose(api.library);
		api.library = nullptr;
		return false;
	}
	return true;
}

struct InteropState;

/// One exportable image: the Vulkan side, the CUDA side, and the barrier that
/// puts it back into the layout CUDA needs.
struct ImageState
{
	InteropState* owner = nullptr;
	int width = 0;
	int height = 0;
	GpuImage::Format format = GpuImage::Format::R8;

	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize memory_size = 0;
	VkCommandBuffer to_general = VK_NULL_HANDLE;

	CUexternalMemory external = nullptr;
	CUmipmappedArray mipmap = nullptr;
	CUarray array = nullptr;

	// The pair's two binary semaphores, held by its luma (R8) image; null on a
	// chroma image. Per pair, not per interop, because one pair's cycle is
	// self-ordered -- its barrier is submitted after the draw that waited on its
	// copy -- while two pairs sharing one semaphore are not: with two readers,
	// the second pair could signal it again before the first signal had been
	// waited on, a binary semaphore signalled twice, and a CUDA wait that never
	// returned stalled the stream NVDEC decodes on (W23, transitions.*).
	//
	// ready: Vulkan -> CUDA, orders the layout barrier before the copy.
	// done:  CUDA -> Vulkan, what the caller's submit waits on.
	VkSemaphore ready = VK_NULL_HANDLE;
	CUexternalSemaphore ready_cu = nullptr;
	VkSemaphore done = VK_NULL_HANDLE;
	CUexternalSemaphore done_cu = nullptr;
	/// ready has been signalled and not yet waited on, for this chroma image.
	/// Binary semaphores take one signal per wait.
	bool ready_signalled = false;
	const ImageState* ready_uv = nullptr;

	bool valid() const { return owner != nullptr; }

	VkFormat vkFormat() const
	{
		return format == GpuImage::Format::R8 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8_UNORM;
	}
	SkColorType colorType() const
	{
		return format == GpuImage::Format::R8 ? kR8_unorm_SkColorType
											  : kR8G8_unorm_SkColorType;
	}
	int bytesPerPixel() const { return format == GpuImage::Format::R8 ? 1 : 2; }
};

/// Everything the interop owns, all of it allocated against one VkDevice and one
/// CUDA context, and all of it released together when that device goes away.
struct InteropState
{
	bool initialised = false;
	bool usable = false;
	unsigned long long generation = 0;
	std::string error;
	std::string device_name;

	CudaApi cu;

	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	PFN_vkGetMemoryFdKHR getMemoryFd = nullptr;
	PFN_vkGetSemaphoreFdKHR getSemaphoreFd = nullptr;

	CUdevice cuda_device = 0;
	CUcontext context = nullptr;
	CUstream stream = nullptr;

	VkCommandPool command_pool = VK_NULL_HANDLE;

	std::mutex mutex;
	std::vector<ImageState*> images;
	/// release() has already drained the device, so releaseImage need not.
	bool draining = false;

	bool initialise();
	bool setup();
	void release();
	bool createImage(ImageState& state, int width, int height, GpuImage::Format format);
	void releaseImage(ImageState& state);
	bool copyNV12(const AVFrame* frame, ImageState& y, ImageState& uv, CUstream stream);
	bool submitBarriers(ImageState& y, ImageState& uv);

	bool createSemaphore(VkSemaphore& semaphore, CUexternalSemaphore& imported);

private:
	int memoryTypeIndex(uint32_t bits) const;
	bool recordBarrier(ImageState& state);
};

/// Makes the interop's CUDA context current for a scope. The context stack is
/// per thread, so this is safe on any thread that reaches the interop.
class ContextGuard
{
public:
	ContextGuard(const CudaApi& api, CUcontext context) : api(api), pushed(false)
	{
		if (context && api.CtxPushCurrent(context) == CUDA_SUCCESS)
			pushed = true;
	}
	~ContextGuard()
	{
		if (pushed) {
			CUcontext popped = nullptr;
			api.CtxPopCurrent(&popped);
		}
	}
	bool ok() const { return pushed; }

	ContextGuard(const ContextGuard&) = delete;
	ContextGuard& operator=(const ContextGuard&) = delete;

private:
	const CudaApi& api;
	bool pushed;
};

// A device teardown invalidates every handle below, so the whole thing is keyed
// on the generation and rebuilt after one. A failure latches until then, rather
// than retrying the whole bring-up on every frame.
bool InteropState::initialise()
{
	const unsigned long long current = GpuDevice::Generation();
	if (initialised && generation == current)
		return usable;

	release();
	error.clear();
	usable = setup();
	if (!usable) {
		const std::string reason = error;
		release();
		error = reason;
	}
	initialised = true;
	generation = current;
	return usable;
}

bool InteropState::setup()
{
	GpuDevice& gpu = GpuDevice::Instance();
	if (!gpu.available()) {
		error = "the GPU is off or unavailable: " + gpu.lastError();
		return false;
	}
	const GpuDevice::VulkanHandles* handles = gpu.vulkanHandles();
	if (!handles) {
		error = "no Vulkan handles (this build has no GPU Skia)";
		return false;
	}
	if (!handles->external_memory_fd || !handles->external_semaphore_fd) {
		// lavapipe has no external_semaphore_fd. Declining is the answer, not an
		// error: the caller keeps its host path.
		error = "this Vulkan device does not export memory and semaphores as fds";
		return false;
	}

	device = static_cast<VkDevice>(handles->device);
	physical_device = static_cast<VkPhysicalDevice>(handles->physical_device);
	queue = static_cast<VkQueue>(handles->queue);
	queue_family = handles->queue_family;
	getMemoryFd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
		vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
	getSemaphoreFd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
		vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
	if (!getMemoryFd || !getSemaphoreFd) {
		error = "the driver has no vkGetMemoryFdKHR / vkGetSemaphoreFdKHR";
		return false;
	}

	if (!loadCuda(cu, error))
		return false;
	CUresult result = cu.Init(0);
	if (result != CUDA_SUCCESS) {
		error = cu.message("cuInit", result);
		return false;
	}

	// The CUDA device has to be the same silicon as the Vulkan one — on a laptop
	// with an iGPU beside the NVIDIA card, "device 0" is not a safe guess. The
	// UUID is the only identifier both APIs agree on.
	VkPhysicalDeviceIDProperties id{};
	id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
	VkPhysicalDeviceProperties2 properties{};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &id;
	vkGetPhysicalDeviceProperties2(physical_device, &properties);

	int device_count = 0;
	cu.DeviceGetCount(&device_count);
	bool matched = false;
	for (int i = 0; i < device_count && !matched; ++i) {
		CUdevice candidate = 0;
		if (cu.DeviceGet(&candidate, i) != CUDA_SUCCESS)
			continue;
		CUuuid uuid{};
		if (cu.DeviceGetUuid(&uuid, candidate) != CUDA_SUCCESS)
			continue;
		if (std::memcmp(uuid.bytes, id.deviceUUID, sizeof(uuid.bytes)) != 0)
			continue;
		cuda_device = candidate;
		matched = true;
	}
	if (!matched) {
		error = "no CUDA device has the Vulkan device's UUID";
		return false;
	}

	char name[128] = {0};
	if (cu.DeviceGetName(name, sizeof(name) - 1, cuda_device) == CUDA_SUCCESS)
		device_name = name;

	// The primary context, which is also what FFmpeg's CUDA hwdevice uses by
	// default — so a decoder set up against this context produces frames these
	// copies can read (W23).
	result = cu.PrimaryCtxRetain(&context, cuda_device);
	if (result != CUDA_SUCCESS) {
		error = cu.message("cuDevicePrimaryCtxRetain", result);
		context = nullptr;
		return false;
	}

	{
		ContextGuard guard(cu, context);
		if (!guard.ok()) {
			error = "cuCtxPushCurrent failed";
			return false;
		}
		result = cu.StreamCreate(&stream, CU_STREAM_NON_BLOCKING);
		if (result != CUDA_SUCCESS) {
			error = cu.message("cuStreamCreate", result);
			stream = nullptr;
			return false;
		}
	}

	VkCommandPoolCreateInfo pool_info{};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = queue_family;
	if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
		error = "vkCreateCommandPool failed";
		command_pool = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

bool InteropState::createSemaphore(VkSemaphore& semaphore, CUexternalSemaphore& imported)
{
	VkExportSemaphoreCreateInfo export_info{};
	export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
	export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkSemaphoreCreateInfo semaphore_info{};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphore_info.pNext = &export_info;
	if (vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS) {
		error = "vkCreateSemaphore failed for an exportable semaphore";
		semaphore = VK_NULL_HANDLE;
		return false;
	}

	VkSemaphoreGetFdInfoKHR fd_info{};
	fd_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
	fd_info.semaphore = semaphore;
	fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	int fd = -1;
	if (getSemaphoreFd(device, &fd_info, &fd) != VK_SUCCESS || fd < 0) {
		error = "vkGetSemaphoreFdKHR failed";
		return false;
	}

	CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
	desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
	desc.handle.fd = fd;
	const CUresult result = cu.ImportExternalSemaphore(&imported, &desc);
	if (result != CUDA_SUCCESS) {
		// CUDA takes the fd on success and leaves it to us on failure.
		close(fd);
		imported = nullptr;
		error = cu.message("cuImportExternalSemaphore", result);
		return false;
	}
	return true;
}

int InteropState::memoryTypeIndex(uint32_t bits) const
{
	VkPhysicalDeviceMemoryProperties memory{};
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory);
	for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
		if ((bits & (1u << i)) &&
			(memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
			return static_cast<int>(i);
	return -1;
}

// One command buffer per image, holding one barrier, recorded once and
// resubmitted every frame. The old layout is UNDEFINED rather than whatever
// Skia last left it in: CUDA is about to overwrite every pixel, so discarding
// the contents is exactly right, and it means nothing has to track the layout
// across the Skia boundary. SIMULTANEOUS_USE is what lets it be resubmitted
// while an earlier submission of it may still be pending.
bool InteropState::recordBarrier(ImageState& state)
{
	VkCommandBufferAllocateInfo allocate{};
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = command_pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1;
	if (vkAllocateCommandBuffers(device, &allocate, &state.to_general) != VK_SUCCESS) {
		error = "vkAllocateCommandBuffers failed";
		state.to_general = VK_NULL_HANDLE;
		return false;
	}

	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	if (vkBeginCommandBuffer(state.to_general, &begin) != VK_SUCCESS) {
		error = "vkBeginCommandBuffer failed";
		return false;
	}

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = 0;   // CUDA's writes are made visible by the semaphore
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = state.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	// ALL_COMMANDS on the source side so the barrier also waits for everything
	// submitted to this queue earlier — which is where Skia's sampling of the
	// previous frame is.
	vkCmdPipelineBarrier(state.to_general, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
						 &barrier);
	if (vkEndCommandBuffer(state.to_general) != VK_SUCCESS) {
		error = "vkEndCommandBuffer failed";
		return false;
	}
	return true;
}

bool InteropState::createImage(ImageState& state, int width, int height,
							   GpuImage::Format format)
{
	state.width = width;
	state.height = height;
	state.format = format;

	VkExternalMemoryImageCreateInfo external_info{};
	external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
	external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkImageCreateInfo image_info{};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.pNext = &external_info;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = state.vkFormat();
	image_info.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vkCreateImage(device, &image_info, nullptr, &state.image) != VK_SUCCESS) {
		error = "vkCreateImage failed for an exportable image";
		state.image = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(device, state.image, &requirements);
	const int type = memoryTypeIndex(requirements.memoryTypeBits);
	if (type < 0) {
		error = "no device-local memory type for an exportable image";
		return false;
	}

	// CUDA imports an image only from a dedicated allocation, which is also why
	// this cannot come out of GpuSurfacePool: Skia's VMA suballocates.
	VkMemoryDedicatedAllocateInfo dedicated{};
	dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicated.image = state.image;

	VkExportMemoryAllocateInfo export_info{};
	export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
	export_info.pNext = &dedicated;
	export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

	VkMemoryAllocateInfo allocate{};
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.pNext = &export_info;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = static_cast<uint32_t>(type);
	if (vkAllocateMemory(device, &allocate, nullptr, &state.memory) != VK_SUCCESS) {
		error = "vkAllocateMemory failed for exportable memory";
		state.memory = VK_NULL_HANDLE;
		return false;
	}
	state.memory_size = requirements.size;
	if (vkBindImageMemory(device, state.image, state.memory, 0) != VK_SUCCESS) {
		error = "vkBindImageMemory failed";
		return false;
	}

	VkMemoryGetFdInfoKHR get_fd{};
	get_fd.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
	get_fd.memory = state.memory;
	get_fd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
	int fd = -1;
	if (getMemoryFd(device, &get_fd, &fd) != VK_SUCCESS || fd < 0) {
		error = "vkGetMemoryFdKHR failed";
		return false;
	}

	CUDA_EXTERNAL_MEMORY_HANDLE_DESC memory_desc{};
	memory_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
	memory_desc.handle.fd = fd;
	memory_desc.size = requirements.size;
	memory_desc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
	CUresult result = cu.ImportExternalMemory(&state.external, &memory_desc);
	if (result != CUDA_SUCCESS) {
		close(fd);   // CUDA takes the fd on success only
		state.external = nullptr;
		error = cu.message("cuImportExternalMemory", result);
		return false;
	}

	CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC array_desc{};
	array_desc.offset = 0;
	array_desc.numLevels = 1;
	array_desc.arrayDesc.Width = static_cast<size_t>(width);
	array_desc.arrayDesc.Height = static_cast<size_t>(height);
	array_desc.arrayDesc.Depth = 0;
	array_desc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
	array_desc.arrayDesc.NumChannels = static_cast<unsigned>(state.bytesPerPixel());
	array_desc.arrayDesc.Flags = 0;
	result = cu.GetMappedMipmappedArray(&state.mipmap, state.external, &array_desc);
	if (result != CUDA_SUCCESS) {
		state.mipmap = nullptr;
		error = cu.message("cuExternalMemoryGetMappedMipmappedArray", result);
		return false;
	}
	result = cu.MipmappedArrayGetLevel(&state.array, state.mipmap, 0);
	if (result != CUDA_SUCCESS) {
		state.array = nullptr;
		error = cu.message("cuMipmappedArrayGetLevel", result);
		return false;
	}

	if (format == GpuImage::Format::R8 &&
		(!createSemaphore(state.ready, state.ready_cu) ||
		 !createSemaphore(state.done, state.done_cu)))
		return false;

	return recordBarrier(state);
}

void InteropState::releaseImage(ImageState& state)
{
	// Nothing queued may still name this image or its semaphores. Only a reader
	// closing gets here outside release(), so a full drain costs nothing.
	if (!draining && state.image != VK_NULL_HANDLE && device != VK_NULL_HANDLE &&
		queue != VK_NULL_HANDLE) {
		GpuDevice::QueueGuard guard;
		vkQueueWaitIdle(queue);
	}
	if (state.external && stream && cu.StreamSynchronize)
		cu.StreamSynchronize(stream);
	if (state.ready_cu && cu.DestroyExternalSemaphore)
		cu.DestroyExternalSemaphore(state.ready_cu);
	if (state.done_cu && cu.DestroyExternalSemaphore)
		cu.DestroyExternalSemaphore(state.done_cu);
	state.ready_cu = nullptr;
	state.done_cu = nullptr;
	if (device != VK_NULL_HANDLE) {
		if (state.ready != VK_NULL_HANDLE)
			vkDestroySemaphore(device, state.ready, nullptr);
		if (state.done != VK_NULL_HANDLE)
			vkDestroySemaphore(device, state.done, nullptr);
	}
	state.ready = VK_NULL_HANDLE;
	state.done = VK_NULL_HANDLE;
	state.ready_signalled = false;
	state.ready_uv = nullptr;

	// CUDA first: the mapped array reads memory this is about to free.
	if (state.mipmap && cu.MipmappedArrayDestroy)
		cu.MipmappedArrayDestroy(state.mipmap);
	if (state.external && cu.DestroyExternalMemory)
		cu.DestroyExternalMemory(state.external);
	state.array = nullptr;
	state.mipmap = nullptr;
	state.external = nullptr;

	if (device != VK_NULL_HANDLE) {
		if (state.to_general != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE)
			vkFreeCommandBuffers(device, command_pool, 1, &state.to_general);
		if (state.image != VK_NULL_HANDLE)
			vkDestroyImage(device, state.image, nullptr);
		if (state.memory != VK_NULL_HANDLE)
			vkFreeMemory(device, state.memory, nullptr);
	}
	state.to_general = VK_NULL_HANDLE;
	state.image = VK_NULL_HANDLE;
	state.memory = VK_NULL_HANDLE;
	state.owner = nullptr;

	// A standing signal on another pair that named this chroma image is now for
	// nothing; submitBarriers consumes it before that luma image is reused.
	for (ImageState* other : images)
		if (other->ready_uv == &state)
			other->ready_uv = nullptr;
}

void InteropState::release()
{
	// Everything below was allocated from a device that is still alive when this
	// runs (GpuDevice::DestroyInstance calls Shutdown before it tears down), and
	// the order matters: CUDA's views of the memory, then the memory, then the
	// context.
	if (device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(device);

	{
		ContextGuard guard(cu, context);
		draining = true;
		for (ImageState* state : images)
			releaseImage(*state);
		images.clear();
		draining = false;

		if (stream && cu.StreamDestroy)
			cu.StreamDestroy(stream);
		stream = nullptr;
	}

	if (context && cu.PrimaryCtxRelease)
		cu.PrimaryCtxRelease(cuda_device);
	context = nullptr;

	if (device != VK_NULL_HANDLE) {
		if (command_pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, command_pool, nullptr);
	}
	command_pool = VK_NULL_HANDLE;

	if (cu.library) {
		dlclose(cu.library);
		cu = CudaApi();
	}

	device = VK_NULL_HANDLE;
	physical_device = VK_NULL_HANDLE;
	queue = VK_NULL_HANDLE;
	getMemoryFd = nullptr;
	getSemaphoreFd = nullptr;
	usable = false;
	initialised = false;
}

// The barrier command buffers put both images back into GENERAL, which is the
// layout CUDA requires, and signal the semaphore CUDA waits on: one submit for
// the pair. Everything submitted to this queue earlier is in the barrier's first
// scope, so the drawing that read these images is ordered before it.
bool InteropState::submitBarriers(ImageState& y, ImageState& uv)
{
	if (!y.valid() || !uv.valid()) {
		error = "the images' device has gone away";
		return false;
	}
	if (y.ready == VK_NULL_HANDLE) {
		error = "the luma image carries no semaphores (it must be the R8 one)";
		return false;
	}
	if (y.ready_signalled && y.ready_uv == &uv)
		return true;   // already up for this pair
	if (y.ready_signalled) {
		// A signal is standing for a different chroma image -- one since freed, or
		// a caller mixing pairs. Consume it, and wait until the consume has actually
		// happened: a second signal submitted while the first is still pending is
		// exactly the double signal these per-pair semaphores exist to rule out.
		// Nothing the reader does takes this path.
		ContextGuard guard(cu, context);
		CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wait{};
		CUresult result = cu.WaitExternalSemaphores(&y.ready_cu, &wait, 1, stream);
		if (result == CUDA_SUCCESS)
			result = cu.StreamSynchronize(stream);
		y.ready_signalled = false;
		if (result != CUDA_SUCCESS) {
			error = cu.message("consuming a standing barrier signal", result);
			return false;
		}
	}

	const VkCommandBuffer buffers[2] = {y.to_general, uv.to_general};
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 2;
	submit.pCommandBuffers = buffers;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &y.ready;

	GpuDevice::QueueGuard guard;   // one queue, externally synchronised
	if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
		error = "vkQueueSubmit failed for the layout barrier";
		return false;
	}
	y.ready_signalled = true;
	y.ready_uv = &uv;
	return true;
}

bool InteropState::copyNV12(const AVFrame* frame, ImageState& y, ImageState& uv,
							CUstream on_stream)
{
	// Everything that can fail is checked before the Vulkan submit below, because
	// that submit signals a binary semaphore and there is no way to un-signal one.
	if (!frame || frame->format != AV_PIX_FMT_CUDA || !frame->data[0] || !frame->data[1]) {
		error = "copyNV12 wants an AV_PIX_FMT_CUDA frame with two planes";
		return false;
	}
	if (!y.valid() || !uv.valid()) {
		error = "copyNV12 was given an image whose device has gone away";
		return false;
	}
	if (y.format != GpuImage::Format::R8 || uv.format != GpuImage::Format::R8G8) {
		error = "copyNV12 wants an R8 luma image and an R8G8 chroma image";
		return false;
	}
	if (y.width != frame->width || y.height != frame->height ||
		uv.width != (frame->width + 1) / 2 || uv.height != (frame->height + 1) / 2) {
		error = "copyNV12's images are not this frame's size";
		return false;
	}

	// Usually already done, by the prepareForCopy() that followed the drawing
	// which last read these images — the whole point of that call is that this
	// handshake is not on the critical path. This is the first frame, or a caller
	// that did not bother.
	if (!submitBarriers(y, uv))
		return false;

	ContextGuard guard(cu, context);
	if (!guard.ok()) {
		error = "cuCtxPushCurrent failed";
		return false;
	}

	CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wait{};
	CUresult result = cu.WaitExternalSemaphores(&y.ready_cu, &wait, 1, on_stream);
	y.ready_signalled = false;   // this wait consumes it, queued or not
	if (result != CUDA_SUCCESS) {
		error = cu.message("cuWaitExternalSemaphoresAsync", result);
		return false;
	}

	const ImageState* planes[2] = {&y, &uv};
	for (int i = 0; i < 2; ++i) {
		CUDA_MEMCPY2D copy{};
		copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
		copy.srcDevice = reinterpret_cast<CUdeviceptr>(frame->data[i]);
		copy.srcPitch = static_cast<size_t>(frame->linesize[i]);
		copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
		copy.dstArray = planes[i]->array;
		copy.WidthInBytes =
			static_cast<size_t>(planes[i]->width) * planes[i]->bytesPerPixel();
		copy.Height = static_cast<size_t>(planes[i]->height);
		result = cu.Memcpy2DAsync(&copy, on_stream);
		if (result != CUDA_SUCCESS) {
			error = cu.message("cuMemcpy2DAsync", result);
			return false;
		}
	}

	CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signal{};
	result = cu.SignalExternalSemaphores(&y.done_cu, &signal, 1, on_stream);
	if (result != CUDA_SUCCESS) {
		error = cu.message("cuSignalExternalSemaphoresAsync", result);
		return false;
	}
	return true;
}

}   // namespace cuda_detail
}   // namespace openshot

using openshot::cuda_detail::ImageState;
using openshot::cuda_detail::InteropState;

class openshot::GpuImage::Impl final : public ImageState
{
};

class openshot::CudaInterop::Impl
{
public:
	InteropState state;
};

// --- GpuImage ---------------------------------------------------------------

GpuImage::GpuImage(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

GpuImage::~GpuImage()
{
	InteropState* owner = impl->owner;
	if (!owner)
		return;
	std::lock_guard<std::mutex> lock(owner->mutex);
	if (!impl->owner)
		return;   // released under us by Shutdown while we waited for the lock
	for (auto it = owner->images.begin(); it != owner->images.end(); ++it)
		if (*it == impl.get()) {
			owner->images.erase(it);
			break;
		}
	openshot::cuda_detail::ContextGuard guard(owner->cu, owner->context);
	owner->releaseImage(*impl);
}

int GpuImage::width() const { return impl->width; }
int GpuImage::height() const { return impl->height; }
GpuImage::Format GpuImage::format() const { return impl->format; }
bool GpuImage::valid() const { return impl->valid(); }

sk_sp<SkImage> GpuImage::image() const
{
	InteropState* owner = impl->owner;
	if (!owner)
		return nullptr;
	skgpu::graphite::Recorder* recorder = GpuDevice::Instance().recorder();
	if (!recorder)
		return nullptr;

	skgpu::graphite::VulkanTextureInfo info(
		VK_SAMPLE_COUNT_1_BIT, skgpu::Mipmapped::kNo, 0, impl->vkFormat(),
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SHARING_MODE_EXCLUSIVE, VK_IMAGE_ASPECT_COLOR_BIT, {});

	skgpu::VulkanAlloc alloc;
	alloc.fMemory = impl->memory;
	alloc.fOffset = 0;
	alloc.fSize = impl->memory_size;

	// GENERAL, because copyNV12 puts the image back into it before every write.
	const skgpu::graphite::BackendTexture texture = skgpu::graphite::BackendTextures::MakeVulkan(
		SkISize::Make(impl->width, impl->height), info, VK_IMAGE_LAYOUT_GENERAL,
		owner->queue_family, impl->image, alloc);

	return SkImages::WrapTexture(recorder, texture, impl->colorType(), kOpaque_SkAlphaType,
								 nullptr);
}

// --- CudaInterop ------------------------------------------------------------

namespace
{
	std::unique_ptr<CudaInterop>& interopSlot()
	{
		static std::unique_ptr<CudaInterop> slot;
		return slot;
	}
	std::mutex& interopSlotMutex()
	{
		static std::mutex m;
		return m;
	}
}

CudaInterop::CudaInterop() : impl(new Impl) {}

CudaInterop::~CudaInterop() = default;

CudaInterop& CudaInterop::Instance()
{
	std::lock_guard<std::mutex> lock(interopSlotMutex());
	std::unique_ptr<CudaInterop>& slot = interopSlot();
	if (!slot)
		slot.reset(new CudaInterop());
	return *slot;
}

void CudaInterop::Shutdown()
{
	CudaInterop* interop = nullptr;
	{
		std::lock_guard<std::mutex> lock(interopSlotMutex());
		interop = interopSlot().get();
	}
	if (!interop)
		return;   // never used; nothing to release, and nothing to create either
	std::lock_guard<std::mutex> lock(interop->impl->state.mutex);
	interop->impl->state.release();
}

bool CudaInterop::available()
{
	std::lock_guard<std::mutex> lock(impl->state.mutex);
	return impl->state.initialise();
}

std::string CudaInterop::lastError() const { return impl->state.error; }

std::string CudaInterop::deviceName() const { return impl->state.device_name; }

void* CudaInterop::cudaContext()
{
	return available() ? impl->state.context : nullptr;
}

void* CudaInterop::cudaStream()
{
	return available() ? impl->state.stream : nullptr;
}

unsigned long long CudaInterop::waitSemaphore(const GpuImage& y) const
{
	unsigned long long handle = 0;
	std::memcpy(&handle, &y.impl->done, sizeof(y.impl->done));
	return handle;
}

std::shared_ptr<GpuImage> CudaInterop::createImage(int width, int height,
												   GpuImage::Format format)
{
	if (width <= 0 || height <= 0)
		return nullptr;
	std::lock_guard<std::mutex> lock(impl->state.mutex);
	if (!impl->state.initialise())
		return nullptr;

	std::unique_ptr<GpuImage::Impl> state(new GpuImage::Impl);
	state->owner = &impl->state;
	openshot::cuda_detail::ContextGuard guard(impl->state.cu, impl->state.context);
	if (!guard.ok() || !impl->state.createImage(*state, width, height, format)) {
		impl->state.releaseImage(*state);
		return nullptr;
	}
	impl->state.images.push_back(state.get());
	return std::shared_ptr<GpuImage>(new GpuImage(std::move(state)));
}

bool CudaInterop::copyNV12(const AVFrame* cuda_frame, GpuImage& y, GpuImage& uv, void* stream)
{
	std::lock_guard<std::mutex> lock(impl->state.mutex);
	if (!impl->state.initialise())
		return false;
	return impl->state.copyNV12(cuda_frame, *y.impl, *uv.impl,
								stream ? static_cast<CUstream>(stream) : impl->state.stream);
}

bool CudaInterop::prepareForCopy(GpuImage& y, GpuImage& uv)
{
	std::lock_guard<std::mutex> lock(impl->state.mutex);
	if (!impl->state.initialise())
		return false;
	return impl->state.submitBarriers(*y.impl, *uv.impl);
}

#else   // !OPENSHOT_CUDA_INTEROP

// No GPU Skia, or no CUDA headers at build time: the interop exists and is never
// available, exactly as GpuDevice does in a CPU-Skia build.

class openshot::GpuImage::Impl
{
};

class openshot::CudaInterop::Impl
{
public:
	std::string error =
#ifndef OPENSHOT_HAVE_SKIA_GPU
		"libopenshot was built against a Skia with no GPU backend "
		"(configure with -DSkia_ROOT=/usr/local/skia-gpu)";
#else
		"libopenshot was built without the CUDA driver headers";
#endif
};

GpuImage::GpuImage(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
GpuImage::~GpuImage() = default;
int GpuImage::width() const { return 0; }
int GpuImage::height() const { return 0; }
GpuImage::Format GpuImage::format() const { return Format::R8; }
bool GpuImage::valid() const { return false; }
sk_sp<SkImage> GpuImage::image() const { return nullptr; }

CudaInterop::CudaInterop() : impl(new Impl) {}
CudaInterop::~CudaInterop() = default;

CudaInterop& CudaInterop::Instance()
{
	static CudaInterop interop;
	return interop;
}

void CudaInterop::Shutdown() {}
bool CudaInterop::available() { return false; }
std::string CudaInterop::lastError() const { return impl->error; }
std::string CudaInterop::deviceName() const { return std::string(); }
void* CudaInterop::cudaContext() { return nullptr; }
void* CudaInterop::cudaStream() { return nullptr; }
unsigned long long CudaInterop::waitSemaphore(const GpuImage&) const { return 0; }

std::shared_ptr<GpuImage> CudaInterop::createImage(int, int, GpuImage::Format)
{
	return nullptr;
}

bool CudaInterop::copyNV12(const AVFrame*, GpuImage&, GpuImage&, void*)
{
	return false;
}

bool CudaInterop::prepareForCopy(GpuImage&, GpuImage&)
{
	return false;
}

#endif  // OPENSHOT_CUDA_INTEROP
