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
#include "EffectShaders.h"
#include "image-processing-lib/src/Planner/EffectPlan.h"

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

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* SplitShift::GpuShaderSource() const
{
	return openshot::shaders::kSplitShift;
}

bool SplitShift::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
					int width, int height) const
{
	// Resolved by the shared planner (image-processing-lib/src/Planner), the same code the editor
	// runs, so the two cannot disagree about what these values mean. It declines -- and the C++
	// twin runs -- for every case the fragment does not cover.
	return BindPlan(builder, Podcastle::Effects::planEffect(
		"SPLIT_SHIFT", {{"shiftAmount", shiftAmount.GetValue(frame_number)}, {"shiftPoint", splitPoint.GetValue(frame_number)}, {"isHorizontal", isHorizontal ? 1.0 : 0.0}}, width, height));
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
