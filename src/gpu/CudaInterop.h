#pragma once

// CUDA -> Vulkan interop: images that NVDEC's output can be copied into on the
// device, and that Skia can then sample, with no trip through host memory.
//
// Read this before changing anything here, because the shape of the code is
// forced by two facts measured on 2026-09-22 (doc/gpu-migration/STATUS.md):
//
//  1. Skia enables no external-memory extensions off Android. GpuDevice adds
//     VK_KHR_external_memory_fd and VK_KHR_external_semaphore_fd itself, and
//     reports them through GpuDevice::VulkanHandles.
//  2. Graphite cannot export its own allocations. The only door into Graphite is
//     BackendTextures::MakeVulkan(..., VkImage, VulkanAlloc), which takes an
//     image the *caller* owns — so every image CUDA writes is allocated here,
//     exportable, with our own vkAllocateMemory and no VMA. GpuSurfacePool's
//     surfaces can never be the imported ones; this allocation path sits beside
//     the pool rather than inside it.
//
// Everything still goes through GpuDevice::Instance().available(): with the GPU
// off, available() below is false. It is false on lavapipe too, which has no
// external_semaphore_fd, and on a machine with no NVIDIA driver — all three are
// normal answers, and every caller falls back to the host path.

#include <memory>
#include <string>

#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkRefCnt.h"

struct AVFrame;

namespace openshot
{
	/**
	 * @brief One single-plane image that CUDA writes and Skia samples.
	 *
	 * The image is a VkImage this library allocated from exportable, dedicated
	 * device memory, imported into CUDA as a mipmapped array. It is kept in
	 * @c VK_IMAGE_LAYOUT_GENERAL between uses, which is the layout CUDA requires;
	 * CudaInterop::copyNV12 restores that layout before each write, so a caller
	 * never has to think about it.
	 *
	 * Created only by CudaInterop::createImage, and dead once the device goes
	 * away: a GpuImage whose valid() is false has already released everything and
	 * hands back a null image().
	 */
	class GpuImage
	{
	public:
		/// The plane layouts NV12 needs: one byte of luma, two of chroma.
		enum class Format
		{
			R8,    ///< VK_FORMAT_R8_UNORM, sampled as (r, 0, 0, 1)
			R8G8,  ///< VK_FORMAT_R8G8_UNORM, sampled as (r, g, 0, 1)
		};

		~GpuImage();

		int width() const;
		int height() const;
		Format format() const;

		/// Is this image still backed by a live device?
		bool valid() const;

		/// A Graphite-backed SkImage over these pixels on this thread's recorder,
		/// or null when the image is dead or the GPU is gone.
		///
		/// A *fresh* wrapper every call, on purpose. Skia transitions the image to
		/// SHADER_READ_ONLY_OPTIMAL when it samples it and remembers having done
		/// so, while copyNV12 puts it back into GENERAL for CUDA; a wrapper kept
		/// across frames would barrier from a layout the image is no longer in.
		/// Ask for one per frame, use it, drop it.
		sk_sp<SkImage> image() const;

		GpuImage(const GpuImage&) = delete;
		GpuImage& operator=(const GpuImage&) = delete;

	private:
		friend class CudaInterop;
		class Impl;
		explicit GpuImage(std::unique_ptr<Impl> impl);

		std::unique_ptr<Impl> impl;
	};

	/**
	 * @brief The process-wide CUDA context that shares memory with the Vulkan device.
	 *
	 * available() is false — normally, not as an error — when the GPU is off, when
	 * the Vulkan device is lavapipe (no external_semaphore_fd), when there is no
	 * CUDA driver, or when no CUDA device has the same UUID as the Vulkan one.
	 * lastError() says which.
	 *
	 * The CUDA driver API is loaded with dlopen, so nothing links against libcuda
	 * and a machine with no NVIDIA driver runs unchanged.
	 *
	 * @code
	 * CudaInterop& interop = CudaInterop::Instance();
	 * if (!interop.available()) { ...host path... }
	 * auto y  = interop.createImage(w, h, GpuImage::Format::R8);
	 * auto uv = interop.createImage(w / 2, h / 2, GpuImage::Format::R8G8);
	 * interop.copyNV12(decoded, *y, *uv);
	 * canvas->drawImage(y->image(), 0, 0);       // ...and the shader that samples both
	 * const unsigned long long wait = interop.waitSemaphore(*y);
	 * GpuDevice::Instance().submit(false, &wait, 1);
	 * @endcode
	 */
	class CudaInterop
	{
	public:
		/// The process-wide instance. Never null; may be unavailable.
		static CudaInterop& Instance();

		/// Release every image, semaphore and CUDA object. Called by
		/// GpuDevice::DestroyInstance() while the Vulkan device is still alive,
		/// because all of it was allocated from that device. Images handed out
		/// stay alive as objects but go invalid.
		static void Shutdown();

		/// Is there a CUDA context sharing memory with the Vulkan device? Sets it
		/// up on the first call. False is a normal answer; see lastError().
		bool available();

		/// Why the interop is unavailable, empty when it is available
		std::string lastError() const;

		/// The CUDA device's name, empty when unavailable
		std::string deviceName() const;

		/// The @c CUcontext the images were imported into — the primary context of
		/// the CUDA device that matches the Vulkan one. Null when unavailable.
		/// This is the context a decoder must use for its frames to be copyable
		/// (W23 hands it to FFmpeg's @c AVCUDADeviceContext).
		void* cudaContext();

		/// The @c CUstream copies run on by default. Null when unavailable.
		void* cudaStream();

		/// The @c VkSemaphore that copyNV12 into @a y signals, as an integer handle —
		/// pass it to GpuDevice::submit() so the drawing that samples the images
		/// waits for the copy. Binary, and one per image pair (it lives on the luma
		/// image): exactly one submit must wait on each copyNV12 call into that pair,
		/// and it must be the next submit to name it. Callers on several threads
		/// must serialise copy -> submit themselves.
		unsigned long long waitSemaphore(const GpuImage& y) const;

		/// An exportable image of this size, or null when unavailable.
		std::shared_ptr<GpuImage> createImage(int width, int height, GpuImage::Format format);

		/// Copy the two planes of an @c AV_PIX_FMT_CUDA frame into @a y and @a uv,
		/// device to device, on @a stream (the interop's own when null).
		///
		/// Asynchronous: it returns once the copies are queued. Ordering is done
		/// with semaphores rather than by blocking — the images are barriered back
		/// into GENERAL on the Vulkan queue, CUDA waits for that, copies, and
		/// signals waitSemaphore().
		bool copyNV12(const AVFrame* cuda_frame, GpuImage& y, GpuImage& uv,
					  void* stream = nullptr);

		/// Make @a y and @a uv writable by CUDA again — the layout barrier and the
		/// semaphore signal the next copyNV12 would otherwise have to do first.
		///
		/// Call it immediately after submitting the drawing that sampled them.
		/// The work is ordered after everything already on the queue, so by the
		/// time the next frame is decoded the semaphore is up and the copy starts
		/// at once. Skipping it is correct but costs the cross-API handshake on
		/// the critical path — 0.40 ms against 0.21 ms a 4K frame, measured.
		bool prepareForCopy(GpuImage& y, GpuImage& uv);

		CudaInterop(const CudaInterop&) = delete;
		CudaInterop& operator=(const CudaInterop&) = delete;

		/// Public only so the singleton can be held in a unique_ptr.
		~CudaInterop();

	private:
		CudaInterop();

		class Impl;
		std::unique_ptr<Impl> impl;
	};
}
