/**
 * @file
 * @brief Source file for Bars effect class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2019 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Bars.h"
#include "Exceptions.h"
#include "./image-processing-lib/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Bars::Bars() : color("#000000"), left(0.0), top(0.1), right(0.0), bottom(0.1) {
	// Init effect properties
	init_effect_details();
}

// Default constructor
Bars::Bars(Color color, Keyframe left, Keyframe top, Keyframe right, Keyframe bottom) :
		color(color), left(left), top(top), right(right), bottom(bottom)
{
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Bars::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Bars";
	info.name = "Bars";
	info.description = "Add colored bars around your video.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Bars::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
	// Get the frame's image
	std::shared_ptr<QImage> frame_image = frame->GetImage();

	// Get current keyframe values
	double left_value = left.GetValue(frame_number);
	double top_value = top.GetValue(frame_number);
	double right_value = right.GetValue(frame_number);
	double bottom_value = bottom.GetValue(frame_number);

    // Get pixel array pointer
    auto *pixels = (unsigned char *)frame_image->bits();
    int width = frame_image->width();
    int height = frame_image->height();

    // Apply the bars effect directly to the pixel data
    Podcastle::Effects::applyBarsEffect(pixels, width, height, left_value, top_value, right_value, bottom_value);

    // return the modified frame
	return frame;
}

// Generate JSON string of this object
std::string Bars::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Bars::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
	root["color"] = color.JsonValue();
	root["left"] = left.JsonValue();
	root["top"] = top.JsonValue();
	root["right"] = right.JsonValue();
	root["bottom"] = bottom.JsonValue();

	// return JsonValue
	return root;
}

// Load JSON string into this object
void Bars::SetJson(const std::string value) {

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
void Bars::SetJsonValue(const Json::Value root) {

	// Set parent data
	EffectBase::SetJsonValue(root);

	// Set data from Json (if key is found)
	if (!root["color"].isNull())
		color.SetJsonValue(root["color"]);
	if (!root["left"].isNull())
		left.SetJsonValue(root["left"]);
	if (!root["top"].isNull())
		top.SetJsonValue(root["top"]);
	if (!root["right"].isNull())
		right.SetJsonValue(root["right"]);
	if (!root["bottom"].isNull())
		bottom.SetJsonValue(root["bottom"]);
}

// Get all properties for a specific frame
std::string Bars::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Keyframes
	root["color"] = add_property_json("Bar Color", 0.0, "color", "", &color.red, 0, 255, false, requested_frame);
	root["color"]["red"] = add_property_json("Red", color.red.GetValue(requested_frame), "float", "", &color.red, 0, 255, false, requested_frame);
	root["color"]["blue"] = add_property_json("Blue", color.blue.GetValue(requested_frame), "float", "", &color.blue, 0, 255, false, requested_frame);
	root["color"]["green"] = add_property_json("Green", color.green.GetValue(requested_frame), "float", "", &color.green, 0, 255, false, requested_frame);
	root["left"] = add_property_json("Left Size", left.GetValue(requested_frame), "float", "", &left, 0.0, 0.5, false, requested_frame);
	root["top"] = add_property_json("Top Size", top.GetValue(requested_frame), "float", "", &top, 0.0, 0.5, false, requested_frame);
	root["right"] = add_property_json("Right Size", right.GetValue(requested_frame), "float", "", &right, 0.0, 0.5, false, requested_frame);
	root["bottom"] = add_property_json("Bottom Size", bottom.GetValue(requested_frame), "float", "", &bottom, 0.0, 0.5, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
