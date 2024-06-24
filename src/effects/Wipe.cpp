#include "Wipe.h"

#include "Exceptions.h"

#include "ReaderBase.h"
#include "ChunkReader.h"
#include "FFmpegReader.h"
#include "QtImageReader.h"
#include "QPainter"
#include "QPainterPath"
#include "image-processing-lib/effects.h"

using namespace openshot;

/// Blank constructor, useful when using Json to load the effect properties
Wipe::Wipe(Keyframe levelsLowPercentage, Keyframe levelsHighPercentage)
    : mLevelsLowPercentage(levelsLowPercentage), mLevelsHighPercentage(levelsHighPercentage) {
	// Init effect properties
	init_effect_details();
}

// Init effect settings
void Wipe::init_effect_details()
{
	/// Initialize the values of the EffectInfo struct.
	InitEffectInfo();

	/// Set the effect info
	info.class_name = "Wipe";
	info.name = "Alpha Wipe Transition";
	info.description = "Uses a grayscale mask image to gradually wipe / transition between 2 images.";
	info.has_audio = false;
	info.has_video = true;
}

// This method is required for all derived classes of EffectBase, and returns a
// modified openshot::Frame object
std::shared_ptr<openshot::Frame> Wipe::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) {
    const auto lowPercentage = mLevelsLowPercentage.GetValue(frame_number);
    const auto highPercentage = mLevelsHighPercentage.GetValue(frame_number);

    auto imageCv = frame->GetImageCV();
    Podcastle::Effects::applyLevelAdjustmentMaskEffect(imageCv, lowPercentage, highPercentage);

    // return the modified frame
    frame->SetImageCV(imageCv);
	return frame;
}

// Generate JSON string of this object
std::string Wipe::Json() const {

	// Return formatted string
	return JsonValue().toStyledString();
}

// Generate Json::Value for this object
Json::Value Wipe::JsonValue() const {

	// Create root json object
	Json::Value root = EffectBase::JsonValue(); // get parent properties
	root["type"] = info.class_name;
    root["levelsLowPercentage"] = mLevelsLowPercentage.JsonValue();
    root["levelsHighPercentage"] = mLevelsHighPercentage.JsonValue();
	// return JsonValue
	return root;
}

// Load JSON string into this object
void Wipe::SetJson(const std::string value) {

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
void Wipe::SetJsonValue(const Json::Value root) {
	// Set parent data
	EffectBase::SetJsonValue(root);
}

// Get all properties for a specific frame
std::string Wipe::PropertiesJSON(int64_t requested_frame) const {

	// Generate JSON properties list
	Json::Value root = BasePropertiesJSON(requested_frame);

	// Add replace_image choices (dropdown style)
//	root["replace_image"] = add_property_json("Replace Image", replace_image, "int", "", NULL, 0, 1, false, requested_frame);
//	root["replace_image"]["choices"].append(add_property_choice_json("Yes", true, replace_image));
//	root["replace_image"]["choices"].append(add_property_choice_json("No", false, replace_image));
//
//	// Keyframes
//	root["brightness"] = add_property_json("Brightness", brightness.GetValue(requested_frame), "float", "", &brightness, -1.0, 1.0, false, requested_frame);
//	root["contrast"] = add_property_json("Contrast", contrast.GetValue(requested_frame), "float", "", &contrast, 0, 20, false, requested_frame);

	// Return formatted string
	return root.toStyledString();
}
