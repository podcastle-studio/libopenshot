#pragma once

// One texture-backed drawing surface, borrowed from GpuSurfacePool for its
// lifetime, with CPU transfers both ways.

#include <memory>
#include <thread>

#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorSpace.h"
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
		/// @a color_space defaults to null (Skia legacy mode), matching the raster
		/// surfaces this replaces; see GpuSurfacePool::acquire.
		static std::shared_ptr<GpuFrame> Create(
			int width, int height, SkColorType color_type = kRGBA_8888_SkColorType,
			sk_sp<SkColorSpace> color_space = nullptr);

		/// Make @a image texture-backed on this thread's recorder, or null when the
		/// GPU is unavailable or the conversion failed.
		///
		/// Graphite will NOT do this implicitly. A raster SkImage used as a shader
		/// — including as a runtime-effect child — is silently dropped with
		/// "Couldn't convert SkImage to a Graphite-backed representation" and the
		/// draw disappears. Ganesh uploaded such images automatically; Graphite does
		/// not, so every image crossing onto a GPU surface goes through here first.
		static sk_sp<SkImage> ToTexture(const sk_sp<SkImage>& image);

		~GpuFrame();

		/// Is this frame usable from the calling thread?
		///
		/// A Graphite surface belongs to the recorder that made it, and recorders
		/// are per thread, so reading or drawing one from another thread is not
		/// safe. It also dies with the device, so a surface from an older
		/// GpuDevice::Generation() is gone whatever thread asks. Anything that
		/// holds a GPU-backed frame across calls — a reader's frame cache, say —
		/// has to ask this before using one, and produce the frame again if the
		/// answer is no.
		bool ownedByThisThread() const;

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
		/// Blocks until the GPU has finished. @a dst must own writable memory. @a count false
		/// keeps a probe (not a frame) out of GpuCounters::Readback.
		bool readback(const SkPixmap& dst, bool count = true);

		GpuFrame(const GpuFrame&) = delete;
		GpuFrame& operator=(const GpuFrame&) = delete;

	private:
		GpuFrame(sk_sp<SkSurface> surface, int width, int height, SkColorType color_type);

		sk_sp<SkSurface> frame_surface;
		std::thread::id frame_owner = std::this_thread::get_id();
		unsigned long long frame_generation = 0;
		int frame_width = 0;
		int frame_height = 0;
		SkColorType frame_color_type = kUnknown_SkColorType;
	};
}

