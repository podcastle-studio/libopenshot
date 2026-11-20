// One texture-backed drawing surface with CPU transfers both ways. See GpuFrame.h.

#include "GpuFrame.h"

#include <atomic>
#include "GpuTelemetry.h"

#include "GpuDevice.h"
#include "GpuSurfacePool.h"
#include <chrono>
#include <memory>
#include <thread>

#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkRect.h"
#include "skia/include/core/SkSamplingOptions.h"

#ifdef OPENSHOT_HAVE_SKIA_GPU
#include "skia/include/core/SkImage.h"
#include "skia/include/gpu/graphite/Context.h"
#include "skia/include/gpu/graphite/Image.h"
#include "skia/include/gpu/graphite/Recorder.h"
#include "skia/include/gpu/graphite/Surface.h"
#endif

using namespace openshot;

GpuFrame::GpuFrame(sk_sp<SkSurface> surface, int width, int height, SkColorType color_type)
	: frame_surface(std::move(surface)), frame_generation(GpuDevice::Generation()),
	  frame_width(width), frame_height(height), frame_color_type(color_type)
{
}

bool GpuFrame::ownedByThisThread() const
{
	return frame_owner == std::this_thread::get_id() &&
		   frame_generation == GpuDevice::Generation();
}

GpuFrame::~GpuFrame()
{
	// release() only ever matches a surface in the calling thread's own pool, so a
	// frame destroyed on the wrong thread is not put into the wrong pool -- it is
	// simply not returned, and the owning pool keeps it marked in use. That is a
	// surface the pool can no longer recycle, which is why ownedByThisThread()
	// exists: anything holding a GPU frame across calls should be releasing it on
	// the thread that made it.
	GpuSurfacePool::Instance().release(frame_surface);
}

std::shared_ptr<GpuFrame> GpuFrame::Create(int width, int height, SkColorType color_type,
										   sk_sp<SkColorSpace> color_space)
{
	sk_sp<SkSurface> surface = GpuSurfacePool::Instance().acquire(
		width, height, color_type, std::move(color_space));
	if (!surface)
		return nullptr;
	return std::shared_ptr<GpuFrame>(
		new GpuFrame(std::move(surface), width, height, color_type));
}

sk_sp<SkImage> GpuFrame::ToTexture(const sk_sp<SkImage>& image)
{
#ifdef OPENSHOT_HAVE_SKIA_GPU
	if (!image)
		return nullptr;
	if (image->isTextureBacked())
		return image;
	skgpu::graphite::Recorder* recorder = GpuDevice::Instance().recorder();
	if (!recorder)
		return nullptr;
	GpuCounters::Add(GpuCounters::Upload);
	return SkImages::TextureFromImage(recorder, image.get(), {});
#else
	(void)image;
	return nullptr;
#endif
}

SkCanvas* GpuFrame::canvas()
{
	return frame_surface ? frame_surface->getCanvas() : nullptr;
}

sk_sp<SkImage> GpuFrame::snapshot()
{
	if (!frame_surface)
		return nullptr;
	return frame_surface->makeImageSnapshot();
}

bool GpuFrame::upload(const SkPixmap& src)
{
#ifdef OPENSHOT_HAVE_SKIA_GPU
	if (!frame_surface || !src.addr() || src.width() <= 0 || src.height() <= 0)
		return false;

	skgpu::graphite::Recorder* recorder = GpuDevice::Instance().recorder();
	if (!recorder)
		return false;

	// RasterFromPixmapCopy, not a borrowed pixmap: the recording is replayed
	// later, by which time the caller's buffer may be gone.
	sk_sp<SkImage> raster = SkImages::RasterFromPixmapCopy(src);
	if (!raster)
		return false;
	GpuCounters::Add(GpuCounters::Upload);
	sk_sp<SkImage> texture = SkImages::TextureFromImage(recorder, raster.get(), {});
	if (!texture)
		return false;

	SkCanvas* target = frame_surface->getCanvas();
	SkPaint paint;
	// kSrc, not the default kSrcOver: upload replaces the frame's contents, and a
	// pooled surface still holds whatever the previous user drew.
	paint.setBlendMode(SkBlendMode::kSrc);
	target->drawImageRect(texture,
						  SkRect::MakeWH(static_cast<float>(src.width()),
										 static_cast<float>(src.height())),
						  SkRect::MakeWH(static_cast<float>(frame_width),
										 static_cast<float>(frame_height)),
						  SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone),
						  &paint, SkCanvas::kFast_SrcRectConstraint);
	return true;
#else
	(void)src;
	return false;
#endif
}

#ifdef OPENSHOT_HAVE_SKIA_GPU
namespace
{
	struct ReadbackState
	{
		std::unique_ptr<const SkImage::AsyncReadResult> result;
		// The callback runs on whichever thread drives the Context (any export's submit or
		// poll), and the waiting thread reads this between polls without the context mutex:
		// release/acquire publishes `result` with it.
		std::atomic<bool> done{false};
	};

	void onReadbackComplete(SkImage::ReadPixelsContext context,
							std::unique_ptr<const SkImage::AsyncReadResult> result)
	{
		auto* state = static_cast<ReadbackState*>(context);
		state->result = std::move(result);
		state->done.store(true, std::memory_order_release);
	}
}
#endif

bool GpuFrame::readback(const SkPixmap& dst, bool count)
{
#ifdef OPENSHOT_HAVE_SKIA_GPU
	if (!frame_surface || !dst.writable_addr() || dst.width() <= 0 || dst.height() <= 0)
		return false;

	GpuDevice& device = GpuDevice::Instance();
	skgpu::graphite::Context* context = device.context();
	if (!context)
		return false;

	// Everything recorded into this surface has to reach the GPU before the read
	// is queued behind it.
	if (!device.submit(false)) {
		GpuCounters::Add(GpuCounters::ReadbackFailure);
		return false;
	}

	// The Context is single-owner: every call into it holds the context mutex, as submit()
	// does. These two ran unlocked until 2026-10-01 (review C1), while other exports'
	// threads inserted and submitted recordings on the same Context.
	//
	// The state is on the heap and outlives a wait that gives up: the callback still fires
	// once the GPU gets there, and on the stack it would write into a dead frame.
	auto* state = new ReadbackState;
	{
		GpuDevice::QueueGuard guard;
		context->asyncRescaleAndReadPixels(frame_surface.get(), dst.info(),
										   SkIRect::MakeWH(frame_width, frame_height),
										   SkImage::RescaleGamma::kSrc,
										   SkImage::RescaleMode::kNearest,
										   onReadbackComplete, state);
		// kNo, then poll: kYes waited for every export's outstanding work with the context
		// mutex held, collapsing the whole process's pipeline to one submission at every
		// readback (review P3).
		if (!context->submit(skgpu::graphite::SyncToCpu::kNo))
			GpuCounters::Add(GpuCounters::SubmitFailure);
	}
	// Bounded by time, not by a spin count: 10 000 unslept spins were 10-50 ms, short of what
	// a busy GPU needs, and a timeout silently dropped the frame's pixels (review F2).
	const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	for (int spins = 0; !state->done.load(std::memory_order_acquire); ++spins) {
		{
			GpuDevice::QueueGuard guard;
			context->checkAsyncWorkCompletion();
		}
		if (state->done.load(std::memory_order_acquire))
			break;
		if (std::chrono::steady_clock::now() > give_up)
			break;
		if (spins < 64)
			std::this_thread::yield();
		else
			std::this_thread::sleep_for(std::chrono::microseconds(50));
	}
	if (!state->done.load(std::memory_order_acquire)) {
		// Leaked on purpose: the callback has yet to run and will write into it.
		GpuCounters::Add(GpuCounters::ReadbackFailure);
		return false;
	}
	std::unique_ptr<ReadbackState> owned(state);
	if (!owned->result) {
		GpuCounters::Add(GpuCounters::ReadbackFailure);
		return false;
	}

	const SkPixmap source(dst.info(), owned->result->data(0), owned->result->rowBytes(0));
	const bool copied = source.readPixels(dst);
	if (copied) {
		if (count)
			GpuCounters::Add(GpuCounters::Readback);
	} else {
		GpuCounters::Add(GpuCounters::ReadbackFailure);
	}

	// The result's pixels belong to the context and are invalidated when it goes
	// away; drop them here rather than letting them outlive this call.
	owned->result.reset();
	return copied;
#else
	(void)dst;
	return false;
#endif
}
