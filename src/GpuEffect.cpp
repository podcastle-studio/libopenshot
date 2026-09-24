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

#include "effects/image-processing-lib/src/Planner/EffectPlan.h"

#include "Frame.h"
#include "ZmqLogger.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"
#include "gpu/GpuTelemetry.h"

#include <atomic>
#include <mutex>
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

namespace
{
	// Does this device's LINEAR filter return exactly (a + b) / 2 at the boundary between two
	// 8-bit texels, for every a and b? blur_pairs.sksl relies on it to read two taps per fetch. The
	// weights are 0.5 and 0.5 there, which every fixed-point filter holds exactly, but not every
	// filter keeps the sum's low bit: NVIDIA does, Mesa's llvmpipe does not (1 LSB). Measured once
	// per device on all 65,536 pairs -- one 512x256 upload, one draw and one readback -- and a device
	// that fails runs plain blur.sksl for those passes, which gives the same bytes either way.
	bool linearMidpointIsExact()
	{
		static std::mutex mutex;
		static unsigned long long measured_generation = 0;
		static bool exact = false;
		const std::lock_guard<std::mutex> lock(mutex);
		const unsigned long long generation = GpuDevice::Generation();
		if (measured_generation == generation + 1)
			return exact;
		exact = false;
		measured_generation = generation + 1;

		// Row b, columns 2a and 2a + 1 hold a and b in every channel.
		std::vector<uint8_t> pairs(512 * 256 * 4);
		for (int b = 0; b < 256; ++b)
			for (int a = 0; a < 256; ++a)
				for (int k = 0; k < 4; ++k) {
					pairs[(b * 512 + 2 * a) * 4 + k] = static_cast<uint8_t>(a);
					pairs[(b * 512 + 2 * a + 1) * 4 + k] = static_cast<uint8_t>(b);
				}
		// Opaque: kRGBA_8888 premultiplied, so colour must not exceed alpha. Put a+b checks in RGB
		// against an alpha of 255 by forcing alpha, and check alpha separately via a == b rows.
		for (int b = 0; b < 256; ++b)
			for (int a = 0; a < 512; ++a)
				pairs[(b * 512 + a) * 4 + 3] = 255;
		std::shared_ptr<GpuFrame> source = GpuFrame::Create(512, 256, kRGBA_8888_SkColorType);
		std::shared_ptr<GpuFrame> result = GpuFrame::Create(256, 256, kRGBA_8888_SkColorType);
		if (!source || !result)
			return false;
		const SkPixmap pixels(SkImageInfo::Make(512, 256, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
							  pairs.data(), 512 * 4);
		if (!source->upload(pixels))
			return false;
		sk_sp<SkImage> image = source->snapshot();
		if (!image)
			return false;
		// out = (sample * 510 - (a + b)) + 128 -- 128 when exact.
		static const char* kProbe =
			"uniform shader src;\n"
			"half4 main(float2 p) {\n"
			"  float2 q = floor(p);\n"
			"  float s = float(src.eval(float2(2.0 * q.x + 1.0, q.y + 0.5)).r);\n"
			"  float d = floor(s * 510.0 + 0.5) - (q.x + q.y);\n"
			"  return half4(half((d + 128.0) / 255.0), 0.0, 0.0, 1.0);\n"
			"}\n";
		auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(kProbe));
		if (!effect)
			return false;
		SkRuntimeEffectBuilder builder(effect);
		builder.child("src") = image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
												 SkSamplingOptions(SkFilterMode::kLinear));
		sk_sp<SkShader> shader = builder.makeShader();
		SkCanvas* canvas = result->canvas();
		if (!shader || !canvas)
			return false;
		SkPaint paint;
		paint.setShader(std::move(shader));
		paint.setBlendMode(SkBlendMode::kSrc);
		canvas->drawRect(SkRect::MakeIWH(256, 256), paint);
		std::vector<uint8_t> out(256 * 256 * 4);
		const SkPixmap readback(SkImageInfo::Make(256, 256, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
								out.data(), 256 * 4);
		if (!result->readback(readback, /*count=*/false))   // a probe, not a frame
			return false;
		for (int i = 0; i < 256 * 256; ++i)
			if (out[i * 4] != 128)
				return false;
		exact = true;
		return true;
	}
}

struct GpuEffect::HostSourceCache
{
	qint64 key = 0;
	unsigned long long generation = 0;
	std::shared_ptr<GpuFrame> staging;
};

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
	// A still hands over the same QImage every frame: upload it once. The staging surface is only
	// ever read (every pass draws into a new one), so it can be handed out again.
	const unsigned long long generation = GpuDevice::Generation();
	if (host_source && host_source->staging && host_source->key == image->cacheKey() &&
		host_source->generation == generation && host_source->staging->ownedByThisThread()) {
		GpuCounters::Add(GpuCounters::UploadCached);
		return host_source->staging;
	}
	const SkPixmap pixels(
		SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType,
						  kPremul_SkAlphaType),
		image->constBits(), image->bytesPerLine());
	std::shared_ptr<GpuFrame> staging =
		GpuFrame::Create(image->width(), image->height(), kRGBA_8888_SkColorType);
	if (!staging || !staging->upload(pixels))
		return nullptr;
	auto cache = std::make_shared<HostSourceCache>();
	cache->key = image->cacheKey();
	cache->generation = generation;
	cache->staging = staging;
	host_source = std::move(cache);
	return staging;
}

std::shared_ptr<GpuFrame> GpuEffect::RunGpuPass(const std::shared_ptr<GpuFrame>& source,
												int width, int height, int64_t frame_number)
{
	if (!source || width <= 0 || height <= 0)
		return nullptr;
	if (!GpuDevice::Instance().available())
		return nullptr;

	// Inside a planned step (RunPlannedStep) the pass names its own fragment and uniforms, so an
	// effect with a multi-pass plan needs no override of its own to run it.
	const char* fragment = planned_pass ? PlannedShaderSource(GpuShaderSource()) : GpuShaderSource();
	// A pass that reads texel pairs through the linear filter, on a device whose filter does not
	// keep their sum exact: the plain twin, same uniforms, same bytes (see linearMidpointIsExact).
	bool linear = planned_pass && planned_pass->linearSource;
	if (linear && planned_pass->shader == "blur_pairs" && !linearMidpointIsExact()) {
		fragment = openshot::shaders::kBlur;
		linear = false;
	}
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
	// ...except a pass the planner marks linearSource (blur_pairs), which reads two texels per fetch
	// on their shared boundary; its texel-centre reads are still exact under linear filtering.
	builder.child("osSrc") = image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
											   linear ? SkSamplingOptions(SkFilterMode::kLinear)
													  : SkSamplingOptions());
	if (planned_pass ? !BindPlannedPass(builder) : !SetGpuUniforms(builder, frame_number, width, height))
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

	// An identity or a clear is not a pass, and BindPlan declines both -- but declining sends the
	// C++ twin a GPU frame, which it reads back for a no-op or a memset. SplitShift at rest did
	// that on every frame of both clips of a SPLIT transition. Neither needs the pixels.
	Podcastle::Effects::EffectPlan plan;
	if (PlanForFrame(frame_number, width, height, plan) && plan.steps.size() == 1) {
		const auto kind = plan.steps.front().kind;
		if (kind == Podcastle::Effects::PlanStep::Kind::Identity)
			return true;   // the frame, untouched, is exactly what the C++ returns
		if (kind == Podcastle::Effects::PlanStep::Kind::Clear && frame->IsGpuBacked()) {
			// A fresh surface rather than clearing the backing in place: a GPU backing can be
			// shared with a cached frame (FrameMapper, Frame's copy constructor). All-zero
			// premultiplied pixels are what the C++'s memset writes. A host frame keeps its
			// memset -- there is no readback to save there.
			if (std::shared_ptr<GpuFrame> cleared = GpuFrame::Create(width, height, kRGBA_8888_SkColorType)) {
				if (SkCanvas* canvas = cleared->canvas()) {
					canvas->clear(SK_ColorTRANSPARENT);
					frame->AttachGpuFrame(std::move(cleared));
					return true;
				}
			}
		}
		// A GPU step of several passes, or of one whose size is not the frame's (a resize, a
		// zoom-out that pads to a new size): run the chain, which attaches its last pass's
		// output -- at that pass's size, as the C++ assigns a resized image back.
		if (kind == Podcastle::Effects::PlanStep::Kind::Gpu) {
			const Podcastle::Effects::PlanStep& step = plan.steps.front();
			const bool plain = step.passes.size() == 1 && step.passes.front().width == width &&
							   step.passes.front().height == height;
			if (!plain)
				return RunPlannedStep(frame, frame_number, step) ? true : declined();
		}
	}

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

namespace
{
	bool bindUniforms(SkRuntimeEffectBuilder& builder, const Podcastle::Effects::PlanPass& pass)
	{
		for (const Podcastle::Effects::PlanUniform& u : pass.uniforms) {
			// set() checks the byte size against the declaration, so a planner/fragment mismatch
			// declines here rather than drawing with a half-bound uniform.
			if (!builder.uniform(u.name.c_str()).set(u.values.data(), static_cast<int>(u.values.size())))
				return false;
		}
		return true;
	}
}

bool GpuEffect::PlanForFrame(int64_t, int, int, Podcastle::Effects::EffectPlan&) const
{
	return false;
}

bool GpuEffect::BindPlan(SkRuntimeEffectBuilder& builder,
						 const Podcastle::Effects::EffectPlan& plan) const
{
	if (plan.steps.size() != 1)
		return false;
	const Podcastle::Effects::PlanStep& step = plan.steps.front();
	if (step.kind != Podcastle::Effects::PlanStep::Kind::Gpu || step.passes.size() != 1)
		return false;
	return bindUniforms(builder, step.passes.front());
}

bool GpuEffect::RunPlannedStep(std::shared_ptr<openshot::Frame> frame, int64_t frame_number,
							   const Podcastle::Effects::PlanStep& step)
{
	if (!frame || step.kind != Podcastle::Effects::PlanStep::Kind::Gpu || step.passes.empty())
		return false;
	if (!GpuDevice::Instance().available())
		return false;
	std::shared_ptr<GpuFrame> current = GpuSourceFrame(frame);
	if (!current)
		return false;
	for (const Podcastle::Effects::PlanPass& pass : step.passes) {
		planned_pass = &pass;
		current = RunGpuPass(current, pass.width, pass.height, frame_number);
		planned_pass = nullptr;
		if (!current)
			return false;
	}
	frame->AttachGpuFrame(std::move(current));
	return true;
}

const char* GpuEffect::PlannedShaderSource(const char* fallback) const
{
	if (!planned_pass)
		return fallback;
	const char* source = openshot::shaders::ByName(planned_pass->shader.c_str());
	return source ? source : fallback;
}

bool GpuEffect::BindPlannedPass(SkRuntimeEffectBuilder& builder) const
{
	return planned_pass && bindUniforms(builder, *planned_pass);
}
