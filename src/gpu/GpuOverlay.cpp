// Additive blend and displacement map as shaders. See GpuOverlay.h.

#include "GpuOverlay.h"

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

	// Shared by both fragments: read a pixel's four bytes. Same contract as
	// GpuEffect's prelude, repeated here because these are not effects and do not go
	// through GpuEffect -- the overlay composite lives in Clip, not in an EffectBase.
	const char* kPrelude = R"SKSL(
uniform shader osSrc;       // the frame being composited onto
uniform shader osOverlay;   // the overlay clip's frame, same size

float4 osBytes(float2 p)     { return floor(float4(osSrc.eval(p)) * 255.0 + 0.5); }
float4 osOverlayBytes(float2 p) { return floor(float4(osOverlay.eval(p)) * 255.0 + 0.5); }
)SKSL";

	// cv::add saturates, and additiveBlend touches only channels 0..2.
	const char* kAdditiveBlend = R"SKSL(
float4 main(float2 p) {
	float4 base = osBytes(p);
	float4 over = osOverlayBytes(p);
	return float4(min(base.rgb + over.rgb, 255.0), base.a) / 255.0;
}
)SKSL";

	// The displacement map. Two things here are the C++'s and are not free choices:
	//
	//   - the luminance comes from cv::cvtColor(..., COLOR_BGRA2GRAY), which for 8-bit
	//     is a FIXED-POINT sum, not a float one: (B*1868 + G*9617 + R*4899 + 8192) >> 14.
	//     Writing it as a float dot product would be close and not exact.
	//   - the gather is nearest with an explicit int(v + 0.5), and clamped to the last
	//     pixel, not wrapped. CLAUDE.md calls this out: the same code runs in the front
	//     end's WASM, so switching the shader to bilinear would open the editor/export
	//     gap that all of this exists to close.
	const char* kDisplacementMap = R"SKSL(
uniform float2 size;    // frame size in pixels
uniform float2 scale;   // hDisplacement * width / 2, vDisplacement * height / 2

float4 main(float2 p) {
	float4 map = osOverlayBytes(p);
	float grey = floor((map.b * 1868.0 + map.g * 9617.0 + map.r * 4899.0 + 8192.0) / 16384.0);

	float n = grey / 255.0;
	float2 q = floor(p);
	float2 moved = clamp(q + n * scale, float2(0.0), size - 1.0);
	// int(v + 0.5) on a non-negative value is floor(v + 0.5).
	float2 sampled = floor(moved + 0.5);

	return osBytes(sampled + 0.5) / 255.0;
}
)SKSL";

	SkRuntimeEffect* compiled(const char* body, sk_sp<SkRuntimeEffect>& cache)
	{
		if (!cache) {
			SkString source(kPrelude);
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
	return draw(frame, overlay, compiled(kAdditiveBlend, cache),
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
	SkRuntimeEffect* effect = compiled(kDisplacementMap, cache);

	const int width = frame.GetWidth();
	const int height = frame.GetHeight();
	return draw(frame, overlay, effect, [&](SkRuntimeEffectBuilder& builder) {
		builder.uniform("size") = SkV2{static_cast<float>(width), static_cast<float>(height)};
		// float, matching the C++'s static_cast<float> of the same products.
		builder.uniform("scale") =
			SkV2{static_cast<float>(horizontal * width / 2.0),
				 static_cast<float>(vertical * height / 2.0)};
	});
}

long long GpuOverlay::GpuPasses() { return passCounter().load(std::memory_order_relaxed); }
long long GpuOverlay::CpuFallbacks() { return fallbackCounter().load(std::memory_order_relaxed); }

void GpuOverlay::ResetCounters()
{
	passCounter().store(0, std::memory_order_relaxed);
	fallbackCounter().store(0, std::memory_order_relaxed);
}
