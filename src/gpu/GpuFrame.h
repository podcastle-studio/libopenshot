#pragma once

// One texture-backed drawing surface, borrowed from GpuSurfacePool for its
// lifetime, with CPU transfers both ways.

#include <memory>

#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkRefCnt.h"
#include "skia/include/core/SkSurface.h"

namespace openshot
{
	/**
	 * @brief One texture-backed drawing surface, with CPU transfers both ways.
	 *
	 * A GpuFrame borrows a render target from GpuSurfacePool for its lifetime and
	 * returns it on destruction, so creating one per frame is the intended use and
	 * does not allocate after the first few frames.
	 *
	 * Create() returns null whenever the GPU is unavailable — callers fall back to
	 * their raster path rather than treating that as an error.
	 *
	 * @note Surfaces are @c kRGBA_8888 by default, not the @c kN32 the raster path
	 * uses. On x86 raster N32 is BGRA, so anything that reads bytes out of a GPU
	 * surface and hands them to code expecting N32 must swap R and B — this is the
	 * channel-order trap called out in plan step 2.3.
	 */
	class GpuFrame
	{
	public:
		/// A frame of this size, or null when the GPU is unavailable. Contents are
		/// undefined: the surface may be recycled, so clear it before drawing.
		static std::shared_ptr<GpuFrame> Create(
			int width, int height, SkColorType color_type = kRGBA_8888_SkColorType);

		~GpuFrame();

		int width() const { return frame_width; }
		int height() const { return frame_height; }
		SkColorType colorType() const { return frame_color_type; }

		/// Canvas for this frame's surface. Never null for a live GpuFrame.
		SkCanvas* canvas();

		/// The surface itself, for Skia calls that need it
		const sk_sp<SkSurface>& surface() const { return frame_surface; }

		/// A GPU-backed snapshot of the current contents, or null on failure
		sk_sp<SkImage> snapshot();

		/// Copy CPU pixels into this frame, replacing its contents. @a src may be
		/// any size or colour type; it is scaled and converted to fit.
		bool upload(const SkPixmap& src);

		/// Copy this frame's pixels into @a dst, converting to its colour type.
		/// Blocks until the GPU has finished. @a dst must own writable memory.
		bool readback(const SkPixmap& dst);

		GpuFrame(const GpuFrame&) = delete;
		GpuFrame& operator=(const GpuFrame&) = delete;

	private:
		GpuFrame(sk_sp<SkSurface> surface, int width, int height, SkColorType color_type);

		sk_sp<SkSurface> frame_surface;
		int frame_width = 0;
		int frame_height = 0;
		SkColorType frame_color_type = kUnknown_SkColorType;
	};
}

