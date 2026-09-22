// The process-wide Vulkan device and Skia Graphite context. See GpuDevice.h.
//
// The Vulkan bootstrap here (instance, physical device, queue, extensions and
// the feature chain Skia asks for through VulkanPreferredFeatures) is the same
// sequence proved out in tests/gpu/skia_gpu_smoke.cpp for plan step 2.1.

#include "GpuDevice.h"

#include "CudaInterop.h"
#include "GpuSurfacePool.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef OPENSHOT_HAVE_SKIA_GPU
#include "skia/include/gpu/graphite/Context.h"
#include "skia/include/gpu/graphite/ContextOptions.h"
#include "skia/include/gpu/graphite/GraphiteTypes.h"
#include "skia/include/gpu/graphite/Recorder.h"
#include "skia/include/gpu/graphite/BackendSemaphore.h"
#include "skia/include/gpu/graphite/Recording.h"
#include "skia/include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "skia/include/gpu/graphite/vk/VulkanGraphiteTypes.h"
#include "skia/include/gpu/vk/VulkanBackendContext.h"
#include "skia/include/gpu/vk/VulkanExtensions.h"
#include "skia/include/gpu/vk/VulkanPreferredFeatures.h"
// Private, but the symbols are in libskia.a and install_skia_gpu.sh ships these
// two self-contained headers. Graphite makes the caller supply the memory
// allocator and Skia's VMA-backed one has no public factory. See
// doc/gpu-migration/GPU-DECISIONS.md, 2026-09-14.
#include "skia/src/gpu/GpuTypesPriv.h"
#include "skia/src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#endif

#include "../Settings.h"

using namespace openshot;

namespace
{
	GpuDevice::Backend backendFromEnvironment()
	{
		const char* requested = std::getenv("OPENSHOT_GPU");
		if (!requested)
			return GpuDevice::Backend::Off;
		return GpuDevice::BackendFromName(requested);
	}

	// The SetBackend() override, when one has been set. Kept apart from the
	// device itself so it survives DestroyInstance() — tearing the device down
	// must not silently revert the caller's choice to whatever the environment
	// happens to say.
	std::mutex& backendOverrideMutex()
	{
		static std::mutex m;
		return m;
	}
	bool g_backend_override_set = false;
	GpuDevice::Backend g_backend_override = GpuDevice::Backend::Off;
}

GpuDevice::Backend GpuDevice::BackendFromName(const std::string& name)
{
	if (name == "vulkan")
		return Backend::Vulkan;
	if (name == "lavapipe")
		return Backend::Lavapipe;
	return Backend::Off;   // "off", empty, and anything unrecognised
}

const char* GpuDevice::BackendName(Backend backend)
{
	switch (backend) {
	case Backend::Vulkan:
		return "vulkan";
	case Backend::Lavapipe:
		return "lavapipe";
	case Backend::Off:
		break;
	}
	return "off";
}

GpuDevice::Backend GpuDevice::RequestedBackend()
{
	{
		std::lock_guard<std::mutex> lock(backendOverrideMutex());
		if (g_backend_override_set)
			return g_backend_override;
	}
	return backendFromEnvironment();
}

void GpuDevice::SetBackend(Backend backend)
{
	{
		std::lock_guard<std::mutex> lock(backendOverrideMutex());
		if (g_backend_override_set && g_backend_override == backend)
			return;                     // already the standing choice
		g_backend_override_set = true;
		g_backend_override = backend;
	}
	// A device that is already up was built for the old choice, so drop it. This
	// moves Generation(), which is what tells every cache holding a GPU object to
	// throw it away; the next available() rebuilds for the new backend (or, for
	// Off, refuses without touching Vulkan at all).
	DestroyInstance();
}

#ifdef OPENSHOT_HAVE_SKIA_GPU

namespace
{
	PFN_vkVoidFunction gpuGetProc(const char* name, VkInstance instance, VkDevice device)
	{
		if (device != VK_NULL_HANDLE)
			return vkGetDeviceProcAddr(device, name);
		return vkGetInstanceProcAddr(instance, name);
	}
}

/// Everything Vulkan and Graphite, so none of it reaches the installed header.
class GpuDevice::Impl
{
public:
	GpuDevice::Backend backend = GpuDevice::Backend::Off;
	bool initialised = false;
	bool usable = false;
	std::string device_name;
	std::string error;

	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_index = 0;
	uint32_t api_version = 0;

	// Skia keeps pointers into these for as long as the context lives
	std::vector<const char*> instance_extensions;
	std::vector<const char*> device_extensions;

	// Handed to interop code inside the library; see GpuDevice::vulkanHandles().
	GpuDevice::VulkanHandles handles;
	VkPhysicalDeviceFeatures2 features{};
	skgpu::VulkanPreferredFeatures skia_features;
	skgpu::VulkanExtensions extensions;
	sk_sp<skgpu::VulkanMemoryAllocator> allocator;

	std::unique_ptr<skgpu::graphite::Context> context;
	std::mutex context_mutex;

	// The device owns every thread's recorder rather than letting each thread
	// hold its own in a thread_local. A Recorder must not outlive the Context it
	// came from, and a thread_local one does exactly that: it survives the device
	// teardown and is destroyed later, against a freed context.
	std::mutex recorder_mutex;
	std::map<std::thread::id, std::unique_ptr<skgpu::graphite::Recorder>> recorders;

	~Impl() { teardown(); }

	bool initialise()
	{
		if (initialised)
			return usable;
		initialised = true;

		backend = GpuDevice::RequestedBackend();
		if (backend == GpuDevice::Backend::Off) {
			error = "GPU is off (OPENSHOT_GPU / GpuDevice::SetBackend)";
			return false;
		}
		// Ask the loader for the software rasteriser only. Setting this here rather
		// than making the caller export it keeps "lavapipe" a one-word switch, and
		// it is read by the loader when the instance is created below.
		if (backend == GpuDevice::Backend::Lavapipe)
			setenv("VK_DRIVER_FILES", "/usr/share/vulkan/icd.d/lvp_icd.json", 1);

		if (!createVulkanDevice())
			return false;
		if (!createGraphiteContext())
			return false;

		usable = true;
		return true;
	}

	void teardown()
	{
		{
			// Before the context: see the comment on `recorders`.
			std::lock_guard<std::mutex> lock(recorder_mutex);
			recorders.clear();
		}
		context.reset();
		allocator.reset();
		if (device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(device);
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}
		if (instance != VK_NULL_HANDLE) {
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
		usable = false;
	}

private:
	bool createVulkanDevice()
	{
		uint32_t loader_version = VK_API_VERSION_1_0;
		if (vkEnumerateInstanceVersion(&loader_version) != VK_SUCCESS) {
			error = "vkEnumerateInstanceVersion failed (no Vulkan loader)";
			return false;
		}
		// Skia's floor is 1.1 and VulkanPreferredFeatures' ceiling is 1.4. Ask for
		// 1.3, which both the NVIDIA driver and lavapipe provide.
		api_version = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3
														   : VK_API_VERSION_1_1;
		skia_features.init(api_version);

		uint32_t instance_extension_count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
		std::vector<VkExtensionProperties> available(instance_extension_count);
		vkEnumerateInstanceExtensionProperties(
			nullptr, &instance_extension_count, available.data());
		skia_features.addToInstanceExtensions(
			available.data(), available.size(), instance_extensions);

		VkApplicationInfo application{};
		application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		application.pApplicationName = "libopenshot";
		application.apiVersion = api_version;

		VkInstanceCreateInfo instance_info{};
		instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_info.pApplicationInfo = &application;
		instance_info.enabledExtensionCount =
			static_cast<uint32_t>(instance_extensions.size());
		instance_info.ppEnabledExtensionNames = instance_extensions.data();
		if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
			error = "vkCreateInstance failed (no Vulkan driver for this backend)";
			return false;
		}

		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
		if (device_count == 0) {
			error = "no Vulkan physical devices";
			return false;
		}
		std::vector<VkPhysicalDevice> devices(device_count);
		vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

		// Same knob FFmpegWriter uses to pick an encode adapter, so one machine
		// with two GPUs sends encode and render to the same card by default.
		const int requested = Settings::Instance()->HW_EN_DEVICE_SET;
		int chosen = -1;
		VkPhysicalDeviceProperties properties{};
		if (requested > 0 && requested < static_cast<int>(device_count)) {
			vkGetPhysicalDeviceProperties(devices[requested], &properties);
			if (properties.apiVersion >= api_version)
				chosen = requested;
		}
		for (uint32_t i = 0; i < device_count && chosen < 0; ++i) {
			vkGetPhysicalDeviceProperties(devices[i], &properties);
			if (properties.apiVersion >= api_version &&
				properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				chosen = static_cast<int>(i);
		}
		for (uint32_t i = 0; i < device_count && chosen < 0; ++i) {
			vkGetPhysicalDeviceProperties(devices[i], &properties);
			if (properties.apiVersion >= api_version)
				chosen = static_cast<int>(i);
		}
		if (chosen < 0) {
			error = "no physical device supports the required Vulkan version";
			return false;
		}
		physical_device = devices[chosen];
		vkGetPhysicalDeviceProperties(physical_device, &properties);
		device_name = properties.deviceName;

		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
		std::vector<VkQueueFamilyProperties> families(family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(
			physical_device, &family_count, families.data());
		bool found_queue = false;
		for (uint32_t i = 0; i < family_count; ++i) {
			if (families[i].queueCount > 0 &&
				(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				queue_index = i;
				found_queue = true;
				break;
			}
		}
		if (!found_queue) {
			error = "no graphics queue family";
			return false;
		}

		uint32_t device_extension_count = 0;
		vkEnumerateDeviceExtensionProperties(
			physical_device, nullptr, &device_extension_count, nullptr);
		std::vector<VkExtensionProperties> device_available(device_extension_count);
		vkEnumerateDeviceExtensionProperties(
			physical_device, nullptr, &device_extension_count, device_available.data());

		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		skia_features.addFeaturesToQuery(
			device_available.data(), device_available.size(), features);
		vkGetPhysicalDeviceFeatures2(physical_device, &features);
		skia_features.addFeaturesToEnable(device_extensions, features);

		// Skia names the external-memory extensions only under SK_BUILD_FOR_ANDROID,
		// so VulkanPreferredFeatures has just asked for neither. CudaInterop needs
		// both to hand an image to CUDA without a trip through host memory, and the
		// only place they can be enabled is here, at device creation. Their absence
		// is recorded rather than treated as an error: lavapipe has no
		// external_semaphore_fd, and the interop declining is the normal answer.
		const auto enableIfOffered = [&](const char* name) {
			bool offered = false;
			for (const VkExtensionProperties& e : device_available)
				if (std::strcmp(e.extensionName, name) == 0) {
					offered = true;
					break;
				}
			if (!offered)
				return false;
			for (const char* already : device_extensions)
				if (std::strcmp(already, name) == 0)
					return true;
			device_extensions.push_back(name);   // a string literal: outlives the device
			return true;
		};
		handles.external_memory_fd =
			enableIfOffered(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
		handles.external_semaphore_fd =
			enableIfOffered(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);

		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info{};
		queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info.queueFamilyIndex = queue_index;
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &priority;

		VkDeviceCreateInfo device_info{};
		device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_info.pNext = &features;
		device_info.queueCreateInfoCount = 1;
		device_info.pQueueCreateInfos = &queue_info;
		device_info.enabledExtensionCount =
			static_cast<uint32_t>(device_extensions.size());
		device_info.ppEnabledExtensionNames = device_extensions.data();
		if (vkCreateDevice(physical_device, &device_info, nullptr, &device) != VK_SUCCESS) {
			error = "vkCreateDevice failed";
			return false;
		}
		vkGetDeviceQueue(device, queue_index, 0, &queue);

		handles.instance = instance;
		handles.physical_device = physical_device;
		handles.device = device;
		handles.queue = queue;
		handles.queue_family = queue_index;
		handles.api_version = api_version;

		extensions.init(gpuGetProc, instance, physical_device,
						static_cast<uint32_t>(instance_extensions.size()),
						instance_extensions.data(),
						static_cast<uint32_t>(device_extensions.size()),
						device_extensions.data());
		return true;
	}

	bool createGraphiteContext()
	{
		skgpu::VulkanBackendContext backend_context{};
		backend_context.fInstance = instance;
		backend_context.fPhysicalDevice = physical_device;
		backend_context.fDevice = device;
		backend_context.fQueue = queue;
		backend_context.fGraphicsQueueIndex = queue_index;
		backend_context.fMaxAPIVersion = api_version;
		backend_context.fVkExtensions = &extensions;
		backend_context.fDeviceFeatures2 = &features;
		backend_context.fGetProc = gpuGetProc;

		// Thread-safe: one allocator serves every thread's recorder.
		allocator = skgpu::VulkanMemoryAllocators::Make(
			backend_context, skgpu::ThreadSafe::kYes);
		if (!allocator) {
			error = "could not create a Vulkan memory allocator";
			return false;
		}
		backend_context.fMemoryAllocator = allocator;

		skgpu::graphite::ContextOptions options;
		context = skgpu::graphite::ContextFactory::MakeVulkan(backend_context, options);
		if (!context) {
			error = "graphite::ContextFactory::MakeVulkan failed";
			return false;
		}
		return true;
	}
};

#else  // !OPENSHOT_HAVE_SKIA_GPU

/// Built against the CPU Skia: the device exists but is never available. The
/// pimpl keeps GpuDevice's layout identical to the GPU build, so an installed
/// header is valid either way.
class GpuDevice::Impl
{
public:
	GpuDevice::Backend backend = GpuDevice::Backend::Off;
	bool initialised = false;
	bool usable = false;
	std::string device_name;
	std::string error;

	bool initialise()
	{
		if (!initialised) {
			initialised = true;
			backend = GpuDevice::RequestedBackend();
			error = "libopenshot was built against a Skia with no GPU backend "
					"(configure with -DSkia_ROOT=/usr/local/skia-gpu)";
		}
		return false;
	}

	void teardown() {}
};

#endif  // OPENSHOT_HAVE_SKIA_GPU

namespace
{
	std::unique_ptr<GpuDevice>& deviceSlot()
	{
		static std::unique_ptr<GpuDevice> slot;
		return slot;
	}
	std::mutex& deviceSlotMutex()
	{
		static std::mutex m;
		return m;
	}
	std::atomic<unsigned long long>& deviceGeneration()
	{
		static std::atomic<unsigned long long> generation{1};
		return generation;
	}
}

GpuDevice::GpuDevice() : impl(new Impl) {}

GpuDevice::~GpuDevice() = default;

GpuDevice& GpuDevice::Instance()
{
	std::lock_guard<std::mutex> lock(deviceSlotMutex());
	auto& slot = deviceSlot();
	if (!slot)
		slot.reset(new GpuDevice());
	return *slot;
}

void GpuDevice::DestroyInstance()
{
	// Pooled surfaces belong to the Graphite context, so they have to be released
	// while it is still alive — freeing them afterwards writes through a dangling
	// context. This must happen before the slot is reset, not after.
	GpuSurfacePool::DiscardAllPools();
	// Same for the interop's images, semaphores and command pool: they are
	// allocated against this VkDevice and have to go before it does.
	CudaInterop::Shutdown();

	std::lock_guard<std::mutex> lock(deviceSlotMutex());
	deviceSlot().reset();
	// After this every surface, image and recorder handed out by the old context
	// is dangling. Caches key on this and drop themselves when it moves.
	deviceGeneration()++;
}

unsigned long long GpuDevice::Generation()
{
	return deviceGeneration().load();
}

bool GpuDevice::available()
{
	return impl->initialise();
}

GpuDevice::Backend GpuDevice::backend() const
{
	return impl->backend;
}

std::string GpuDevice::deviceName() const
{
	return impl->device_name;
}

std::string GpuDevice::lastError() const
{
	return impl->error;
}

#ifdef OPENSHOT_HAVE_SKIA_GPU

skgpu::graphite::Context* GpuDevice::context()
{
	if (!available())
		return nullptr;
	return impl->context.get();
}

skgpu::graphite::Recorder* GpuDevice::recorder()
{
	if (!available())
		return nullptr;

	// One recorder per thread, which is the unit Graphite expects to be
	// per-thread — but owned by the device, not by the thread, so that a device
	// teardown destroys it while its context is still alive. The thread_local here
	// is only a lookup cache, keyed on the generation so it cannot survive one.
	thread_local unsigned long long cached_generation = 0;
	thread_local skgpu::graphite::Recorder* cached_recorder = nullptr;
	const unsigned long long generation = Generation();
	if (cached_recorder && cached_generation == generation)
		return cached_recorder;

	std::lock_guard<std::mutex> lock(impl->recorder_mutex);
	std::unique_ptr<skgpu::graphite::Recorder>& slot =
		impl->recorders[std::this_thread::get_id()];
	if (!slot)
		slot = impl->context->makeRecorder();
	cached_recorder = slot.get();
	cached_generation = generation;
	return cached_recorder;
}

bool GpuDevice::submit(bool syncToCpu)
{
	return submit(syncToCpu, nullptr, 0);
}

bool GpuDevice::submit(bool syncToCpu, const unsigned long long* wait_semaphores,
					   unsigned int wait_count)
{
	skgpu::graphite::Recorder* rec = recorder();
	if (!rec)
		return false;

	std::unique_ptr<skgpu::graphite::Recording> recording = rec->snap();
	if (!recording)
		return false;

	// The handles arrive as integers because GpuDevice.h is installed and must
	// compile with no Vulkan headers; VkSemaphore is the same eight bytes either
	// way it is defined, so copy rather than cast.
	std::vector<skgpu::graphite::BackendSemaphore> waits;
	waits.reserve(wait_count);
	for (unsigned int i = 0; i < wait_count; ++i) {
		VkSemaphore semaphore = VK_NULL_HANDLE;
		static_assert(sizeof(semaphore) <= sizeof(unsigned long long),
					  "a VkSemaphore does not fit in the handle type");
		std::memcpy(&semaphore, &wait_semaphores[i], sizeof(semaphore));
		waits.push_back(skgpu::graphite::BackendSemaphores::MakeVulkan(semaphore));
	}

	// insertRecording and submit both touch the single Context, which recorders
	// on other threads share.
	std::lock_guard<std::mutex> lock(impl->context_mutex);
	skgpu::graphite::InsertRecordingInfo info;
	info.fRecording = recording.get();
	info.fNumWaitSemaphores = waits.size();
	info.fWaitSemaphores = waits.empty() ? nullptr : waits.data();
	if (impl->context->insertRecording(info) != skgpu::graphite::InsertStatus::kSuccess)
		return false;
	return impl->context->submit(syncToCpu ? skgpu::graphite::SyncToCpu::kYes
										   : skgpu::graphite::SyncToCpu::kNo);
}

const GpuDevice::VulkanHandles* GpuDevice::vulkanHandles()
{
	if (!available())
		return nullptr;
	return &impl->handles;
}

void GpuDevice::lockQueue()
{
	if (available())
		impl->context_mutex.lock();
}

void GpuDevice::unlockQueue()
{
	if (impl->usable)
		impl->context_mutex.unlock();
}

#else

skgpu::graphite::Context* GpuDevice::context()
{
	available();
	return nullptr;
}

skgpu::graphite::Recorder* GpuDevice::recorder()
{
	available();
	return nullptr;
}

bool GpuDevice::submit(bool)
{
	return false;
}

bool GpuDevice::submit(bool, const unsigned long long*, unsigned int)
{
	return false;
}

const GpuDevice::VulkanHandles* GpuDevice::vulkanHandles()
{
	available();
	return nullptr;
}

void GpuDevice::lockQueue() {}

void GpuDevice::unlockQueue() {}

#endif

GpuDevice::QueueGuard::QueueGuard()
{
	GpuDevice::Instance().lockQueue();
}

GpuDevice::QueueGuard::~QueueGuard()
{
	GpuDevice::Instance().unlockQueue();
}
