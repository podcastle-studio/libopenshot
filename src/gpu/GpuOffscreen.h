#pragma once

// An offscreen drawing surface that matches the canvas it will be drawn onto.

#include <memory>

#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkRect.h"
#include "skia/include/core/SkRefCnt.h"
#include "skia/include/core/SkSurface.h"

#include "GpuFrame.h"

namespace openshot
{
	/**
	 * @brief An offscreen drawing surface that matches the canvas it will be drawn onto.
	 *
	 * The render path builds a lot of small offscreens — glow silhouettes, the
	 * baked 3D block textures, the large-sigma shadow — and every one of them is
	 * snapshotted and drawn onto a destination canvas. Which memory that offscreen
	 * should live in is not a property of the offscreen: it is a property of the
	 * destination. Drawing a raster image onto a GPU canvas means an upload;
	 * drawing a texture onto a raster canvas means a readback. Either way the
	 * mismatch costs a full copy of the offscreen, per frame, and for a shader
	 * child Graphite does not even do the upload — it drops the draw (see
	 * GpuFrame::ToTexture).
	 *
	 * So Match() asks the destination: a GPU canvas gets a pooled GpuFrame, a
	 * raster canvas gets SkSurfaces::Raster — both with the same @c kN32
	 * premultiplied info the call sites used before. Callers then write one drawing
	 * path and it stays on whichever side of the boundary its destination is
	 * already on.
	 *
	 * A GPU offscreen falls back to raster whenever the pool cannot hand one out,
	 * so valid() is about running out of memory, not about the GPU being off.
	 *
	 * **A GPU offscreen is padded.** The pooled surface is the requested size
	 * rounded up to a multiple of kSizeStep in each dimension, its canvas clipped to
	 * the requested rectangle, and snapshot() copies only that rectangle -- so the
	 * pixels are those of an exact-size surface (the clip bounds every blur layer
	 * exactly as the surface edge did) while surfaces whose size drifts by a few
	 * pixels a frame, or differs a little between clips, share one pooled
	 * allocation instead of taking a new one each. A 4K block bake whose glow
	 * margin is keyframed took a fresh ~40 MB surface every frame before this. The
	 * raster branch stays exact: those surfaces are freed when the offscreen dies.
	 *
	 * @note The pooled surface is returned to the pool when the GpuOffscreen dies,
	 * so it must outlive any drawing into its canvas — but not the image from
	 * snapshot(), which is a genuine copy of the contents.
	 */
	class GpuOffscreen
	{
	public:
		/// Is this canvas GPU-backed? Null, or a raster canvas, is false. Safe to
		/// call on a build with no Graphite: the answer is then always false.
		static bool IsGpuBacked(const SkCanvas* canvas);

		/// An offscreen of this size in the same memory as @a target. Invalid only
		/// when allocation failed; a raster @a target simply gets a raster surface.
		static GpuOffscreen Match(const SkCanvas* target, int width, int height);

		/// The step a GPU offscreen's pooled surface is rounded up to, in pixels
		static constexpr int kSizeStep = 256;

		/// Round a dimension up to the pool step used for GPU offscreens
		static int PaddedSize(int size);

		/// The size that was asked for (the drawable, snapshotted area)
		int width() const { return offscreen_width; }
		int height() const { return offscreen_height; }

		GpuOffscreen() = default;

		bool valid() const { return gpu_frame != nullptr || raster_surface != nullptr; }
		explicit operator bool() const { return valid(); }

		/// True when this offscreen is texture-backed, so its snapshot can be drawn
		/// onto a GPU canvas — or used as a shader child — without an upload.
		bool onGpu() const { return gpu_frame != nullptr; }

		/// Canvas to draw into, or null when invalid. Contents are undefined until
		/// cleared: a pooled GPU surface still holds whatever its last user drew.
		SkCanvas* canvas() const;

		/// A copy of the current contents — texture-backed when onGpu(). Later
		/// drawing into canvas() does not change it, so the offscreen may go away.
		sk_sp<SkImage> snapshot() const;

		/// As snapshot(), for a sub-rectangle of the offscreen
		sk_sp<SkImage> snapshot(const SkIRect& bounds) const;

		GpuOffscreen(GpuOffscreen&&) noexcept = default;
		GpuOffscreen& operator=(GpuOffscreen&&) noexcept = default;
		GpuOffscreen(const GpuOffscreen&) = delete;
		GpuOffscreen& operator=(const GpuOffscreen&) = delete;

	private:
		std::shared_ptr<GpuFrame> gpu_frame;
		sk_sp<SkSurface> raster_surface;
		int offscreen_width = 0;
		int offscreen_height = 0;
	};
}
