// An offscreen drawing surface that matches its destination. See GpuOffscreen.h.

#include "GpuOffscreen.h"

#include "GpuDevice.h"

#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImageInfo.h"

using namespace openshot;

bool GpuOffscreen::IsGpuBacked(const SkCanvas* canvas)
{
	// SkCanvas::recorder() is declared whether or not Skia was built with
	// Graphite, and returns null for every raster canvas, so this needs no
	// OPENSHOT_HAVE_SKIA_GPU guard and no Graphite header — the Recorder stays an
	// opaque pointer here.
	return canvas != nullptr && const_cast<SkCanvas*>(canvas)->recorder() != nullptr;
}

int GpuOffscreen::PaddedSize(int size)
{
	if (size <= 0)
		return 0;
	return (size + kSizeStep - 1) / kSizeStep * kSizeStep;
}

GpuOffscreen GpuOffscreen::Match(const SkCanvas* target, int width, int height)
{
	GpuOffscreen offscreen;
	if (width <= 0 || height <= 0)
		return offscreen;
	offscreen.offscreen_width = width;
	offscreen.offscreen_height = height;

	// kN32, not GpuFrame's kRGBA_8888 default: these offscreens stand in for surfaces
	// that were SkImageInfo::MakeN32Premul, and every one of them ends up drawn onto a
	// destination of that same type. Matching it keeps the two branches of this function
	// byte-identical in layout as well as in colour, so none of the R/B reasoning that
	// applies to a kRGBA_8888 GPU surface applies here at all.
	//
	// Padded to the pool step and clipped to the requested size; see the class comment.
	// The clip goes inside a save(): a recycled surface's canvas keeps a clip set at the
	// base level -- GpuSurfacePool::resetCanvas restores to the base save level and resets
	// the matrix, which is all SkCanvas lets it do -- so a base-level clip would cut the
	// surface's next user down to this size. Inside a save() the pool's reset pops it.
	if (IsGpuBacked(target))
		offscreen.gpu_frame = GpuFrame::Create(PaddedSize(width), PaddedSize(height), kN32_SkColorType);
	if (offscreen.gpu_frame) {
		if (SkCanvas* canvas = offscreen.gpu_frame->canvas();
			canvas && (offscreen.gpu_frame->width() != width || offscreen.gpu_frame->height() != height)) {
			canvas->save();
			canvas->clipRect(SkRect::MakeIWH(width, height));
		}
		return offscreen;
	}

	// Raster destination, or the pool could not hand out a surface. N32 premultiplied
	// with no colour space is what every call site used before this class existed;
	// changing it would change blending, not just where the pixels live.
	offscreen.raster_surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
	return offscreen;
}

SkCanvas* GpuOffscreen::canvas() const
{
	if (gpu_frame)
		return gpu_frame->canvas();
	return raster_surface ? raster_surface->getCanvas() : nullptr;
}

sk_sp<SkImage> GpuOffscreen::snapshot() const
{
	if (gpu_frame) {
		// Only the requested rectangle of the padded surface. Graphite copies on every
		// snapshot anyway, so a subset costs no more than the whole.
		if (gpu_frame->width() != offscreen_width || gpu_frame->height() != offscreen_height)
			return snapshot(SkIRect::MakeWH(offscreen_width, offscreen_height));
		return gpu_frame->snapshot();
	}
	return raster_surface ? raster_surface->makeImageSnapshot() : nullptr;
}

sk_sp<SkImage> GpuOffscreen::snapshot(const SkIRect& bounds) const
{
	if (gpu_frame)
		return gpu_frame->surface() ? gpu_frame->surface()->makeImageSnapshot(bounds) : nullptr;
	return raster_surface ? raster_surface->makeImageSnapshot(bounds) : nullptr;
}
