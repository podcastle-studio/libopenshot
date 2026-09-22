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

#include <atomic>
#include <utility>
#include <vector>

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
#include "EffectShaders.h"

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

// One Program per distinct fragment. Most effects have exactly one; Blur has four and picks
// between them per pass, so the cache is keyed on the source pointer rather than being a single
// slot. A vector because the count is one to four — a map would be more machinery than lookups.
struct GpuEffect::ProgramCache
{
	std::vector<std::pair<const char*, Program>> entries;

	Program& for_source(const char* source)
	{
		for (auto& entry : entries)
			if (entry.first == source)
				return entry.second;
		entries.emplace_back(source, Program{});
		return entries.back().second;
	}
};

namespace
{
	// Relaxed: these are for tests and diagnostics, so a torn read across threads
	// costs nothing worth an ordering guarantee.
	std::atomic<long long>& gpuPassCounter()
	{
		static std::atomic<long long> passes{0};
		return passes;
	}
	std::atomic<long long>& cpuFallbackCounter()
	{
		static std::atomic<long long> fallbacks{0};
		return fallbacks;
	}
}

GpuEffect::GpuEffect() = default;
GpuEffect::~GpuEffect() = default;

long long GpuEffect::GpuPasses() { return gpuPassCounter().load(std::memory_order_relaxed); }
long long GpuEffect::CpuFallbacks() { return cpuFallbackCounter().load(std::memory_order_relaxed); }

void GpuEffect::ResetCounters()
{
	gpuPassCounter().store(0, std::memory_order_relaxed);
	cpuFallbackCounter().store(0, std::memory_order_relaxed);
}

const char* GpuEffect::GpuShaderPrelude()
{
	// The shared prelude lives in image-processing-lib/shaders/_prelude.sksl, so the editor
	// concatenates the same bytes before the same fragments; it is embedded here at build time.
	// Every helper in it is explained there, including why each one exists.
	return openshot::shaders::kPrelude;
}

// The frame's pixels on the GPU. A frame that is already GPU-backed — the previous
// effect in the chain, or a text clip — is handed back as it is; a CPU frame pays one
// upload, which is the crossing W22-W25 exists to remove.
std::shared_ptr<GpuFrame> GpuEffect::GpuSourceFrame(std::shared_ptr<openshot::Frame> frame)
{
	if (!frame || !GpuDevice::Instance().available())
		return nullptr;

	if (frame->IsGpuBacked())
		return frame->GpuBacking();

	std::shared_ptr<QImage> image = frame->GetImage();
	if (!image || image->isNull())
		return nullptr;
	// Format_RGBA8888_Premultiplied is byte-for-byte kRGBA_8888 premultiplied, so this
	// needs no conversion and no channel swap — the same reasoning as
	// Frame::FlattenGpuFrame in the other direction.
	if (image->format() != QImage::Format_RGBA8888_Premultiplied)
		return nullptr;
	const SkPixmap pixels(
		SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType,
						  kPremul_SkAlphaType),
		image->constBits(), image->bytesPerLine());
	std::shared_ptr<GpuFrame> staging =
		GpuFrame::Create(image->width(), image->height(), kRGBA_8888_SkColorType);
	if (!staging || !staging->upload(pixels))
		return nullptr;
	return staging;
}

std::shared_ptr<GpuFrame> GpuEffect::RunGpuPass(const std::shared_ptr<GpuFrame>& source,
												int width, int height, int64_t frame_number)
{
	if (!source || width <= 0 || height <= 0)
		return nullptr;
	if (!GpuDevice::Instance().available())
		return nullptr;

	const char* fragment = GpuShaderSource();
	if (!fragment)
		return nullptr;
	if (!programs)
		programs = std::make_shared<ProgramCache>();
	Program& program_ref = programs->for_source(fragment);
	if (!program_ref.compiled) {
		program_ref.compiled = true;
		SkString code(GpuShaderPrelude());
		code.append(fragment);
		auto [effect, error] = SkRuntimeEffect::MakeForShader(code);
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
		program_ref.effect = std::move(effect);
	}
	if (!program_ref.effect)
		return nullptr;

	// A genuine copy, so drawing into a different surface below cannot race it, and
	// the tasks replay in the order they were recorded.
	sk_sp<SkImage> image = source->snapshot();
	if (!image)
		return nullptr;

	std::shared_ptr<GpuFrame> destination =
		GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
	if (!destination)
		return nullptr;

	SkRuntimeEffectBuilder builder(program_ref.effect);

	// Nearest sampling, no local matrix. main()'s coordinate is the destination
	// pixel centre, so with an identity mapping every eval() lands on exactly one
	// texel and osBytes() recovers the source byte exactly. Linear filtering would
	// blend neighbours and no amount of care in the fragment would then match the
	// C++ — this one line is load-bearing for the parity gate.
	builder.child("osSrc") = image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
											   SkSamplingOptions());
	if (!SetGpuUniforms(builder, frame_number, width, height))
		return nullptr;

	sk_sp<SkShader> shader = builder.makeShader();
	if (!shader)
		return nullptr;

	SkPaint paint;
	paint.setShader(std::move(shader));
	// kSrc: this writes every pixel of a pooled surface that still holds whatever
	// its last user drew, so there is nothing to blend with and nothing to clear.
	paint.setBlendMode(SkBlendMode::kSrc);
	SkCanvas* canvas = destination->canvas();
	if (!canvas)
		return nullptr;
	canvas->drawRect(SkRect::MakeIWH(width, height), paint);

	gpuPassCounter().fetch_add(1, std::memory_order_relaxed);
	return destination;
}

bool GpuEffect::ApplyOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
	// Counted on every path so a test can tell "the shader ran" from "the shader
	// declined and the CPU twin produced the same pixels", which look identical
	// from the outside. See GpuPasses().
	const auto declined = []() -> bool {
		cpuFallbackCounter().fetch_add(1, std::memory_order_relaxed);
		return false;
	};

	if (!frame)
		return declined();

	// The one question every GPU path in this fork asks, and false is a normal
	// answer: no GPU, OPENSHOT_GPU=off, or SetBackend(Off) at runtime.
	if (!GpuDevice::Instance().available())
		return declined();

	const int width = frame->GetWidth();
	const int height = frame->GetHeight();

	// `source` has to outlive the draw: it owns the surface the snapshot came from.
	std::shared_ptr<GpuFrame> source = GpuSourceFrame(frame);
	if (!source)
		return declined();

	std::shared_ptr<GpuFrame> destination = RunGpuPass(source, width, height, frame_number);
	if (!destination)
		return declined();

	// The result becomes the frame's pixels without a readback: GetImage() will do
	// that once, whenever the first unported path asks.
	frame->AttachGpuFrame(std::move(destination));
	return true;
}
