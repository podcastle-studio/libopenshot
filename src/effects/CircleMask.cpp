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
#include "EffectShaders.h"
#include "image-processing-lib/src/Planner/EffectPlan.h"

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

// The shared SkSL source lives in image-processing-lib/shaders/ so the editor loads the
// same bytes through CanvasKit; it is embedded here at build time. Read it there --
// including why it is written the way it is.
const char* CircleMask::GpuShaderSource() const
{
	return openshot::shaders::kCircleMask;
}

bool CircleMask::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
								int width, int height) const
{
	// Resolved by the shared planner, the same code the editor runs. >= 1 and <= 0 are an
	// identity and a clear there; ApplyOnGpu takes both before this is reached.
	Podcastle::Effects::EffectPlan plan;
	PlanForFrame(frame_number, width, height, plan);
	return BindPlan(builder, plan);
}

bool CircleMask::PlanForFrame(int64_t frame_number, int width, int height,
							  Podcastle::Effects::EffectPlan& plan) const
{
	plan = Podcastle::Effects::planEffect(
		"CIRCLE_MASK", {{"circleRadius", circleRadius.GetValue(frame_number)}}, width, height);
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
