// Additive blend and displacement map as shaders. See GpuOverlay.h.

#include "GpuOverlay.h"

#include "EffectShaders.h"
#include "../effects/image-processing-lib/src/Planner/EffectPlan.h"

#include "../Frame.h"
#include "GpuDevice.h"
#include "GpuFrame.h"

#include <QImage>

#include <atomic>
#include <functional>

#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkM44.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkRect.h"
#include "skia/include/core/SkRefCnt.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkShader.h"
#include "skia/include/core/SkString.h"
#include "skia/include/core/SkTileMode.h"
#include "skia/include/effects/SkRuntimeEffect.h"

using namespace openshot;

namespace
{
	std::atomic<long long>& passCounter()
	{
		static std::atomic<long long> passes{0};
		return passes;
	}
	std::atomic<long long>& fallbackCounter()
	{
		static std::atomic<long long> fallbacks{0};
		return fallbacks;
	}

	// Both fragments are the shared ones in image-processing-lib/shaders/ (additive_blend.sksl,
	// displacement_map.sksl), concatenated onto the same prelude as every effect fragment -- the
	// bytes the editor compiles through CanvasKit.
	SkRuntimeEffect* compiled(const char* body, sk_sp<SkRuntimeEffect>& cache)
	{
		if (!cache) {
			SkString source(openshot::shaders::kPrelude);
			source.append(body);
			auto [effect, error] = SkRuntimeEffect::MakeForShader(source);
			cache = std::move(effect);
		}
		return cache.get();
	}

	// A frame's pixels as a texture, uploading only when they are not on the GPU
	// already. `staging` must outlive the draw: it owns the surface behind the image.
	sk_sp<SkImage> asTexture(Frame& frame, std::shared_ptr<GpuFrame>& staging)
	{
		if (frame.IsGpuBacked())
			return frame.GpuBacking()->snapshot();

		std::shared_ptr<QImage> image = frame.GetImage();
		if (!image || image->isNull() ||
			image->format() != QImage::Format_RGBA8888_Premultiplied)
			return nullptr;

		const SkPixmap pixels(
			SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType,
							  kPremul_SkAlphaType),
			image->constBits(), image->bytesPerLine());
		staging = GpuFrame::Create(image->width(), image->height(), kRGBA_8888_SkColorType);
		if (!staging || !staging->upload(pixels))
			return nullptr;
		return staging->snapshot();
	}

	// Everything the two composites share: bind both images, run the fragment into a
	// fresh surface, and make that the frame's pixels.
	bool draw(Frame& frame, Frame& overlay, SkRuntimeEffect* effect,
			  const std::function<void(SkRuntimeEffectBuilder&)>& uniforms)
	{
		// Counted on every path so a test can tell "the shader ran" from "it declined and
		// OpenCV produced the same pixels", which look identical from the outside.
		const auto declined = []() -> bool {
			fallbackCounter().fetch_add(1, std::memory_order_relaxed);
			return false;
		};

		if (!effect)
			return declined();

		const int width = frame.GetWidth();
		const int height = frame.GetHeight();
		if (width <= 0 || height <= 0)
			return declined();
		// The C++ resizes a mismatched overlay with cv::resize; Skia will not reproduce
		// OpenCV's INTER_LINEAR, so decline rather than differ. See GpuOverlay.h.
		if (overlay.GetWidth() != width || overlay.GetHeight() != height)
			return declined();

		std::shared_ptr<GpuFrame> frame_staging, overlay_staging;
		sk_sp<SkImage> frame_texture = asTexture(frame, frame_staging);
		sk_sp<SkImage> overlay_texture = asTexture(overlay, overlay_staging);
		if (!frame_texture || !overlay_texture)
			return declined();

		std::shared_ptr<GpuFrame> destination =
			GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
		if (!destination)
			return declined();

		SkRuntimeEffectBuilder builder(sk_ref_sp(effect));
		// Nearest and no local matrix on both, so every eval lands on one texel.
		builder.child("osSrc") = frame_texture->makeShader(
			SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions());
		builder.child("osOverlay") = overlay_texture->makeShader(
			SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions());
		uniforms(builder);

		sk_sp<SkShader> shader = builder.makeShader();
		if (!shader)
			return declined();

		SkPaint paint;
		paint.setShader(std::move(shader));
		paint.setBlendMode(SkBlendMode::kSrc);
		SkCanvas* canvas = destination->canvas();
		if (!canvas)
			return declined();
		canvas->drawRect(SkRect::MakeIWH(width, height), paint);

		frame.AttachGpuFrame(std::move(destination));
		passCounter().fetch_add(1, std::memory_order_relaxed);
		return true;
	}
}

bool GpuOverlay::AdditiveBlend(Frame& frame, Frame& overlay)
{
	if (!GpuDevice::Instance().available()) {
		fallbackCounter().fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	// Compiled once per process. An SkRuntimeEffect is a program description and holds
	// nothing from the Graphite Context, so unlike a texture it survives a teardown and
	// needs no Generation() key.
	static sk_sp<SkRuntimeEffect> cache;
	return draw(frame, overlay, compiled(openshot::shaders::kAdditiveBlend, cache),
				[](SkRuntimeEffectBuilder&) {});
}

bool GpuOverlay::DisplacementMap(Frame& frame, Frame& overlay,
								 double horizontal, double vertical)
{
	if (!GpuDevice::Instance().available()) {
		fallbackCounter().fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	static sk_sp<SkRuntimeEffect> cache;
	SkRuntimeEffect* effect = compiled(openshot::shaders::kDisplacementMap, cache);

	// The uniforms come from the shared planner, as the editor's do. It only plans a GPU pass
	// for an overlay the frame's size -- draw() declines the other case itself, before this runs.
	const Podcastle::Effects::EffectPlan plan = Podcastle::Effects::planEffect(
		"DISPLACEMENT_MAP",
		{{"horizontalDisplacement", horizontal}, {"verticalDisplacement", vertical}},
		frame.GetWidth(), frame.GetHeight(), overlay.GetWidth(), overlay.GetHeight());
	if (plan.steps.size() != 1 || plan.steps.front().kind != Podcastle::Effects::PlanStep::Kind::Gpu) {
		fallbackCounter().fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	const Podcastle::Effects::PlanPass& pass = plan.steps.front().passes.front();
	return draw(frame, overlay, effect, [&](SkRuntimeEffectBuilder& builder) {
		for (const Podcastle::Effects::PlanUniform& u : pass.uniforms)
			builder.uniform(u.name.c_str()).set(u.values.data(), static_cast<int>(u.values.size()));
	});
}

long long GpuOverlay::GpuPasses() { return passCounter().load(std::memory_order_relaxed); }
long long GpuOverlay::CpuFallbacks() { return fallbackCounter().load(std::memory_order_relaxed); }

void GpuOverlay::ResetCounters()
{
	passCounter().store(0, std::memory_order_relaxed);
	fallbackCounter().store(0, std::memory_order_relaxed);
}
