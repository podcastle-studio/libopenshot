/**
 * @file
 * @brief Source file for GpuEffect class
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "GpuEffect.h"

#include "Frame.h"
#include "ZmqLogger.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"

#include <QImage>

#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
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

// A compiled fragment. Cached on the effect instance, which outlives every frame
// it is asked about, so the SkSL is compiled once per effect rather than per
// frame. An SkRuntimeEffect is a program description and holds nothing belonging
// to the Graphite Context, so unlike a texture or a surface it does NOT have to
// be keyed on GpuDevice::Generation() and survives a device teardown intact.
struct GpuEffect::Program
{
	sk_sp<SkRuntimeEffect> effect;  ///< null once compilation has been tried and failed
	bool compiled = false;          ///< tried, so a failure is not retried every frame
};

GpuEffect::GpuEffect() = default;
GpuEffect::~GpuEffect() = default;

const char* GpuEffect::GpuShaderPrelude()
{
	// Keep this in step with the front end's copy. Every helper exists because a
	// CPU twin does exactly this and a shader that does the cleaner thing instead
	// stops being bit-identical.
	//
	// SkSL is the GLSL ES 1.00 intrinsic set, which is narrower than it looks:
	// there is no round() and no trunc(), only floor(). That is not a Skia quirk to
	// route around — the front end reaches these fragments through CanvasKit, which
	// has the same ceiling, so a helper written in terms of floor() is a helper both
	// sides can actually run. `openshot-gpu-effect-parity --sksl` asks the compiler
	// directly when something looks like it ought to work.
	return R"SKSL(
uniform shader osSrc;   // the frame: premultiplied, sampled 1:1 at texel centres

// The premultiplied source pixel at p, as the four 0..255 bytes the C++ reads.
// Rounding is safe rather than sloppy: the surface is 8-bit, so every channel came
// from an exact byte, and rounding recovers it even through a half-precision
// eval() (half carries 11 bits and 255 needs 8). floor(x + 0.5) because SkSL has
// no round(); the values are never negative, so the two agree.
float4 osBytes(float2 p) { return floor(float4(osSrc.eval(p)) * 255.0 + 0.5); }

// Alpha as a fraction, with the CPU twins' guard: A == 0 becomes 1.0 so the
// unpremultiply has something to divide by. Those pixels are fully transparent
// and their colour is not observable either way.
float osAlphaPercent(float a_byte) { return a_byte == 0.0 ? 1.0 : a_byte / 255.0; }

// Unpremultiply to 0..255. floor(), not round(), because the C++ writes
// static_cast<unsigned char>(pixels[i] / alpha_percent) and a cast truncates.
// A premultiplied channel never exceeds its alpha, so the quotient stays <= 255
// and the cast's wrap-around is unreachable.
float3 osUnpremul(float3 premul_bytes, float alpha_percent) {
	return floor(premul_bytes / alpha_percent);
}

// The constrain() every CPU effect defines for itself. Named per arity because
// SkSL is not GLSL and does not promise user-function overloading.
float3 osConstrain3(float3 v) { return clamp(v, 0.0, 255.0); }
float  osConstrain1(float v)  { return clamp(v, 0.0, 255.0); }

// static_cast<int>, which truncates toward zero rather than flooring. SkSL has no
// trunc(), so it is spelled out. It only differs from floor() on negatives, and
// every caller so far clamps those to 0 immediately, but a helper that is only
// right for its current callers is a trap for the next fragment.
float3 osToInt3(float3 v) { return sign(v) * floor(abs(v)); }
float  osToInt1(float v)  { return sign(v) * floor(abs(v)); }

// Premultiply back and return what the surface stores. Truncating again, because
// the C++ writes static_cast<unsigned char>(Rb * alpha_percent). The result is
// valid premultiplied colour: floor(b * alpha) <= floor(255 * alpha) <= A.
float4 osPremul(float3 bytes, float alpha_percent, float a_byte) {
	return float4(floor(bytes * alpha_percent), a_byte) / 255.0;
}
)SKSL";
}

bool GpuEffect::ApplyOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
	if (!frame)
		return false;

	// The one question every GPU path in this fork asks, and false is a normal
	// answer: no GPU, OPENSHOT_GPU=off, or SetBackend(Off) at runtime.
	if (!GpuDevice::Instance().available())
		return false;

	if (!program)
		program = std::make_shared<Program>();
	if (!program->compiled) {
		program->compiled = true;
		SkString source(GpuShaderPrelude());
		source.append(GpuShaderSource());
		auto [effect, error] = SkRuntimeEffect::MakeForShader(source);
		if (!effect) {
			// A fragment that will not compile is a mistake in this repo, not a
			// runtime condition — but it must cost quality, not the export. Log it
			// once and leave the frame to the CPU twin forever after.
			// AppendDebugMethod's arguments are floats, and the compiler error is the
			// whole point of this message, so it goes through the string logger.
			ZmqLogger::Instance()->AppendDebugMethod(
				"GpuEffect::ApplyOnGpu (SkSL failed to compile, using the CPU path)");
			ZmqLogger::Instance()->Log("GpuEffect: " + info.class_name + " SkSL: " +
									   std::string(error.c_str()));
		}
		program->effect = std::move(effect);
	}
	if (!program->effect)
		return false;

	const int width = frame->GetWidth();
	const int height = frame->GetHeight();
	if (width <= 0 || height <= 0)
		return false;

	// The source pixels as a texture. A frame that is already GPU-backed — the
	// previous effect in the chain, or a text clip — costs nothing here beyond the
	// snapshot's copy; a CPU frame pays one upload, which is the crossing W22-W25
	// exists to remove. `staging` has to outlive the draw below: it owns the
	// surface the snapshot was taken from.
	std::shared_ptr<GpuFrame> staging;
	sk_sp<SkImage> source;
	if (frame->IsGpuBacked()) {
		// A genuine copy, so drawing into a different surface below cannot race it,
		// and the tasks replay in the order they were recorded.
		source = frame->GpuBacking()->snapshot();
	} else {
		std::shared_ptr<QImage> image = frame->GetImage();
		if (!image || image->isNull())
			return false;
		// Format_RGBA8888_Premultiplied is byte-for-byte kRGBA_8888 premultiplied,
		// so this needs no conversion and no channel swap — the same reasoning as
		// Frame::FlattenGpuFrame in the other direction.
		if (image->format() != QImage::Format_RGBA8888_Premultiplied)
			return false;
		const SkPixmap pixels(
			SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType,
							  kPremul_SkAlphaType),
			image->constBits(), image->bytesPerLine());
		staging = GpuFrame::Create(image->width(), image->height(), kRGBA_8888_SkColorType);
		if (!staging || !staging->upload(pixels))
			return false;
		source = staging->snapshot();
	}
	if (!source)
		return false;

	std::shared_ptr<GpuFrame> destination =
		GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
	if (!destination)
		return false;

	SkRuntimeEffectBuilder builder(program->effect);

	// Nearest sampling, no local matrix. main()'s coordinate is the destination
	// pixel centre, so with an identity mapping every eval() lands on exactly one
	// texel and osBytes() recovers the source byte exactly. Linear filtering would
	// blend neighbours and no amount of care in the fragment would then match the
	// C++ — this one line is load-bearing for the parity gate.
	builder.child("osSrc") = source->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
												SkSamplingOptions());
	if (!SetGpuUniforms(builder, frame_number, width, height))
		return false;

	sk_sp<SkShader> shader = builder.makeShader();
	if (!shader)
		return false;

	SkPaint paint;
	paint.setShader(std::move(shader));
	// kSrc: this writes every pixel of a pooled surface that still holds whatever
	// its last user drew, so there is nothing to blend with and nothing to clear.
	paint.setBlendMode(SkBlendMode::kSrc);
	SkCanvas* canvas = destination->canvas();
	if (!canvas)
		return false;
	canvas->drawRect(SkRect::MakeIWH(width, height), paint);

	// The result becomes the frame's pixels without a readback: GetImage() will do
	// that once, whenever the first unported path asks.
	frame->AttachGpuFrame(std::move(destination));
	return true;
}
