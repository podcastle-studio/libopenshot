#pragma once

// The process-wide Vulkan device and Skia Graphite context, and the ONE switch
// that turns GPU use on and off.
//
// Everything GPU in this fork goes through GpuDevice::Instance().available(),
// which is false unless a backend is asked for — a machine with no GPU is a
// supported configuration, not an error. Keep it that way: a new GPU code path
// asks available() (or GpuOffscreen::Match / GpuFrame::Create, which ask it for
// you) rather than reading the environment or adding a flag of its own, so
// SetBackend(Backend::Off) is guaranteed to disable all of it at once.

#include <memory>
#include <string>

// Forward declarations only. This header is installed with the rest of src/*.h
// and must compile for a consumer who has no Skia GPU headers, so it never names
// a Graphite type by value and the class is pimpl'd: the layout is identical
// whether or not libopenshot was built against a Graphite-capable Skia.
namespace skgpu { namespace graphite { class Context; class Recorder; } }

namespace openshot
{
	/**
	 * @brief The process-wide Vulkan device and Skia Graphite context.
	 *
	 * One device, one queue and one Graphite @c Context per process — Graphite is
	 * not designed to be instantiated per thread, and gets its parallelism from
	 * pipeline depth rather than width. Each thread that records drawing gets its
	 * own @c Recorder from recorder(), which is the unit Graphite *does* expect to
	 * be per-thread.
	 *
	 * The device is created lazily on the first call to available(). Which
	 * backend it asks for comes from SetBackend() when that has been called, and
	 * otherwise from the @c OPENSHOT_GPU environment variable:
	 *
	 * - @c off (default) — never touch Vulkan; available() is false.
	 * - @c vulkan — real hardware; the adapter index comes from
	 *   @c Settings::HW_EN_DEVICE_SET, and a discrete GPU is preferred.
	 * - @c lavapipe — force Mesa's software rasteriser, for machines with no GPU
	 *   and for checking that a result does not depend on the vendor.
	 *
	 * Every caller must treat available() == false as normal and fall back to the
	 * raster path; a machine with no GPU, no driver or an old loader is a supported
	 * configuration, not an error. lastError() explains why it is unavailable.
	 *
	 * @code
	 * // Turn every GPU path in the library off / on from the host application.
	 * openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
	 * openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Vulkan);
	 * @endcode
	 */
	class GpuDevice
	{
	public:
		/// Which backend the device was asked for
		enum class Backend
		{
			Off,      ///< disabled (the default)
			Vulkan,   ///< hardware Vulkan
			Lavapipe, ///< Mesa lavapipe software rasteriser
		};

		/// Select the backend explicitly, overriding @c OPENSHOT_GPU. This is the
		/// single switch for GPU use: because every GPU path asks available(),
		/// Backend::Off disables all of them and Vulkan / Lavapipe enable them.
		///
		/// Takes effect at once. If a device is already up for a different
		/// backend it is torn down first, exactly as DestroyInstance() does — so
		/// anything holding a GPU object must already be keyed on Generation(),
		/// which this moves. Not thread-safe against concurrent rendering: call
		/// it during setup, or with no frame in flight.
		static void SetBackend(Backend backend);

		/// The backend the next device creation will use — the SetBackend()
		/// override when one has been set, otherwise @c OPENSHOT_GPU. Unlike
		/// backend(), this answers without creating the device.
		static Backend RequestedBackend();

		/// Convert to and from the @c OPENSHOT_GPU spelling ("off", "vulkan",
		/// "lavapipe"). Anything unrecognised is Backend::Off.
		static Backend BackendFromName(const std::string& name);
		static const char* BackendName(Backend backend);

		/// The process-wide instance. Never null; may be unavailable.
		static GpuDevice& Instance();

		/// Tear the device down. Only for tests that cycle it; not thread-safe.
		static void DestroyInstance();

		/// Bumped every time the device is destroyed. Anything that caches GPU
		/// objects across calls must record this and throw its cache away when it
		/// changes — the objects belong to a context that no longer exists.
		static unsigned long long Generation();

		/// Is there a usable Vulkan device and Graphite context? Creates it on
		/// the first call. False on any failure, with lastError() set.
		bool available();

		/// Which backend was requested (from OPENSHOT_GPU), regardless of success
		Backend backend() const;

		/// Human-readable adapter name, empty when unavailable
		std::string deviceName() const;

		/// Why the device is unavailable, empty when it is available
		std::string lastError() const;

		/// The process-wide Graphite context, or null when unavailable
		skgpu::graphite::Context* context();

		/// This thread's Graphite recorder, or null when unavailable. Owned by
		/// the device; do not delete. Created on first use per thread.
		skgpu::graphite::Recorder* recorder();

		/// Hand this thread's recorded work to the GPU. When @a syncToCpu, block
		/// until it has finished. Returns false when unavailable or on failure.
		bool submit(bool syncToCpu);

		/// As submit(), with the GPU first waiting on @a wait_semaphores — raw
		/// @c VkSemaphore handles, passed as integers so this installed header
		/// needs no Vulkan headers of its own. Each wait consumes one signal, the
		/// way a Vulkan binary semaphore does, so a semaphore handed here must
		/// have been signalled exactly once: this is how CudaInterop's copy is
		/// ordered before the drawing that samples it, with no CPU sync.
		bool submit(bool syncToCpu, const unsigned long long* wait_semaphores,
					unsigned int wait_count);

		/// The raw Vulkan handles behind the Graphite context, or null when the
		/// device is unavailable or this build has no GPU Skia.
		///
		/// For interop code *inside* the library (src/gpu/CudaInterop) that has to
		/// allocate its own images: Graphite cannot export its own allocations, so
		/// anything CUDA imports must be allocated against this device. The
		/// handles are @c void* because every installed header must compile for a
		/// consumer with no Vulkan headers; they are the dispatchable handles
		/// @c VkInstance, @c VkPhysicalDevice, @c VkDevice and @c VkQueue.
		struct VulkanHandles
		{
			void* instance = nullptr;
			void* physical_device = nullptr;
			void* device = nullptr;
			void* queue = nullptr;
			unsigned int queue_family = 0;
			unsigned int api_version = 0;
			/// VK_KHR_external_memory_fd was offered and is enabled
			bool external_memory_fd = false;
			/// VK_KHR_external_semaphore_fd was offered and is enabled. lavapipe
			/// has no such thing, which is why the interop declines there.
			bool external_semaphore_fd = false;
		};
		const VulkanHandles* vulkanHandles();

		/// Holds the process-wide queue for its lifetime.
		///
		/// There is one @c VkQueue and submitting to it needs external
		/// synchronisation; submit() takes this lock internally. Raw Vulkan work
		/// on the same queue — an image-layout barrier, say — must hold it too.
		class QueueGuard
		{
		public:
			QueueGuard();
			~QueueGuard();
			QueueGuard(const QueueGuard&) = delete;
			QueueGuard& operator=(const QueueGuard&) = delete;
		};

		GpuDevice(const GpuDevice&) = delete;
		GpuDevice& operator=(const GpuDevice&) = delete;

		/// Public only so the singleton can be held in a unique_ptr; the private
		/// constructor is what stops anyone making their own device.
		~GpuDevice();

	private:
		GpuDevice();

		void lockQueue();
		void unlockQueue();

		class Impl;
		std::unique_ptr<Impl> impl;
	};
}

