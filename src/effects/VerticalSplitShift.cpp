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
    int height = frame->GetHeight();

    double shiftAmountValue = shiftAmount.GetValue(frame_number) * height;
    double splitPointValue = splitPoint.GetValue(frame_number);

    if (shiftAmountValue == 0 || shiftAmountValue > height) {
        return frame;
    }

    // Get the frame's image
    auto src = frame->GetImageCV();

    // Check if the source image has an alpha channel; if not, add one.
    cv::Mat srcWithAlpha;
    if (src.channels() == 4) {
        srcWithAlpha = src;
    } else {
        // Convert the loaded image to BGRA (with alpha channel)
        cv::cvtColor(src, srcWithAlpha, cv::COLOR_BGR2BGRA);
    }

    // Initialize the result image with an alpha channel and set all pixels to transparent
    cv::Mat result(srcWithAlpha.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));

    // Adjust the logic for source and destination rectangles based on the shift direction
    cv::Rect leftSourceRect, leftDestRect, rightSourceRect, rightDestRect;
    if (shiftAmountValue > 0) {
        shiftAmountValue = std::min(shiftAmountValue, (double)srcWithAlpha.rows - 1);
        // Shifting parts upwards
        leftSourceRect = cv::Rect(0, shiftAmountValue, srcWithAlpha.cols / 2, srcWithAlpha.rows - shiftAmountValue);
        leftDestRect = cv::Rect(0, 0, srcWithAlpha.cols / 2, srcWithAlpha.rows - shiftAmountValue);

        rightSourceRect = cv::Rect(srcWithAlpha.cols / 2, 0, srcWithAlpha.cols / 2, srcWithAlpha.rows - shiftAmountValue);
        rightDestRect = cv::Rect(srcWithAlpha.cols / 2, shiftAmountValue, srcWithAlpha.cols / 2, srcWithAlpha.rows - shiftAmountValue);
    } else {
        shiftAmountValue = -std::min(std::abs(shiftAmountValue), (double)srcWithAlpha.rows - 1);

        // Shifting parts downwards
        int absShiftAmount = std::abs(shiftAmountValue);
        leftSourceRect = cv::Rect(0, 0, srcWithAlpha.cols / 2, srcWithAlpha.rows - absShiftAmount);
        leftDestRect = cv::Rect(0, absShiftAmount, srcWithAlpha.cols / 2, srcWithAlpha.rows - absShiftAmount);

        rightSourceRect = cv::Rect(srcWithAlpha.cols / 2, absShiftAmount, srcWithAlpha.cols / 2, srcWithAlpha.rows - absShiftAmount);
        rightDestRect = cv::Rect(srcWithAlpha.cols / 2, 0, srcWithAlpha.cols / 2, srcWithAlpha.rows - absShiftAmount);
    }

    // Copy the left part of the image
    cv::Mat leftPart = srcWithAlpha(leftSourceRect);
    leftPart.copyTo(result(leftDestRect), leftPart);

    // Copy the right part of the image
    cv::Mat rightPart = srcWithAlpha(rightSourceRect);
    rightPart.copyTo(result(rightDestRect), rightPart);

    frame->SetImageCV(result);
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
