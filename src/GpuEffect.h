/**
 * @file
 * @brief Header file for GpuEffect class
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_GPU_EFFECT_H
#define OPENSHOT_GPU_EFFECT_H

#include "EffectBase.h"

#include <memory>

// Skia, forward-declared deliberately. src/CMakeLists.txt installs every *.h
// under src/ wholesale, and ../video-rendering-service compiles the installed
// effect headers with no Skia include path, so a public header may not name a
// Skia type by value. SkRuntimeEffectBuilder is in the global namespace and is
// only ever passed by reference across this boundary — the same arrangement
// Frame.h uses for openshot::GpuFrame. It is the builder's real class name;
// SkRuntimeShaderBuilder is a deprecated alias for it and, being an alias, cannot
// be forward-declared at all.
class SkRuntimeEffectBuilder;

namespace openshot
{
	class Frame;

	/**
	 * @brief Base for an effect whose per-pixel work also exists as one SkSL fragment.
	 *
	 * A derived effect keeps its CPU implementation exactly as it was — that is the
	 * path production runs, and the path a machine with no GPU falls back to — and
	 * adds an SkSL twin of the same arithmetic. GetFrame() offers the frame to
	 * ApplyOnGpu() first and runs its existing CPU code when that declines, which it
	 * does whenever there is no GPU, the shader will not compile, or the effect's
	 * data is not ready.
	 *
	 * ### The fragment is shared with the front end, so it is not free-form
	 *
	 * These fragments are the same source the web editor runs through
	 * `CanvasKit.RuntimeEffect` (decided 2026-09-18; see GPU-DECISIONS.md), which is
	 * what makes editor and export identical by construction rather than by a test
	 * that keeps catching drift. A fragment therefore declares its uniforms and
	 * reads its pixels only through the prelude below, and puts nothing
	 * host-specific in the body.
	 *
	 * ### Frames stay on the GPU between effects
	 *
	 * ApplyOnGpu() leaves its result as the frame's GPU backing rather than reading
	 * it back, so a chain of GPU effects costs one upload at the front and one
	 * readback at the end — the latter happening lazily in Frame::GetImage(),
	 * wherever the first unported path asks for pixels. Per-effect readback would
	 * make a shader slower than the C++ it replaces; the whole benefit is in the
	 * chain.
	 */
	class GpuEffect : public EffectBase
	{
	public:
		~GpuEffect() override;

		/// How many frames have run as a shader, and how many fell back to the CPU
		/// twin, process-wide since the counters were last reset.
		///
		/// This exists because a GPU path with a silent fallback cannot be tested
		/// without it. ApplyOnGpu declining is a normal answer, so a check that only
		/// compares pixels passes just as happily when no shader ever ran — which is
		/// how the first version of openshot-gpu-effect-parity reported a perfect
		/// result for a fragment that had not compiled. `unit.gpu_effect_path` in the
		/// golden suite asserts against these.
		static long long GpuPasses();
		static long long CpuFallbacks();
		static void ResetCounters();

	protected:
		GpuEffect();

		/// This effect's SkSL body: uniform declarations plus
		/// `float4 main(float2 p)`. It is concatenated onto GpuShaderPrelude(),
		/// whose helpers it is expected to use for anything touching bytes or alpha.
		///
		/// An effect that is more than one fragment — Blur is four, and runs one of
		/// them up to six times — returns whichever source the pass it is about to
		/// run needs. Each distinct source is compiled once and cached on its own; the
		/// cache is keyed on the returned pointer, so a fragment must come from a
		/// stable address. Every one of them does: they are the `constexpr char[]`
		/// constants the build embeds from image-processing-lib/shaders/.
		virtual const char* GpuShaderSource() const = 0;

		/// Bind this frame's uniform values. Returning false declines the GPU path
		/// for this frame and the CPU implementation runs instead — an effect whose
		/// LUT or mask has not loaded yet says so here.
		virtual bool SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
									int width, int height) const = 0;

		/// Run GpuShaderSource() over @a frame's pixels, leaving the result as the
		/// frame's GPU backing. False means nothing was touched and the caller must
		/// run its CPU path; that is a normal answer, not an error.
		bool ApplyOnGpu(std::shared_ptr<openshot::Frame> frame, int64_t frame_number);

		/// SkSL prefixed to every fragment: the source shader plus the byte and
		/// alpha helpers that make a fragment bit-identical to its C++ twin.
		///
		/// The CPU effects in image-processing-lib all work the same way on 8-bit
		/// premultiplied RGBA — unpremultiply by truncation, operate, re-premultiply
		/// by truncation — and reproducing *that*, rather than the same arithmetic in
		/// clean floating point, is what keeps the goldens bit-exact on the GPU
		/// instead of needing a tolerance band. The helpers are the parity contract,
		/// so both sides use them and neither open-codes the rounding.
		static const char* GpuShaderPrelude();

	private:
		struct Program;
		struct ProgramCache;
		/// Compiled SkSL, per effect instance, one entry per distinct GpuShaderSource().
		std::shared_ptr<ProgramCache> programs;
	};
}

#endif
