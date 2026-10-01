#pragma once

// One lock for opening and closing hardware codecs, process-wide.

#include <mutex>

namespace openshot
{
	/**
	 * @brief Serialises the open and close of hardware codecs (NVDEC, NVENC) across threads.
	 *
	 * Four exports in one process, all finishing or all starting in the same second, crashed
	 * about one run in eight (2026-10-01): a libnvcuvid worker thread segfaulted while two
	 * exports tore their NVDEC decoders down together, and a writer opening NVENC aborted in
	 * malloc while three others opened theirs. AddressSanitizer on both the service and this
	 * library saw nothing, and under gdb the timing changed enough that it never happened: the
	 * damage is in the driver's own state, from concurrent create/destroy of codecs that share
	 * one CUDA context. Decoding and encoding themselves are left alone -- the driver is fine
	 * with many codecs working at once -- only their birth and death take this lock. Both are
	 * rare (once per clip, once per export), so it costs nothing measurable.
	 *
	 * Held by FFmpegReader::Open/Close around the video codec and FFmpegWriter::open_video /
	 * close_video. GpuDevice and CudaInterop locks ARE taken under it (device and stream
	 * creation and teardown), so the order is fixed, and nothing may take this lock while
	 * holding any of them:
	 *
	 *   Timeline.getFrameMutex -> Reader.getFrameMutex -> HardwareCodecMutex
	 *     -> gConvertSequence (FFmpegReader.cpp) -> interop state.mutex -> GpuDevice context mutex
	 *
	 * and on the decode path Reader.getFrameMutex -> gConvertSequence -> {recorder mutex,
	 * interop state.mutex -> context mutex}. Keep host waits on the GPU (vkQueueWaitIdle, stream
	 * synchronises) out from under this lock where possible: it is process-wide.
	 */
	inline std::mutex& HardwareCodecMutex()
	{
		static std::mutex m;
		return m;
	}

}
