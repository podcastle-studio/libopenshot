#pragma once

// The process-wide Vulkan device and Skia Graphite context. Everything GPU in
// this fork goes through GpuDevice::Instance().available(), which is false
// unless OPENSHOT_GPU asks for a backend — a machine with no GPU is a supported
// configuration, not an error.

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
	 * The device is created lazily on the first call to available() and is off
	 * unless the @c OPENSHOT_GPU environment variable asks for it:
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

		GpuDevice(const GpuDevice&) = delete;
		GpuDevice& operator=(const GpuDevice&) = delete;

		/// Public only so the singleton can be held in a unique_ptr; the private
		/// constructor is what stops anyone making their own device.
		~GpuDevice();

	private:
		GpuDevice();

		class Impl;
		std::unique_ptr<Impl> impl;
	};
}

