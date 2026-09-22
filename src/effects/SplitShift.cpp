/**
 * @file
 * @brief Source file for Shift effect class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "SplitShift.h"

#include "skia/include/core/SkM44.h"
#include "skia/include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <cmath>
#include "Exceptions.h"
#include "./image-processing-lib/src/Effects/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
SplitShift::SplitShift() : shiftAmount(0.0), splitPoint(0.5) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
SplitShift::SplitShift(Keyframe newShiftAmount, bool _isHorizontal, Keyframe newSplitPoint)
    : splitPoint(newSplitPoint), isHorizontal(_isHorizontal), shiftAmount(newShiftAmount)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void SplitShift::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "SplitShift";
	info.name = "SplitShift";
	info.description = "Vertically or Horizontally Shift the image up<->down or right<->left.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> SplitShift::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
    double shiftAmountValue = shiftAmount.GetValue(frame_number);
    double splitPointValue = splitPoint.GetValue(frame_number);

    // The shader when there is a GPU to run it on, OpenCV otherwise. Before GetImageCV(), which
    // on a GPU-backed frame is a readback AND two full-frame cv::Mat conversions.
    if (ApplyOnGpu(frame, frame_number))
        return frame;

    // Get the frame's image
    auto imageCv = frame->GetImageCV();

    Podcastle::Effects::applySplitShiftEffect(imageCv, shiftAmountValue, splitPointValue, isHorizontal);
    frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// The SkSL twin of applySplitShiftEffect.
//
// The effect splits the frame in two and slides the halves in opposite directions, leaving
// transparent black where neither half lands. It is two integer rectangle blits in the C++, so it
// is exactly reproducible -- nothing resamples and nothing is interpolated.
//
// One thing that looks load-bearing and is not: the C++ passes the source as its own mask to
// copyTo, which copies each channel only where that channel is non-zero. Because the destination
// starts as zeros, "copy v where v != 0, else leave 0" is just "copy v", so the mask is a no-op
// and the fragment does not reproduce it.
const char* SplitShift::GpuShaderSource() const
{
	return R"SKSL(
uniform float splitAt;     // the split coordinate, in pixels, along the axis being split
uniform float shift;       // signed, in whole pixels, along the axis being moved
uniform float span;        // how many pixels each half actually covers -- see the host side
uniform float horizontal;  // 1 when the split runs along y and the shift along x

float4 main(float2 p) {
	float2 q = floor(p);
	// Which side of the split this pixel is on, and therefore which way it pulls from.
	float along = horizontal > 0.0 ? q.y : q.x;
	float moved = horizontal > 0.0 ? q.x : q.y;
	bool  first = along < splitAt;

	// Each half covers `span` pixels starting here. The two starts differ by the shift, and
	// which one starts at zero depends on the sign -- that is the C++'s two rectangle layouts
	// expressed once.
	float start = first ? max(0.0, -shift) : max(0.0, shift);
	if (moved < start || moved >= start + span)
		return float4(0.0);   // neither half reaches here

	float source = first ? moved + shift : moved - shift;
	float2 from = horizontal > 0.0 ? float2(source, q.y) : float2(q.x, source);
	return osBytes(from + 0.5) / 255.0;
}
)SKSL";
}

bool SplitShift::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
								int width, int height) const
{
	const double shift_value = shiftAmount.GetValue(frame_number);
	const double split_value = std::clamp(splitPoint.GetValue(frame_number), 0.0, 1.0);

	// The axis the shift runs along, and the one the split runs along, are swapped between the
	// two branches of the C++ -- a "horizontal" split divides top from bottom and slides them
	// sideways.
	const int extent = isHorizontal ? width : height;
	const int split_extent = isHorizontal ? height : width;

	double shift_pixels = shift_value * extent;
	// The C++ returns early on both of these rather than clamping, so the effect simply does not
	// apply; declining here leaves the frame untouched by the same rule.
	if (shift_pixels == 0 || std::abs(shift_pixels) > extent)
		return false;
	// Then it clamps to extent - 1, and every rect it builds truncates the double to an int.
	if (shift_pixels > 0)
		shift_pixels = std::min(shift_pixels, static_cast<double>(extent) - 1);
	else
		shift_pixels = -std::min(std::abs(shift_pixels), static_cast<double>(extent) - 1);

	// The span is NOT extent - |shift| in whole pixels, and the difference is a visible row.
	// For a positive shift the C++ builds its rectangles straight from the *double*, so the
	// height is int(extent - shift) -- 256 - 89.6 truncates to 166, where 256 - int(89.6) would
	// be 167, and that missing row is a full-strength seam. For a negative shift it truncates
	// the shift to an int first and then subtracts, so there the two agree. Asymmetric, and
	// reproduced rather than tidied up.
	const int shift_whole = static_cast<int>(shift_pixels);
	const int span = shift_pixels > 0 ? static_cast<int>(extent - shift_pixels)
									  : extent - std::abs(shift_whole);

	builder.uniform("splitAt") = static_cast<float>(static_cast<int>(split_extent * split_value));
	builder.uniform("shift") = static_cast<float>(shift_whole);
	builder.uniform("span") = static_cast<float>(span);
	builder.uniform("horizontal") = isHorizontal ? 1.0f : 0.0f;
	return true;
}

// Generate JSON string of this object
std::string SplitShift::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value SplitShift::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["splitPoint"] = splitPoint.JsonValue();
	root["shiftAmount"] = shiftAmount.JsonValue();
	root["isHorizontal"] = isHorizontal;

	// return JsonValue
	return root;
}

// Load JSON string into this object
void SplitShift::SetJson(const std::string value) {

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
void SplitShift::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["splitPoint"].isNull())
		splitPoint.SetJsonValue(root["splitPoint"]);
	if (!root["shiftAmount"].isNull())
		shiftAmount.SetJsonValue(root["shiftAmount"]);
	if (!root["isHorizontal"].isNull())
		isHorizontal = root["isHorizontal"].asBool();
}

// Get all properties for a specific frame
std::string SplitShift::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
//	root["x"] = add_property_json("X Shift", x.GetValue(requested_frame), "float", "", &x, -1, 1, false, requested_frame);
//	root["y"] = add_property_json("Y Shift", y.GetValue(requested_frame), "float", "", &y, -1, 1, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
