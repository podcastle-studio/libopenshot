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

#include "VerticalSplitShift.h"
#include "Exceptions.h"
#include "image-processing-lib/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
VerticalSplitShift::VerticalSplitShift() : shiftAmount(0.0), splitPoint(0.5) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
VerticalSplitShift::VerticalSplitShift(Keyframe newShiftAmount, Keyframe newSplitPoint)
    : splitPoint(newSplitPoint), shiftAmount(newShiftAmount)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void VerticalSplitShift::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "VerticalShift";
	info.name = "VerticalShift";
	info.description = "Vertically Shift the image up, down.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> VerticalSplitShift::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    double shiftAmountValue = shiftAmount.GetValue(frame_number);
    double splitPointValue = splitPoint.GetValue(frame_number);

    // Get the frame's image
    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyVerticalSplitShiftEffect(imageCv, shiftAmountValue, splitPointValue);
    frame->SetImageCV(imageCv);

	// return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string VerticalSplitShift::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value VerticalSplitShift::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["splitPoint"] = splitPoint.JsonValue();
	root["shiftAmount"] = shiftAmount.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void VerticalSplitShift::SetJson(const std::string value) {

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
void VerticalSplitShift::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["splitPoint"].isNull())
		splitPoint.SetJsonValue(root["splitPoint"]);
	if (!root["shiftAmount"].isNull())
		shiftAmount.SetJsonValue(root["shiftAmount"]);
}

// Get all properties for a specific frame
std::string VerticalSplitShift::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
//	root["x"] = add_property_json("X Shift", x.GetValue(requested_frame), "float", "", &x, -1, 1, false, requested_frame);
//	root["y"] = add_property_json("Y Shift", y.GetValue(requested_frame), "float", "", &y, -1, 1, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
