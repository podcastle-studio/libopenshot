/**
 * @file
 * @brief Header file for global Settings class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_SETTINGS_H
#define OPENSHOT_SETTINGS_H

#include <string>

namespace openshot {

	/**
	 * @brief This class is contains settings used by libopenshot (and can be safely toggled at any point)
	 *
	 * Settings class is used primarily to toggle scale settings between preview and rendering, and adjust
	 * other runtime related settings.
	 */
	class Settings {
	private:

		/// Default constructor
		Settings(){}; 						 // Don't allow user to create an instance of this singleton

#if __GNUC__ >=7
		/// Default copy method
		Settings(Settings const&) = delete; // Don't allow the user to assign this instance

		/// Default assignment operator
		Settings & operator=(Settings const&) = delete;  // Don't allow the user to assign this instance
#else
		/// Default copy method
		Settings(Settings const&) {}; // Don't allow the user to assign this instance

		/// Default assignment operator
		Settings & operator=(Settings const&);  // Don't allow the user to assign this instance
#endif

		/// Private variable to keep track of singleton instance
		static Settings * m_pInstance;

		/// Last OMP thread count applied to the OpenMP runtime
		int applied_omp_threads = 0;

		/// Machine default OpenMP thread count detected at startup
		int default_omp_threads = 2;

		/// Machine default FFmpeg thread count detected at startup
		int default_ff_threads = 2;

	public:
		/**
		 * @brief Use video codec for faster video decoding (if supported)
		 *
		 * 0 - No acceleration,
		 * 1 - Linux VA-API,
		 * 2 - nVidia NVDEC,
		 * 3 - Windows D3D9,
		 * 4 - Windows D3D11,
		 * 5 - MacOS / VideoToolBox,
		 * 6 - Linux VDPAU,
		 * 7 - Intel QSV
		 */
		int HARDWARE_DECODER = 0;

		/// Scale mode used in FFmpeg decoding and encoding (used as an optimization for faster previews)
		bool HIGH_QUALITY_SCALING = true;

		/**
		 * @brief Let the reader convert YUV to RGBA on the GPU, so decoded frames stay there.
		 *
		 * Needs a GPU as well (@c GpuDevice::available()); with one, the reader runs the
		 * conversion and the pre-scale as an SkSL pass and hands the compositor a GPU-backed
		 * frame, so nothing crosses the bus but the YUV. It replaces @c sws_scale, which is
		 * 95 % of the reader's wall clock.
		 *
		 * **Off by default because it changes pixels, not because it is unfinished.** Two
		 * differences, both measured (see GPU-WORKLIST W23):
		 *
		 * - the conversion itself is faithful — 46.2 dB, 3 LSB at worst — but it is not
		 *   bit-identical, and effects that threshold or divide (chroma key, colour-burn)
		 *   turn 3 LSB into a visible difference;
		 * - **it honours the stream's declared colour space and swscale, as this reader
		 *   configures it, never did.** A file tagged @c bt709 decoded as BT.709 rather than
		 *   BT.601 is the correct answer and a different picture.
		 *
		 * Turning it on is the project owner's call, and it re-baselines every golden that
		 * decodes video.
		 */
		bool GPU_DECODE = false;

		/**
		 * @brief How many frames each FFmpegReader decodes ahead, on a thread of its own.
		 *
		 * An export reads every clip front to back, so while the timeline composites frame N
		 * the reader can already be decoding N+1..N+this. 0 turns it off. A seek retargets it.
		 * Only frames that live in host memory are decoded ahead: a GPU-decoded frame belongs
		 * to the thread whose recorder made it, so with GPU_DECODE on and a GPU present the
		 * reader decodes on the caller's thread as before (W24).
		 *
		 * 2, not the 4 first planned: measured on the CPU path, 2 is as fast as 4 within noise
		 * (source_4k x264 +20-25 %, grid_3x3 +12 %) and each extra frame is a full frame of
		 * host memory per open reader -- 4 cost grid_3x3's nine readers 270 MB.
		 */
		int READ_AHEAD_FRAMES = 2;

		/**
		 * @brief Hand NVENC the compositor's frame on the GPU instead of reading it back (W25).
		 *
		 * With h264_nvenc/hevc_nvenc, a GPU compositor and CudaInterop available, each frame is
		 * converted to NV12 by an SkSL pass and copied device to device into the encoder's
		 * input; the readback, the writer's swscale and the upload all go. Anything short of
		 * that falls back to the readback path.
		 *
		 * **Off by default because it changes pixels:** the chroma is a 2x2 box average where
		 * the readback path's swscale (HIGH_QUALITY_SCALING) uses a bicubic, and the luma
		 * rounds exactly where swscale's does not quite. Same matrix (BT.601) either way.
		 * Turning it on is the project owner's call, like GPU_DECODE.
		 */
		bool GPU_ENCODE = false;

		/// Number of OpenMP threads
		int OMP_THREADS = 2;

			/// Number of threads that ffmpeg uses
			int FF_THREADS = 16;

			/// Minimum number of frames for frame-count-based caches
			int CACHE_MIN_FRAMES = 24;

		/// Which GPU to use to decode (0 is the first)
		int HW_DE_DEVICE_SET = 0;

		/// Which GPU to use to encode (0 is the first)
		int HW_EN_DEVICE_SET = 0;

		/// Percentage of cache in front of the playhead (0.0 to 1.0)
		float VIDEO_CACHE_PERCENT_AHEAD = 0.7;

		/// Minimum number of frames to cache before playback begins
		int VIDEO_CACHE_MIN_PREROLL_FRAMES = 30;

		/// Max number of frames (ahead of playhead) to cache during playback
		int VIDEO_CACHE_MAX_PREROLL_FRAMES = 60;

		/// Max number of frames (when paused) to cache for playback
		int VIDEO_CACHE_MAX_FRAMES = 30 * 10;

		/// Enable/Disable the cache thread to pre-fetch and cache video frames before we need them
		bool ENABLE_PLAYBACK_CACHING = true;

		/// The audio device name to use during playback
		std::string PLAYBACK_AUDIO_DEVICE_NAME = "";

		/// The device type for the playback audio devices
		std::string PLAYBACK_AUDIO_DEVICE_TYPE = "";

		/// Size of playback buffer before audio playback starts
		int PLAYBACK_AUDIO_BUFFER_SIZE = 512;

		/// The current install path of OpenShot (needs to be set when using Timeline(path), since certain
		/// paths depend on the location of OpenShot transitions and files)
		std::string PATH_OPENSHOT_INSTALL = "";

        ///
        bool ENABLE_LEGACY_MODE = false;

		///
		bool DISABLE_CACHING = true;

 		/// Whether to dump ZeroMQ debug messages to stderr
		bool DEBUG_TO_STDERR = false;

		/// Return the effective OpenMP worker budget used by libopenshot heuristics
		int EffectiveOMPThreads() const;

		/// Return the maximum allowed thread override based on this machine
		int MaxAllowedThreads() const;

		/// Return the machine default OpenMP thread count detected at startup
		int DefaultOMPThreads() const { return default_omp_threads; }

		/// Return the machine default FFmpeg thread count detected at startup
		int DefaultFFThreads() const { return default_ff_threads; }

		/// Apply any explicit OpenMP thread override to the runtime
		void ApplyOpenMPSettings();

		/// Create or get an instance of this logger singleton (invoke the class with this method)
		static Settings * Instance();
	};

}

#endif
