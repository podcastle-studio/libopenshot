#pragma once

// The two overlay-clip composites, run as shaders instead of OpenCV round trips.

#include <memory>

namespace openshot
{
	class Frame;

	/**
	 * @brief Additive blend and displacement map, on the GPU.
	 *
	 * These are the last two things in the render path that went through
	 * Frame::GetImageCV() / SetImageCV(), which is a QImage -> BGRA cv::Mat conversion
	 * out and a BGRA -> QImage conversion back, per overlay, per frame — and on a
	 * GPU-backed frame a readback and a re-upload on top. Both composites are
	 * per-pixel functions of two images, so both are one draw.
	 *
	 * Each returns false to decline, which is a normal answer and means the caller
	 * must run its OpenCV path: no GPU, an upload that failed, or a case the fragment
	 * does not implement. The one such case is **a size mismatch** — the C++ resizes
	 * the overlay with cv::resize, and OpenCV's INTER_LINEAR is a fixed-point filter
	 * that Skia's sampling does not reproduce, so matching it would mean guessing at
	 * a rasteriser rather than at arithmetic. Same-size overlays, which is what the
	 * golden suite and the service's transition overlays use, run on the GPU.
	 *
	 * @note Both leave the result as @a frame's GPU backing rather than reading it
	 * back, so an overlay in the middle of a chain costs no crossing at all.
	 */
	namespace GpuOverlay
	{
		/// frame.rgb += overlay.rgb, saturating; frame.a untouched. Matches
		/// Podcastle::Effects::additiveBlend, which adds only channels 0..2.
		bool AdditiveBlend(openshot::Frame& frame, openshot::Frame& overlay);

		/// Displace each pixel by the overlay's luminance, scaled by @a horizontal and
		/// @a vertical (fractions of the frame's width and height). Matches
		/// Podcastle::Effects::applyDisplacementMapEffect, nearest-sampled.
		bool DisplacementMap(openshot::Frame& frame, openshot::Frame& overlay,
							 double horizontal, double vertical);

		/// How many overlay composites ran as shaders, and how many declined, since the
		/// counters were last reset.
		///
		/// The same reason GpuEffect carries these: a path with a silent fallback cannot
		/// be tested by comparing pixels, because the fallback produces the right answer.
		/// `unit.gpu_overlay_path` in the golden suite asserts on them.
		long long GpuPasses();
		long long CpuFallbacks();
		void ResetCounters();
	}
}
