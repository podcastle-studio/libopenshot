#pragma once

// Decoded YUV planes -> an RGBA GPU frame, in one SkSL pass that also does the
// reader's pre-scale.
//
// This is the conversion the reader used to hand to swscale. On the CPU that is
// 95 % of the reader's wall clock (4.6 ms a 4K frame even with the buffers
// pooled) and it ends with a full-size RGBA buffer in host memory that the
// compositor then has to push back to the GPU. Done here instead, the only thing
// that crosses the bus is the YUV itself — 1.5 bytes a pixel against 4 — and the
// frame arrives where the compositor already wants it.
//
// Off unless GpuDevice::Instance().available(); Convert() returning null is the
// normal answer and the caller falls back to swscale.

#include <cstdint>
#include <memory>

#include "skia/include/core/SkRefCnt.h"

class SkCanvas;
class SkImage;

namespace openshot
{
	class GpuFrame;
	class GpuImage;

	/// Decoded YUV planes, as FFmpeg hands them over: a pointer, a stride and the
	/// plane's own dimensions.
	struct GpuYuvPlane
	{
		const std::uint8_t* data = nullptr;
		int stride = 0;
		int width = 0;
		int height = 0;
	};

	/// Turns decoded YUV into an RGBA GPU frame.
	class GpuYuv
	{
	public:
		/// How the chroma is laid out. These are the two 8-bit 4:2:0 layouts the
		/// decoders in this fork produce: planar from libavcodec's software H.264,
		/// interleaved from NVDEC.
		enum class Layout
		{
			I420,   ///< three planes, Y then U then V
			NV12,   ///< two planes, Y then interleaved UV
		};

		/// Which matrix takes Y'CbCr to R'G'B'.
		enum class Matrix
		{
			BT601,    ///< SD, and swscale's default when a stream says nothing
			BT709,    ///< HD
			BT2020,   ///< UHD wide gamut, non-constant luminance
		};

		/// Is this pixel format one Convert() handles? Pass an @c AVPixelFormat.
		/// Keeps the FFmpeg enum out of this header while letting the reader ask.
		static bool Supports(int av_pixel_format);

		/// The layout and range of a supported @c AVPixelFormat. Undefined for one
		/// Supports() rejects.
		static Layout LayoutOf(int av_pixel_format);
		static bool IsFullRange(int av_pixel_format);

		/// The matrix for an @c AVColorSpace, falling back to what swscale would
		/// use when the stream declares nothing.
		static Matrix MatrixOf(int av_color_space, int width, int height);

		/// Convert @a planes into a new GPU frame of @a out_width x @a out_height,
		/// scaling in the same pass. Null when the GPU is unavailable, the layout
		/// is unsupported, or anything failed — all of which mean "use swscale".
		///
		/// The frame belongs to the calling thread's recorder, as every Graphite
		/// surface does.
		static std::shared_ptr<GpuFrame> Convert(Layout layout, Matrix matrix, bool full_range,
												 const GpuYuvPlane* planes, int plane_count,
												 int out_width, int out_height);

		/// Convert NVDEC's two planes where they already are -- the images
		/// CudaInterop::copyNV12 wrote -- so nothing crosses the bus at all. Same
		/// conversion and pre-scale as the overload above, bit for bit: only where
		/// the planes come from differs. @a luma must be R8 and @a chroma R8G8.
		///
		/// Records the drawing and returns; it does not submit. The caller submits
		/// waiting on CudaInterop::waitSemaphore(luma), then calls prepareForCopy().
		static std::shared_ptr<GpuFrame> Convert(const GpuImage& luma, const GpuImage& chroma,
												 Matrix matrix, bool full_range,
												 int out_width, int out_height);

		/// The other direction, for the encoder (W25): draw @a rgba (width x
		/// height, premultiplied, as the compositor leaves it) into @a packed as
		/// NV12 laid out the way FFmpeg lays it out -- Y in rows [0, height), then
		/// interleaved UV in rows [height, height + height/2) -- on a single R8
		/// surface of width x (height + height/2). Limited range. Chroma is the
		/// 2x2 box average of the four pixels it covers, which is swscale's
		/// RGB->YUV420 with SWS_FAST_BILINEAR to within a code value.
		static bool EncodeNV12(const sk_sp<SkImage>& rgba, SkCanvas* packed, int width,
							   int height, Matrix matrix);

		/// How many frames EncodeNV12 has drawn. The harness asserts on it.
		static unsigned long long Encodes();

		/// How many frames have been converted here. The golden harness reads this
		/// to know whether a scenario actually went through the GPU: a scenario
		/// whose pixels a GPU produced cannot be held to CPU goldens bit-for-bit,
		/// and a check that cannot tell the two apart proves nothing.
		static unsigned long long Conversions();

		/// How many of those also had to scale. Kept apart because the two differ
		/// from the CPU by different amounts: the conversion is faithful (46 dB,
		/// 3 LSB at worst, measured), while the scale is a genuinely different
		/// filter from swscale's SWS_FAST_BILINEAR, which carries a half-pixel
		/// phase -- and on colour bars that is a whole column of wrong pixels at
		/// every edge. The harness picks its tolerance on this.
		static unsigned long long ScaledConversions();

		/// How many of those came straight from device images (the NVDEC overload),
		/// so a check can tell the zero-copy path from the host-plane one.
		static unsigned long long DeviceConversions();
	};
}
