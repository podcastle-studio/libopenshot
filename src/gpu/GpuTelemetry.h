#pragma once

// Per-export GPU telemetry (W31): what the GPU did during one export, and which
// frames took the GPU paths versus a fallback.
//
// Two halves. GpuCounters is a set of process-wide counters the render path bumps
// as it goes -- a readback, a frame encoded straight from the GPU, a crop that had
// to read its frame back -- so a report can say *why* an export was slow, not just
// that it was. ExportTelemetry samples NVML on a thread of its own while an export
// runs and folds both into one Report.
//
// NVML is dlopen'd, like the CUDA driver: nothing links against it, and a machine
// with no NVIDIA driver gets a Report with nvml == false and the counters only.
// NVML's utilisation figures are per *device*, so with several exports sharing a
// GPU each one sees the total -- that is what the numbers mean, and Summary() says so.

#include <atomic>
#include <memory>
#include <string>

namespace openshot
{
	/// Process-wide render-path counters. Cheap enough to bump per frame.
	class GpuCounters
	{
	public:
		enum Counter
		{
			Readback,            ///< a GPU frame read back to host memory (GpuFrame::readback)
			EncodedOnDevice,     ///< a frame NVENC took straight from the GPU (GPU_ENCODE)
			EncodedFromHost,     ///< a frame the writer converted from host RGBA (swscale)
			DecodedOnDevice,     ///< NVDEC -> GPU conversion without leaving the device
			DecodedOnGpu,        ///< host YUV uploaded and converted on the GPU
			DecodedOnCpu,        ///< YUV -> RGBA by swscale
			CropOnGpu,           ///< Crop drew a GPU frame on the GPU (GPU_CROP)
			CropReadback,        ///< Crop read a GPU frame back to run on the CPU
			HardwareDecodeFallback, ///< NVDEC refused a stream; the reader reopened in software
			Upload,              ///< host pixels copied to the GPU (GpuFrame::ToTexture / upload)
			UploadCached,        ///< a host source drawn from a texture kept from an earlier frame
			AllocationFailure,   ///< Skia could not allocate a render target (GpuSurfacePool::acquire): the GPU is full
			SubmitFailure,       ///< a recording could not be inserted or submitted (GpuDevice::submit): draws were lost
			ReadbackFailure,     ///< a GPU frame could not be read back (GpuFrame::readback): its pixels are lost
			Count
		};

		static void Add(Counter counter, unsigned long long n = 1);
		static unsigned long long Get(Counter counter);
		static const char* Name(Counter counter);

		/// Per-export attribution (2026-10-01). The counters above are process-wide, so with
		/// four exports in one process one export running out of memory made all four look
		/// failed. A thread can name a Block that its Add() calls are also counted in;
		/// ExportTelemetry::Start() gives the calling thread one, and the library's own worker
		/// threads (the writer's encode thread, a reader's read-ahead) inherit the block of the
		/// thread that started them. GPU work is recorded per thread, so that is every thread
		/// an export's GPU work can happen on.
		struct Block
		{
			std::atomic<unsigned long long> values[Count];
			Block() { for (auto& v : values) v.store(0, std::memory_order_relaxed); }
		};
		/// This thread's block, or null.
		static std::shared_ptr<Block> ThreadBlock();
		/// Count this thread's Add() calls in @a block as well (null: process-wide only).
		static void SetThreadBlock(std::shared_ptr<Block> block);

		/// Sets this thread's block for the scope and restores the previous one.
		class Inherit
		{
		public:
			explicit Inherit(std::shared_ptr<Block> block) : previous(ThreadBlock())
			{
				SetThreadBlock(std::move(block));
			}
			~Inherit() { SetThreadBlock(std::move(previous)); }
			Inherit(const Inherit&) = delete;
			Inherit& operator=(const Inherit&) = delete;
		private:
			std::shared_ptr<Block> previous;
		};
	};

	/// Samples the GPU across one export.
	class ExportTelemetry
	{
	public:
		struct Report
		{
			bool nvml = false;              ///< false: no NVIDIA driver, counters only
			std::string device;             ///< the NVML device sampled
			double seconds = 0.0;           ///< between Start() and Stop()
			unsigned samples = 0;
			double gpu_busy_mean = 0.0;     ///< percent, device-wide
			double gpu_busy_peak = 0.0;
			double encoder_mean = 0.0;      ///< NVENC, percent, device-wide
			double decoder_mean = 0.0;      ///< NVDEC, percent, device-wide
			double vram_process_peak_mb = 0.0;  ///< this process, from NVML's process list
			double vram_device_peak_mb = 0.0;   ///< the whole device
			long long frames = 0;           ///< set by the caller (SetFrames) for fps
			/// This export's own events: counted on the thread that called Start() and the
			/// library threads it started (GpuCounters::Block). What to fail an export on.
			unsigned long long counters[GpuCounters::Count] = {};
			/// Process-wide deltas over the same time, other exports' events included.
			unsigned long long process_counters[GpuCounters::Count] = {};

			/// One line: frames, wall time, fps, GPU/NVENC/NVDEC busy, VRAM, the
			/// fallback counters that moved.
			std::string Summary() const;
		};

		ExportTelemetry();
		~ExportTelemetry();

		/// Was this build compiled with NVML support (it needs the CUDA headers at build
		/// time, as CudaInterop does)? False on a CPU-Skia build without them.
		static bool NvmlBuiltIn();

		/// The render device's memory right now, for a caller deciding whether another
		/// export fits (the service's admission gate). @c nvml false means no NVIDIA
		/// driver, and every figure is zero. "Used" is NVML's v2 figure, the one
		/// nvidia-smi shows; @c process_bytes is this process's share of it.
		struct DeviceMemory
		{
			bool nvml = false;
			std::string device;
			unsigned long long total_bytes = 0;
			unsigned long long used_bytes = 0;
			unsigned long long free_bytes = 0;
			unsigned long long process_bytes = 0;
		};
		static DeviceMemory DeviceMemoryNow();

		/// Start sampling on a thread of its own (every 250 ms), and attribute the calling
		/// thread's counter events to this export until Stop(). Idempotent. Call Start() and
		/// Stop() on the thread that renders the export.
		void Start();

		/// How many frames the export wrote, for Summary()'s fps.
		void SetFrames(long long frames);

		/// Stop sampling and return what was seen since Start().
		Report Stop();

		ExportTelemetry(const ExportTelemetry&) = delete;
		ExportTelemetry& operator=(const ExportTelemetry&) = delete;

	private:
		class Impl;
		std::unique_ptr<Impl> impl;
	};
}
