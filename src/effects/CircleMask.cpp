/**
 * @file
 * @brief Source file for Mask class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "CircleMask.h"

#include "../gpu/GpuDevice.h"
#include "../gpu/GpuFrame.h"

#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkShader.h"
#include "skia/include/core/SkTileMode.h"
#include "skia/include/effects/SkRuntimeEffect.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>
#include "./image-processing-lib/src/Effects/effects.h"

#include "Exceptions.h"
#include "FFmpegReader.h"


using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
CircleMask::CircleMask() {
	// Init effect properties
	init_effect_details();
}

// Default constructor
CircleMask::CircleMask(Keyframe _circleRadius) : circleRadius(_circleRadius) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void CircleMask::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "CircleMask";
	info.name = "Alpha Circle Mask ";
	info.description = "Uses a grayscale mask image to gradually wipe / transition between 2 images.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a modified openshot::Frame object
std::shared_ptr<openshot::Frame> CircleMask::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
	const auto circleRadiusValue = circleRadius.GetValue(frame_number);
	if (circleRadiusValue >= 1) {
        return frame;
    }
	// The shader when there is a GPU to run it on, OpenCV otherwise. Before GetImageCV(), which
	// on a GPU-backed frame is a readback and two full-frame cv::Mat conversions.
	if (ApplyOnGpu(frame, frame_number))
		return frame;

	auto imageCv = frame->GetImageCV();
	// openshot::Frame images are premultiplied (see Frame::Mat2Qimage), so the mask has to scale
	// every channel by its coverage -- scaling alpha alone leaves a bright fringe on the edge.
	Podcastle::Effects::applyCircleMaskEffect(imageCv, circleRadiusValue, /*premultipliedAlpha=*/true);
	frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// The rasterised circle, cached across frames. See CircleMask.h.
struct CircleMask::CoverageCache
{
	sk_sp<SkImage> texture;
	double radius = -1.0;
	int width = 0;
	int height = 0;
	unsigned long long generation = 0;
	std::shared_ptr<GpuFrame> owner;   // keeps the pooled surface alive alongside its snapshot
};

// The SkSL twin of applyCircleMaskEffect -- of its per-pixel half only.
//
// **OpenCV rasterises the circle and the GPU never does.** cv::circle with LINE_AA does not draw a
// circle at all: it fills a polygon approximation with OpenCV's own scanline coverage, at 1/8-pixel
// precision via the shift argument. An analytic disc in SkSL would be a *better* circle and a worse
// port -- the edge ring is where the whole effect lives, and a different rasteriser there is a
// visible difference on every frame, not a rounding one.
//
// So the mask is built by the same OpenCV call as before, cached (it depends only on the radius and
// the frame size, and a still radius is the common case), and uploaded as a texture. Exactly the
// arrangement Mask uses for its matte and LightAdjustment for its tone curve: the CPU keeps
// ownership of anything a rasteriser decides, and the fragment does the arithmetic.
const char* CircleMask::GpuShaderSource() const
{
	return R"SKSL(
uniform shader coverage;   // 8-bit, frame-sized, 255 inside / 0 outside / partial on the edge

float4 main(float2 p) {
	float4 bytes = osBytes(p);
	float c = floor(float4(coverage.eval(p)).r * 255.0 + 0.5);
	// (channel * coverage + 127) / 255, integer division, on every channel including alpha
	// because the frame is premultiplied -- scaling alpha alone leaves a bright fringe.
	float4 n = bytes * c + 127.0;
	return float4(osIDiv(n.r, 255.0), osIDiv(n.g, 255.0),
				  osIDiv(n.b, 255.0), osIDiv(n.a, 255.0)) / 255.0;
}
)SKSL";
}

bool CircleMask::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
								int width, int height) const
{
	const double radius_value = circleRadius.GetValue(frame_number);
	// >= 1 never reaches here (GetFrame returns early); <= 0 zeroes the whole frame, which the
	// C++ does with a Mat::zeros rather than through the coverage path.
	if (radius_value >= 1.0 || radius_value <= 0.0)
		return false;

	const unsigned long long generation = GpuDevice::Generation();
	if (!coverage || coverage->radius != radius_value || coverage->width != width ||
		coverage->height != height || coverage->generation != generation || !coverage->texture) {
		auto cache = std::make_shared<CoverageCache>();
		cache->radius = radius_value;
		cache->width = width;
		cache->height = height;
		cache->generation = generation;

		// The same call, with the same sub-pixel precision, as applyCircleMaskEffect.
		const double max_radius =
			std::sqrt(static_cast<double>(width) * width +
					  static_cast<double>(height) * height) / 2.0;
		constexpr int kSubPixelBits = 3;
		constexpr int kSubPixelScale = 1 << kSubPixelBits;
		cv::Mat mask = cv::Mat::zeros(height, width, CV_8UC1);
		cv::circle(mask,
				   cv::Point(cvRound((width / 2) * kSubPixelScale),
							 cvRound((height / 2) * kSubPixelScale)),
				   cvRound(radius_value * max_radius * kSubPixelScale),
				   cv::Scalar(255), -1, cv::LINE_AA, kSubPixelBits);

		// Upload as RGBA with the coverage in R; a single-channel GPU surface would work too but
		// the pool speaks kRGBA_8888 and this is 256 KB at 1080p, built once.
		std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
		for (int y = 0; y < height; ++y) {
			const uint8_t* row = mask.ptr<uint8_t>(y);
			for (int x = 0; x < width; ++x) {
				const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
				rgba[i + 0] = row[x];
				rgba[i + 3] = 255;
			}
		}
		const SkPixmap pixels(
			SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
			rgba.data(), static_cast<std::size_t>(width) * 4);

		cache->owner = GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
		if (!cache->owner || !cache->owner->upload(pixels))
			return false;
		cache->texture = cache->owner->snapshot();
		if (!cache->texture)
			return false;
		coverage = std::move(cache);
	}

	builder.child("coverage") = coverage->texture->makeShader(
		SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions());
	return true;
}

// Generate JSON string of this object
std::string CircleMask::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value CircleMask::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["circleRadius"] = circleRadius.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void CircleMask::SetJson(const std::string value) {

	// Parse JSON string into JSON objects
	try
	{
		const Json::Value root = openshot::stringToJson(value);
		// Set all values that match
		SetJsonValue(root);
	}
	catch (const std::exception& e)
	{
		// Error parsing JSON (or missing keys)
		throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
	}
}

// Load Json::Value into this object
void CircleMask::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["circleRadius"].isNull())
		circleRadius.SetJsonValue(root["circleRadius"]);
}

// Get all properties for a specific frame
std::string CircleMask::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["circleRadius"] = add_property_json("circleMask", circleRadius.GetValue(requested_frame), "float", "", &circleRadius, 0, 1.0, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
