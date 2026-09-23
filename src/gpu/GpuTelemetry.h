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
			Count
		};

		static void Add(Counter counter, unsigned long long n = 1);
		static unsigned long long Get(Counter counter);
		static const char* Name(Counter counter);
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
			unsigned long long counters[GpuCounters::Count] = {};  ///< deltas over the export

			/// One line: frames, wall time, fps, GPU/NVENC/NVDEC busy, VRAM, the
			/// fallback counters that moved.
			std::string Summary() const;
		};

		ExportTelemetry();
		~ExportTelemetry();

		/// Was this build compiled with NVML support (it needs the CUDA headers at build
		/// time, as CudaInterop does)? False on a CPU-Skia build without them.
		static bool NvmlBuiltIn();

		/// Start sampling on a thread of its own (every 250 ms). Idempotent.
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
