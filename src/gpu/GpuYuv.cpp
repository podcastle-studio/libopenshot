// Decoded YUV planes -> an RGBA GPU frame. See GpuYuv.h.

#include "GpuYuv.h"

#include "CudaInterop.h"
#include "GpuDevice.h"
#include "GpuFrame.h"

#include <atomic>
#include <cstring>

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkRect.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkShader.h"
#include "skia/include/core/SkString.h"
#include "skia/include/core/SkTileMode.h"
#include "skia/include/effects/SkRuntimeEffect.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

using namespace openshot;

namespace
{
	std::atomic<unsigned long long>& conversionCount()
	{
		static std::atomic<unsigned long long> count{0};
		return count;
	}

	std::atomic<unsigned long long>& scaledCount()
	{
		static std::atomic<unsigned long long> count{0};
		return count;
	}

	std::atomic<unsigned long long>& deviceCount()
	{
		static std::atomic<unsigned long long> count{0};
		return count;
	}

	// One shader for both layouts. The branch is on a uniform, so it costs nothing
	// that matters and it keeps a single compiled effect in the cache; splitting it
	// in two would double the pipeline warm-up for no measurable gain.
	//
	// Deliberately within the GLSL ES 1.00 intrinsic set that SkSL targets (no
	// round, no trunc) so the same source would compile in CanvasKit, which is the
	// standing rule for shaders in this fork.
	const char* kYuvToRgba = R"(
uniform shader luma;
uniform shader chromaA;   // I420: the U plane. NV12: the interleaved UV plane.
uniform shader chromaB;   // I420: the V plane. NV12: unused, bound to chromaA.
uniform half4 rowR;       // (y, u, v, offset), already carrying the range shift
uniform half4 rowG;
uniform half4 rowB;
uniform float2 lumaStep;    // destination pixel -> luma texel
uniform float2 chromaStep;  // destination pixel -> chroma texel
uniform int interleaved;    // 1 when chromaA holds both channels

half4 main(float2 p) {
    half y = luma.eval(p * lumaStep).r;
    float2 c = p * chromaStep;
    half u;
    half v;
    if (interleaved == 1) {
        half2 both = chromaA.eval(c).rg;
        u = both.r;
        v = both.g;
    } else {
        u = chromaA.eval(c).r;
        v = chromaB.eval(c).r;
    }
    half3 yuv = half3(y, u, v);
    half3 rgb = half3(dot(rowR.xyz, yuv) + rowR.w,
                      dot(rowG.xyz, yuv) + rowG.w,
                      dot(rowB.xyz, yuv) + rowB.w);
    rgb = clamp(rgb, 0.0, 1.0);
    return half4(rgb, 1.0);   // opaque, so premultiplied and straight agree
}
)";

	sk_sp<SkRuntimeEffect> yuvEffect()
	{
		// Built once. SkRuntimeEffect is immutable and refcounted, and Skia caches
		// the compiled pipeline per Context, so this is only the SkSL parse.
		static SkRuntimeEffect::Result result =
			SkRuntimeEffect::MakeForShader(SkString(kYuvToRgba));
		return result.effect;
	}

	/// The Y'CbCr -> R'G'B' rows, with the range shift folded into the offset.
	///
	/// Limited range is the 16..235 / 16..240 encoding every camera and every
	/// H.264 stream uses unless it says otherwise; full range is the JPEG one that
	/// the YUVJ formats and `color_range = AVCOL_RANGE_JPEG` mean.
	struct Rows
	{
		float r[4];
		float g[4];
		float b[4];
	};

	Rows matrixRows(GpuYuv::Matrix matrix, bool full_range)
	{
		// Kr and Kb; Kg follows. These are the defining constants of each matrix.
		float kr = 0.299f, kb = 0.114f;
		switch (matrix) {
		case GpuYuv::Matrix::BT709:
			kr = 0.2126f;
			kb = 0.0722f;
			break;
		case GpuYuv::Matrix::BT2020:
			kr = 0.2627f;
			kb = 0.0593f;
			break;
		case GpuYuv::Matrix::BT601:
			break;
		}
		const float kg = 1.0f - kr - kb;

		// R = Y + 2(1-Kr)Cr, B = Y + 2(1-Kb)Cb, G from the luma identity.
		const float cr_r = 2.0f * (1.0f - kr);
		const float cb_b = 2.0f * (1.0f - kb);
		const float cb_g = -2.0f * (1.0f - kb) * kb / kg;
		const float cr_g = -2.0f * (1.0f - kr) * kr / kg;

		// Limited range stores Y in 16..235 and chroma in 16..240 of 0..255, so it
		// is scaled up before the matrix; full range is already 0..1.
		const float y_scale = full_range ? 1.0f : 255.0f / 219.0f;
		const float c_scale = full_range ? 1.0f : 255.0f / 224.0f;
		const float y_shift = full_range ? 0.0f : 16.0f / 255.0f;

		// Neutral chroma is code 128 in both ranges, which a UNORM texture samples as
		// 128/255 -- not 0.5. Taking it as 0.5 put R and B about 0.9 of a code value
		// high on every pixel, all of the "3 LSB against swscale" this pass used to
		// measure; found by the BT.709 chart (unit.bt709_chart), where it was the
		// difference between 2 and 3.
		const float chroma_zero = 128.0f / 255.0f;

		Rows rows{};
		const auto row = [&](float cb, float cr, float* out) {
			out[0] = y_scale;
			out[1] = cb * c_scale;
			out[2] = cr * c_scale;
			// The offset undoes the black level and the chroma bias.
			out[3] = -(y_scale * y_shift) - chroma_zero * (out[1] + out[2]);
		};
		row(0.0f, cr_r, rows.r);
		row(cb_g, cr_g, rows.g);
		row(cb_b, 0.0f, rows.b);
		return rows;
	}

	/// A plane as a GPU-backed single- or two-channel image.
	///
	/// RasterFromPixmapCopy rather than a borrowed pixmap on purpose: the recording
	/// is replayed when the frame is submitted, by which time the decoder may have
	/// handed this AVFrame's buffer back. That is the same reason GpuFrame::upload
	/// copies.
	sk_sp<SkImage> planeTexture(const GpuYuvPlane& plane, SkColorType color_type)
	{
		if (!plane.data || plane.width <= 0 || plane.height <= 0)
			return nullptr;
		const SkImageInfo info =
			SkImageInfo::Make(plane.width, plane.height, color_type, kOpaque_SkAlphaType);
		const SkPixmap pixmap(info, plane.data, static_cast<std::size_t>(plane.stride));
		sk_sp<SkImage> raster = SkImages::RasterFromPixmapCopy(pixmap);
		if (!raster)
			return nullptr;
		// Graphite never uploads a raster image used as a shader for you; it drops
		// the draw instead. Everything crossing onto a GPU surface goes through here.
		return GpuFrame::ToTexture(raster);
	}
}

bool GpuYuv::Supports(int av_pixel_format)
{
	switch (av_pixel_format) {
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
	case AV_PIX_FMT_NV12:
		return true;
	default:
		return false;
	}
}

GpuYuv::Layout GpuYuv::LayoutOf(int av_pixel_format)
{
	return av_pixel_format == AV_PIX_FMT_NV12 ? Layout::NV12 : Layout::I420;
}

bool GpuYuv::IsFullRange(int av_pixel_format)
{
	return av_pixel_format == AV_PIX_FMT_YUVJ420P;
}

GpuYuv::Matrix GpuYuv::MatrixOf(int av_color_space, int width, int height)
{
	switch (av_color_space) {
	case AVCOL_SPC_BT709:
		return Matrix::BT709;
	case AVCOL_SPC_BT2020_NCL:
	case AVCOL_SPC_BT2020_CL:
		return Matrix::BT2020;
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M:
		return Matrix::BT601;
	default:
		break;
	}
	// Undeclared. swscale picks BT.601 whatever the resolution, and matching it is
	// what keeps this path a speed change rather than a colour change. W23's
	// "default BT.709 at >= 720p" is a deliberate, separate decision: it moves
	// every golden that decodes video, so it is not smuggled in here.
	(void)width;
	(void)height;
	return Matrix::BT601;
}

unsigned long long GpuYuv::Conversions()
{
	return conversionCount().load();
}

unsigned long long GpuYuv::ScaledConversions()
{
	return scaledCount().load();
}

unsigned long long GpuYuv::DeviceConversions()
{
	return deviceCount().load();
}

namespace
{
// Both Convert() overloads end here: the planes are textures by now, wherever they came from.
std::shared_ptr<GpuFrame> convertImages(GpuYuv::Layout layout, GpuYuv::Matrix matrix,
										bool full_range, const sk_sp<SkImage>& luma,
										const sk_sp<SkImage>& chroma_a,
										const sk_sp<SkImage>& chroma_b, int luma_width,
										int luma_height, int chroma_width, int chroma_height,
										int out_width, int out_height)
{
	using Layout = GpuYuv::Layout;
	if (!luma || !chroma_a || !chroma_b || luma_width <= 0 || luma_height <= 0)
		return nullptr;

	sk_sp<SkRuntimeEffect> effect = yuvEffect();
	if (!effect)
		return nullptr;

	// Convert at the source's own resolution, and pre-filter afterwards if the reader asked for a
	// much smaller frame. One pass cannot do both: a single bilinear tap is not a downscale
	// filter, and folding the pre-scale into the sample measured 29 dB against the CPU on a 2x
	// reduction -- aliasing, not a colour difference.
	//
	// The reduction is done as box HALVINGS first and then, only if anything is left over, one
	// fractional step to the exact size the reader asked for.
	//
	// Halving is what a downscale actually needs: bilinear at exactly one half averages a 2x2
	// block, which is an exact box prefilter, and it is phase-exact so it cannot shift the
	// picture the way a fractional resample can. Doing the whole reduction in one fractional draw
	// instead measured 7 % slower end to end at 4K -> 1080p (88-90 fps against 94-97).
	//
	// The exact step is not optional, and that was measured too: **the frame's pixel size is part
	// of the contract downstream.** A SCALE_NONE clip is drawn at its own size, so a frame that
	// is merely "close enough and the compositor will scale it" changes the picture -- stopping
	// at the halving cost 4.5 dB on readers.video_b_24fps_prescale. At a power-of-two ratio the
	// halvings land exactly on the target and this step disappears, which is the common case.
	int scale_steps = 0;
	for (int w = luma_width, h = luma_height;
		 w / 2 >= out_width && h / 2 >= out_height && scale_steps < 4; w /= 2, h /= 2)
		++scale_steps;
	const bool scaling = scale_steps > 0 || out_width != luma_width ||
						 out_height != luma_height;
	std::shared_ptr<GpuFrame> frame =
		GpuFrame::Create(luma_width, luma_height);
	if (!frame)
		return nullptr;

	// Luma linear, chroma nearest -- and the chroma choice is the one that matters.
	//
	// Linear luma is exact at 1:1 (the sample lands on the texel centre) and is the bilinear the
	// rest of this path already uses when the reader pre-scales. Chroma is a different question:
	// swscale's unscaled converter *replicates* each 4:2:0 chroma sample over its 2x2 luma block,
	// and interpolating instead puts a two-pixel transition band at every colour edge. On colour
	// bars that is 25 % of the frame past 2 LSB and a peak error of 198 -- measured, before this
	// line said kNearest. Interpolated chroma is arguably the better picture, but it is a
	// different conversion, and this path is meant to be a speed change.
	const SkSamplingOptions luma_sampling(SkFilterMode::kLinear, SkMipmapMode::kNone);
	const SkSamplingOptions chroma_sampling(SkFilterMode::kNearest, SkMipmapMode::kNone);
	SkRuntimeShaderBuilder builder(effect);
	builder.child("luma") =
		luma->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, luma_sampling);
	builder.child("chromaA") =
		chroma_a->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, chroma_sampling);
	builder.child("chromaB") =
		chroma_b->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, chroma_sampling);

	const Rows rows = matrixRows(matrix, full_range);
	builder.uniform("rowR") = SkV4{rows.r[0], rows.r[1], rows.r[2], rows.r[3]};
	builder.uniform("rowG") = SkV4{rows.g[0], rows.g[1], rows.g[2], rows.g[3]};
	builder.uniform("rowB") = SkV4{rows.b[0], rows.b[1], rows.b[2], rows.b[3]};
	builder.uniform("lumaStep") = SkV2{1.0f, 1.0f};
	builder.uniform("chromaStep") =
		SkV2{static_cast<float>(chroma_width) / static_cast<float>(luma_width),
			 static_cast<float>(chroma_height) / static_cast<float>(luma_height)};
	builder.uniform("interleaved") = layout == Layout::NV12 ? 1 : 0;

	sk_sp<SkShader> shader = builder.makeShader();
	if (!shader)
		return nullptr;

	SkPaint paint;
	paint.setShader(shader);
	// kSrc, not kSrcOver: a pooled surface still holds the previous frame.
	paint.setBlendMode(SkBlendMode::kSrc);
	frame->canvas()->drawPaint(paint);

	int width = luma_width;
	int height = luma_height;
	for (int step = 0; step < scale_steps; ++step) {
		sk_sp<SkImage> source = frame->snapshot();
		const int half_width = width / 2;
		const int half_height = height / 2;
		std::shared_ptr<GpuFrame> halved = GpuFrame::Create(half_width, half_height);
		if (!source || !halved)
			return nullptr;
		SkPaint blit;
		blit.setBlendMode(SkBlendMode::kSrc);
		// Bilinear at exactly one half lands each destination sample in the middle of a 2x2
		// source block and weights all four equally: an exact box average, and the cheapest
		// correct downscale there is. Mitchell would be wasted work here.
		halved->canvas()->drawImageRect(
			source, SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
			SkRect::MakeWH(static_cast<float>(half_width), static_cast<float>(half_height)),
			SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone), &blit,
			SkCanvas::kFast_SrcRectConstraint);
		frame = std::move(halved);
		width = half_width;
		height = half_height;
	}

	if (width != out_width || height != out_height) {
		sk_sp<SkImage> source = frame->snapshot();
		std::shared_ptr<GpuFrame> exact = GpuFrame::Create(out_width, out_height);
		if (!source || !exact)
			return nullptr;
		SkPaint blit;
		blit.setBlendMode(SkBlendMode::kSrc);
		// Linear is enough here: the halvings above have already brought the source to within a
		// factor of two of the target, which is exactly the range one bilinear tap covers.
		exact->canvas()->drawImageRect(
			source, SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
			SkRect::MakeWH(static_cast<float>(out_width), static_cast<float>(out_height)),
			SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone), &blit,
			SkCanvas::kFast_SrcRectConstraint);
		frame = std::move(exact);
	}

	if (scaling)
		scaledCount()++;

	conversionCount()++;
	return frame;
}
}   // namespace

std::shared_ptr<GpuFrame> GpuYuv::Convert(Layout layout, Matrix matrix, bool full_range,
										  const GpuYuvPlane* planes, int plane_count,
										  int out_width, int out_height)
{
	if (!planes || out_width <= 0 || out_height <= 0)
		return nullptr;
	const int wanted_planes = layout == Layout::NV12 ? 2 : 3;
	if (plane_count < wanted_planes)
		return nullptr;
	if (!GpuDevice::Instance().available())
		return nullptr;

	const GpuYuvPlane& luma_plane = planes[0];
	if (luma_plane.width <= 0 || luma_plane.height <= 0)
		return nullptr;

	sk_sp<SkImage> luma = planeTexture(luma_plane, kR8_unorm_SkColorType);
	sk_sp<SkImage> chroma_a = planeTexture(
		planes[1], layout == Layout::NV12 ? kR8G8_unorm_SkColorType : kR8_unorm_SkColorType);
	sk_sp<SkImage> chroma_b =
		layout == Layout::NV12 ? chroma_a : planeTexture(planes[2], kR8_unorm_SkColorType);
	return convertImages(layout, matrix, full_range, luma, chroma_a, chroma_b, luma_plane.width,
						 luma_plane.height, planes[1].width, planes[1].height, out_width,
						 out_height);
}

std::shared_ptr<GpuFrame> GpuYuv::Convert(const GpuImage& luma, const GpuImage& chroma,
										  Matrix matrix, bool full_range, int out_width,
										  int out_height)
{
	if (out_width <= 0 || out_height <= 0 || !GpuDevice::Instance().available())
		return nullptr;
	if (luma.format() != GpuImage::Format::R8 || chroma.format() != GpuImage::Format::R8G8)
		return nullptr;
	// Fresh wrappers, used for this frame only: see GpuImage::image().
	sk_sp<SkImage> luma_image = luma.image();
	sk_sp<SkImage> chroma_image = chroma.image();
	std::shared_ptr<GpuFrame> frame =
		convertImages(Layout::NV12, matrix, full_range, luma_image, chroma_image, chroma_image,
					  luma.width(), luma.height(), chroma.width(), chroma.height(), out_width,
					  out_height);
	if (frame)
		deviceCount()++;
	return frame;
}

