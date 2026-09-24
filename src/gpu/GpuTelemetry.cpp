// Per-export GPU telemetry. See GpuTelemetry.h.

#include "GpuTelemetry.h"

#include "GpuDevice.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(OPENSHOT_HAVE_CUDA)
#define OPENSHOT_NVML 1
#include <dlfcn.h>
#include <unistd.h>
#include <nvml.h>
#endif

using namespace openshot;

// --- GpuCounters --------------------------------------------------------------

namespace
{
	std::atomic<unsigned long long>* counterSlots()
	{
		static std::atomic<unsigned long long> slots[GpuCounters::Count] = {};
		return slots;
	}
}

void GpuCounters::Add(Counter counter, unsigned long long n)
{
	if (counter >= 0 && counter < Count)
		counterSlots()[counter].fetch_add(n, std::memory_order_relaxed);
}

unsigned long long GpuCounters::Get(Counter counter)
{
	return counter >= 0 && counter < Count ? counterSlots()[counter].load(std::memory_order_relaxed) : 0;
}

const char* GpuCounters::Name(Counter counter)
{
	switch (counter) {
	case Readback: return "readbacks";
	case EncodedOnDevice: return "encoded_on_device";
	case EncodedFromHost: return "encoded_from_host";
	case DecodedOnDevice: return "decoded_on_device";
	case DecodedOnGpu: return "decoded_on_gpu";
	case DecodedOnCpu: return "decoded_on_cpu";
	case CropOnGpu: return "crop_on_gpu";
	case CropReadback: return "crop_readback";
	case HardwareDecodeFallback: return "nvdec_fallbacks";
	case Upload: return "uploads";
	case UploadCached: return "uploads_cached";
	default: return "?";
	}
}

// --- NVML ---------------------------------------------------------------------

#ifdef OPENSHOT_NVML
namespace
{
	/// The few NVML entry points this needs, loaded once. The versioned names are asked
	/// for explicitly: nvml.h maps the plain ones onto them with macros, which dlsym
	/// does not see.
	struct Nvml
	{
		void* library = nullptr;
		bool ok = false;
		decltype(&nvmlInit_v2) Init = nullptr;
		decltype(&nvmlDeviceGetCount_v2) DeviceGetCount = nullptr;
		decltype(&nvmlDeviceGetHandleByIndex_v2) DeviceGetHandleByIndex = nullptr;
		decltype(&nvmlDeviceGetName) DeviceGetName = nullptr;
		decltype(&nvmlDeviceGetUtilizationRates) DeviceGetUtilizationRates = nullptr;
		decltype(&nvmlDeviceGetEncoderUtilization) DeviceGetEncoderUtilization = nullptr;
		decltype(&nvmlDeviceGetDecoderUtilization) DeviceGetDecoderUtilization = nullptr;
		decltype(&nvmlDeviceGetMemoryInfo_v2) DeviceGetMemoryInfo = nullptr;
		decltype(&nvmlDeviceGetGraphicsRunningProcesses_v3) DeviceGetGraphicsProcesses = nullptr;
		decltype(&nvmlDeviceGetComputeRunningProcesses_v3) DeviceGetComputeProcesses = nullptr;

		static Nvml& Instance()
		{
			static Nvml nvml = [] {
				Nvml n;
				n.library = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
				if (!n.library)
					return n;
				bool missing = false;
				const auto load = [&](auto& slot, const char* name) {
					slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(dlsym(n.library, name));
					if (!slot)
						missing = true;
				};
				load(n.Init, "nvmlInit_v2");
				load(n.DeviceGetCount, "nvmlDeviceGetCount_v2");
				load(n.DeviceGetHandleByIndex, "nvmlDeviceGetHandleByIndex_v2");
				load(n.DeviceGetName, "nvmlDeviceGetName");
				load(n.DeviceGetUtilizationRates, "nvmlDeviceGetUtilizationRates");
				load(n.DeviceGetEncoderUtilization, "nvmlDeviceGetEncoderUtilization");
				load(n.DeviceGetDecoderUtilization, "nvmlDeviceGetDecoderUtilization");
				// _v2: "used" without the driver's reservation, which is what nvidia-smi shows;
				// v1 counts ~350 MB of reserved memory as used.
				load(n.DeviceGetMemoryInfo, "nvmlDeviceGetMemoryInfo_v2");
				load(n.DeviceGetGraphicsProcesses, "nvmlDeviceGetGraphicsRunningProcesses_v3");
				load(n.DeviceGetComputeProcesses, "nvmlDeviceGetComputeRunningProcesses_v3");
				// Initialised once for the life of the process and never shut down: NVML
				// refcounts init/shutdown, and an export is not the unit that owns it.
				n.ok = !missing && n.Init() == NVML_SUCCESS;
				return n;
			}();
			return nvml;
		}
	};

	/// The NVML device this process renders on: the one whose name matches the Vulkan
	/// device when there is one, else the first. Null when NVML is unusable.
	nvmlDevice_t pickDevice(std::string& name)
	{
		Nvml& nvml = Nvml::Instance();
		if (!nvml.ok)
			return nullptr;
		unsigned int count = 0;
		if (nvml.DeviceGetCount(&count) != NVML_SUCCESS || count == 0)
			return nullptr;
		const std::string vulkan = GpuDevice::Instance().available() ? GpuDevice::Instance().deviceName() : "";
		nvmlDevice_t chosen = nullptr;
		for (unsigned int i = 0; i < count; ++i) {
			nvmlDevice_t device = nullptr;
			if (nvml.DeviceGetHandleByIndex(i, &device) != NVML_SUCCESS)
				continue;
			char buffer[NVML_DEVICE_NAME_V2_BUFFER_SIZE] = {0};
			nvml.DeviceGetName(device, buffer, sizeof(buffer));
			if (!chosen || (!vulkan.empty() && vulkan.find(buffer) != std::string::npos)) {
				chosen = device;
				name = buffer;
			}
		}
		return chosen;
	}

	/// This process's VRAM on @a device, in bytes. A process using both Vulkan and CUDA is in
	/// both of NVML's lists with the same per-process total, so the larger is taken, not the sum.
	unsigned long long processVram(nvmlDevice_t device)
	{
		Nvml& nvml = Nvml::Instance();
		const unsigned int pid = static_cast<unsigned int>(getpid());
		unsigned long long total = 0;
		for (auto query : {nvml.DeviceGetGraphicsProcesses, nvml.DeviceGetComputeProcesses}) {
			std::vector<nvmlProcessInfo_t> processes(64);
			unsigned int n = static_cast<unsigned int>(processes.size());
			if (query(device, &n, processes.data()) != NVML_SUCCESS)
				continue;
			for (unsigned int i = 0; i < n; ++i)
				if (processes[i].pid == pid && processes[i].usedGpuMemory != NVML_VALUE_NOT_AVAILABLE)
					total = std::max(total, processes[i].usedGpuMemory);
		}
		return total;
	}
}
#endif

// --- ExportTelemetry ----------------------------------------------------------

class ExportTelemetry::Impl
{
public:
	std::mutex mutex;
	std::condition_variable wake;
	std::thread sampler;
	bool running = false;
	std::chrono::steady_clock::time_point started;
	unsigned long long counters_at_start[GpuCounters::Count] = {};
	Report report;
	double gpu_sum = 0, encoder_sum = 0, decoder_sum = 0;

	void sample()
	{
#ifdef OPENSHOT_NVML
		std::string name;
		nvmlDevice_t device = pickDevice(name);
		{
			std::lock_guard<std::mutex> lock(mutex);
			report.nvml = device != nullptr;
			report.device = name;
		}
		if (!device)
			return;
		Nvml& nvml = Nvml::Instance();
		std::unique_lock<std::mutex> lock(mutex);
		while (running) {
			lock.unlock();
			nvmlUtilization_t util{};
			unsigned int encoder = 0, decoder = 0, period = 0;
			nvmlMemory_v2_t memory{};
			memory.version = nvmlMemory_v2;
			const bool have_util = nvml.DeviceGetUtilizationRates(device, &util) == NVML_SUCCESS;
			const bool have_enc = nvml.DeviceGetEncoderUtilization(device, &encoder, &period) == NVML_SUCCESS;
			const bool have_dec = nvml.DeviceGetDecoderUtilization(device, &decoder, &period) == NVML_SUCCESS;
			const bool have_mem = nvml.DeviceGetMemoryInfo(device, &memory) == NVML_SUCCESS;
			const double process_mb = processVram(device) / (1024.0 * 1024.0);
			lock.lock();
			++report.samples;
			if (have_util) {
				gpu_sum += util.gpu;
				report.gpu_busy_peak = std::max(report.gpu_busy_peak, double(util.gpu));
			}
			if (have_enc) encoder_sum += encoder;
			if (have_dec) decoder_sum += decoder;
			if (have_mem)
				report.vram_device_peak_mb = std::max(report.vram_device_peak_mb, memory.used / (1024.0 * 1024.0));
			report.vram_process_peak_mb = std::max(report.vram_process_peak_mb, process_mb);
			wake.wait_for(lock, std::chrono::milliseconds(250), [&] { return !running; });
		}
#endif
	}
};

ExportTelemetry::ExportTelemetry() : impl(new Impl) {}

bool ExportTelemetry::NvmlBuiltIn()
{
#ifdef OPENSHOT_NVML
	return true;
#else
	return false;
#endif
}

ExportTelemetry::~ExportTelemetry()
{
	Stop();
}

void ExportTelemetry::Start()
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	if (impl->running)
		return;
	impl->report = Report();
	impl->gpu_sum = impl->encoder_sum = impl->decoder_sum = 0;
	for (int i = 0; i < GpuCounters::Count; ++i)
		impl->counters_at_start[i] = GpuCounters::Get(static_cast<GpuCounters::Counter>(i));
	impl->started = std::chrono::steady_clock::now();
	impl->running = true;
	impl->sampler = std::thread([this] { impl->sample(); });
}

void ExportTelemetry::SetFrames(long long frames)
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	impl->report.frames = frames;
}

ExportTelemetry::Report ExportTelemetry::Stop()
{
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		if (!impl->running)
			return impl->report;
		impl->running = false;
	}
	impl->wake.notify_all();
	if (impl->sampler.joinable())
		impl->sampler.join();

	std::lock_guard<std::mutex> lock(impl->mutex);
	Report& r = impl->report;
	r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - impl->started).count();
	if (r.samples > 0) {
		r.gpu_busy_mean = impl->gpu_sum / r.samples;
		r.encoder_mean = impl->encoder_sum / r.samples;
		r.decoder_mean = impl->decoder_sum / r.samples;
	}
	for (int i = 0; i < GpuCounters::Count; ++i)
		r.counters[i] = GpuCounters::Get(static_cast<GpuCounters::Counter>(i)) - impl->counters_at_start[i];
	return r;
}

std::string ExportTelemetry::Report::Summary() const
{
	std::ostringstream out;
	out.setf(std::ios::fixed);
	out.precision(1);
	out << "frames=" << frames << " wall=" << seconds << "s";
	if (frames > 0 && seconds > 0)
		out << " fps=" << frames / seconds;
	if (nvml) {
		out << " | gpu_busy=" << gpu_busy_mean << "% (peak " << gpu_busy_peak << "%)"
			<< " nvenc=" << encoder_mean << "% nvdec=" << decoder_mean << "%"
			<< " vram_peak=" << vram_process_peak_mb << "MB (device " << vram_device_peak_mb << "MB)"
			<< " samples=" << samples << " device=\"" << device << "\" (utilisation is device-wide)";
	} else {
		out << (NvmlBuiltIn() ? " | no NVML (no NVIDIA driver): GPU figures unavailable"
							  : " | built without NVML: GPU figures unavailable");
	}
	out << " |";
	bool any = false;
	for (int i = 0; i < GpuCounters::Count; ++i) {
		if (!counters[i])
			continue;
		out << " " << GpuCounters::Name(static_cast<GpuCounters::Counter>(i)) << "=" << counters[i];
		any = true;
	}
	if (!any)
		out << " no GPU-path events";
	return out.str();
}
